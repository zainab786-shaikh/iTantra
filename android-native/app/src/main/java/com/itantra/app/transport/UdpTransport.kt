package com.itantra.app.transport

import android.content.Context
import android.util.Log
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.ITantraPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.SocketException
import java.util.concurrent.atomic.AtomicInteger

private const val TAG = "UdpTransport"

/** Default port. Arbitrary, unassigned, and above the privileged range. */
const val DEFAULT_LINK_PORT = 47821

/** How often a keepalive goes out while the link is otherwise silent. */
private const val HELLO_INTERVAL_MS = 15_000L

/** Link state is re-evaluated this often, so a peer going away is noticed. */
private const val TICK_MS = 5_000L

/** A peer unheard-from for longer than this is treated as gone. */
private const val PEER_TIMEOUT_MS = 45_000L

/**
 * A real wireless [Transport]: one UDP socket, one peer, no infrastructure.
 *
 * No Wi-Fi Direct, no Nearby Connections, no Play Services, no discovery
 * protocol — two devices on the same Wi-Fi (typically one phone's hotspot,
 * with no uplink) exchanging datagrams. That keeps the "zero external
 * runtime dependencies" property the rest of the app already has, and it is
 * the shape a constrained-radio transport will have too: send bytes, receive
 * bytes, know whether the far end is still there.
 *
 * ## Finding the peer
 *
 * Three mechanisms, in increasing order of authority:
 *
 * 1. **Broadcast.** With nothing configured, frames go to the subnet
 *    broadcast address. On a small hotspot subnet this is often enough on
 *    its own.
 * 2. **A configured host.** The operator types the peer's address on the
 *    link screen. This is the reliable path and the one to use on camera.
 * 3. **A learned socket address.** The exact `address:port` a frame arrived
 *    from, which outranks both of the above.
 *
 * Point 3 is what makes this work across a NAT, and it is the reason the
 * learned peer is an `InetSocketAddress` rather than an `InetAddress`. When
 * a peer's traffic is translated on its way to us, it reaches us from an
 * address and an *ephemeral port* that the peer itself does not know and
 * nobody can type in ahead of time. Replying to the address it arrived from
 * but to our own well-known port would be dropped. Replying to the exact
 * socket address it arrived from is delivered.
 *
 * This is ordinary UDP peer behaviour, not a special case for any one kind
 * of device: it is equally correct between two phones on a hotspot, where
 * the learned address simply matches the configured one. Nothing above the
 * transport knows or needs to know which situation it is in.
 *
 * ## Keepalive
 *
 * A translated return path only stays open while traffic flows through it,
 * and idle mappings are reclaimed in about a minute. A [PacketCodec.MODE_HELLO]
 * frame every [HELLO_INTERVAL_MS] keeps it open and doubles as the link's
 * liveness signal: [isConnected] means "a frame arrived from the peer within
 * [PEER_TIMEOUT_MS]", which is an observation rather than an assumption.
 */
class UdpTransport(
    context: Context,
    private val port: Int = DEFAULT_LINK_PORT,
    /** Language announced in keepalives. Cosmetic; carried because the header has the field anyway. */
    @Volatile var announcedLanguage: String = "en-IN",
) : Transport, LinkControl {

    private val appContext = context.applicationContext
    private val selfNodeId: Short = DeviceId.getNodeId(appContext)
    private val prefs = appContext.getSharedPreferences("itantra-link", Context.MODE_PRIVATE)

    override val name: String = "udp://:$port"
    override val nodeLabel: String = DeviceId.nodeLabel(selfNodeId)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    /**
     * Per-message counter, and it must NOT restart at zero.
     *
     * The receiver reconstructs a packet's identity from `nodeId + seq` (see
     * [PacketCodec.frameId]) and dedups on it — the TTS queue refuses an id
     * it has already spoken. A counter that reset on every launch therefore
     * replayed ids the far end had already seen, and those messages arrived,
     * displayed, and were silently never spoken. No error, because from the
     * receiver's point of view they were duplicates.
     *
     * Persisted rather than randomised so the property is guaranteed rather
     * than merely probable, and advanced by a block on startup so a process
     * killed between writes can never hand out an id twice.
     */
    private val sequence: AtomicInteger

    @Volatile private var socket: DatagramSocket? = null
    @Volatile private var learnedPeer: InetSocketAddress? = null
    @Volatile private var configuredPeer: InetSocketAddress? = null
    @Volatile private var lastHeardAt: Long = 0L
    @Volatile private var lastHelloAt: Long = 0L
    @Volatile private var connected = false

    private val connectionListeners = mutableSetOf<(Boolean) -> Unit>()
    private val receiveListeners = mutableSetOf<(ITantraPacket) -> Unit>()

    private val _localAddress = MutableStateFlow("resolving…")
    override val localAddress: StateFlow<String> = _localAddress.asStateFlow()

    private val _peerAddress = MutableStateFlow<String?>(null)
    override val peerAddress: StateFlow<String?> = _peerAddress.asStateFlow()

    private val _peerSource = MutableStateFlow(PeerSource.NONE)
    override val peerSource: StateFlow<PeerSource> = _peerSource.asStateFlow()

    private val _lastHeardMs = MutableStateFlow<Long?>(null)
    override val lastHeardMs: StateFlow<Long?> = _lastHeardMs.asStateFlow()

    private val _configuredHost = MutableStateFlow(prefs.getString(KEY_PEER_HOST, "") ?: "")
    override val configuredHost: StateFlow<String> = _configuredHost.asStateFlow()

    init {
        // Reserve a whole block up front and persist the end of it. Anything
        // handed out within the block is safe even if we are killed without
        // writing again; the cost is that a restart burns up to one block.
        val resumeAt = prefs.getInt(KEY_SEQUENCE, 0)
        sequence = AtomicInteger(resumeAt)
        prefs.edit().putInt(KEY_SEQUENCE, (resumeAt + SEQUENCE_BLOCK) and 0xFFFF).apply()

        _configuredHost.value.takeIf { it.isNotBlank() }?.let { applyConfiguredHost(it) }
        // The receive loop opens the socket; nothing else needs to race it to
        // do so first.
        scope.launch { receiveLoop() }
        scope.launch { heartbeatLoop() }
    }

    // -----------------------------------------------------------------------
    // Transport
    // -----------------------------------------------------------------------

    override suspend fun sendPacket(packet: ITantraPacket): Boolean {
        val target = currentTarget()
        if (target == null) {
            Log.w(TAG, "no peer and no broadcast address; dropping ${packet.id}")
            return false
        }

        val seq = nextSequence()
        val frame = PacketCodec.serialize(packet, selfNodeId, seq)
        if (frame == null) {
            Log.w(TAG, "frame for ${packet.id} exceeds ${PacketCodec.MAX_FRAME_BYTES} B; dropping")
            return false
        }

        return withContext(Dispatchers.IO) {
            val active = socket ?: openSocket()
            if (active == null) return@withContext false
            try {
                active.send(DatagramPacket(frame, frame.size, target))
                Log.d(TAG, "-> ${packet.priority.value} ${packet.language} ${frame.size} B to $target")
                true
            } catch (e: Exception) {
                // Reported honestly rather than swallowed: the transmitter's
                // log renders this as FAILED, which is what actually happened.
                Log.w(TAG, "send failed for ${packet.id}", e)
                false
            }
        }
    }

    override fun isConnected(): Boolean = connected

    override fun onConnectionChange(listener: (Boolean) -> Unit): () -> Unit {
        connectionListeners.add(listener)
        return { connectionListeners.remove(listener) }
    }

    override fun onPacketReceived(listener: (ITantraPacket) -> Unit): () -> Unit {
        receiveListeners.add(listener)
        return { receiveListeners.remove(listener) }
    }

    // -----------------------------------------------------------------------
    // LinkControl
    // -----------------------------------------------------------------------

    override fun setPeerHost(host: String) {
        val trimmed = host.trim()
        _configuredHost.value = trimmed
        prefs.edit().putString(KEY_PEER_HOST, trimmed).apply()

        // Re-typing an address is an override, not a hint: drop whatever was
        // learned so the operator's input actually takes effect.
        learnedPeer = null
        if (trimmed.isBlank()) {
            configuredPeer = null
        } else {
            applyConfiguredHost(trimmed)
        }
        publishPeer()
        // Reach out immediately rather than waiting for the next tick.
        scope.launch { sendHello() }
    }

    // -----------------------------------------------------------------------
    // Internals
    // -----------------------------------------------------------------------

    /**
     * Next id, persisting a fresh block whenever this run exhausts its own.
     */
    private fun nextSequence(): Int {
        val next = sequence.getAndIncrement() and 0xFFFF
        if (next % SEQUENCE_BLOCK == 0) {
            prefs.edit().putInt(KEY_SEQUENCE, (next + SEQUENCE_BLOCK) and 0xFFFF).apply()
        }
        return next
    }

    /**
     * Drop the learned address and fall back to whatever else we know.
     *
     * Deliberately does not clear [lastHeardAt]: link state stays "down"
     * until a frame genuinely arrives, so forgetting an address never makes
     * the badge claim a connection that does not exist.
     */
    private fun forgetLearnedPeer() {
        if (learnedPeer == null) return
        learnedPeer = null
        // Re-resolve the typed host: on a new network the operator's entry is
        // the only address we have, and it may now be reachable again.
        _configuredHost.value.takeIf { it.isNotBlank() }?.let { applyConfiguredHost(it) }
        publishPeer()
    }

    private fun applyConfiguredHost(host: String) {
        configuredPeer = try {
            InetSocketAddress(InetAddress.getByName(host), port)
        } catch (e: Exception) {
            Log.w(TAG, "unusable peer host \"$host\"", e)
            null
        }
    }

    /**
     * A learned socket address wins over a typed one — see the class comment:
     * it is the only address known to actually carry frames back.
     */
    private fun currentTarget(): InetSocketAddress? =
        learnedPeer ?: configuredPeer ?: broadcastTarget()

    /**
     * The subnet-directed broadcast address of the active interface — e.g.
     * `192.168.0.255` on a `192.168.0.0/24` network.
     *
     * Deliberately not the limited broadcast address `255.255.255.255`:
     * Android refuses to send to it (`EPERM` from `sendto`) on at least some
     * devices, so using it produces a link that fails silently every tick.
     * The subnet broadcast is derived from the interface itself, which also
     * means it follows the device onto whatever network it joins.
     */
    private fun broadcastTarget(): InetSocketAddress? = try {
        NetworkInterface.getNetworkInterfaces()
            .asSequence()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { it.interfaceAddresses.asSequence() }
            .mapNotNull { it.broadcast }
            .firstOrNull()
            ?.let { InetSocketAddress(it, port) }
    } catch (e: Exception) {
        null
    }

    /**
     * Synchronized, and it must stay that way.
     *
     * The receive loop and the heartbeat loop both open the socket lazily. An
     * unsynchronized check-then-create let both win the race, and because
     * `reuseAddress` is set, the second bind *succeeded* rather than failing
     * loudly: two live sockets on the same port, with the receive loop
     * reading the one the OS was not delivering to. The link then looks
     * perfectly healthy from the sending side and receives nothing.
     */
    @Synchronized
    private fun openSocket(): DatagramSocket? {
        socket?.let { if (!it.isClosed) return it }
        // Read into a local first: inside the apply block below, `port` would
        // resolve to DatagramSocket.getPort(), which is -1 until the socket is
        // bound - so binding to it fails with "port out of range: -1".
        val bindPort = port
        return try {
            DatagramSocket(null).apply {
                reuseAddress = true
                broadcast = true
                bind(InetSocketAddress(bindPort))
                socket = this
                Log.i(TAG, "listening on :$bindPort as $nodeLabel")
            }
        } catch (e: Exception) {
            Log.e(TAG, "could not bind :$bindPort", e)
            null
        }
    }

    private suspend fun receiveLoop() {
        val buffer = ByteArray(PacketCodec.MAX_FRAME_BYTES)
        while (scope.isActive) {
            val active = socket ?: openSocket()
            if (active == null) {
                delay(1_000)
                continue
            }
            try {
                val datagram = DatagramPacket(buffer, buffer.size)
                active.receive(datagram)
                handleDatagram(buffer, datagram.length, datagram.socketAddress as? InetSocketAddress)
            } catch (e: SocketException) {
                if (!scope.isActive) return // disposed; expected
                Log.w(TAG, "socket error, reopening", e)
                socket = null
                delay(500)
            } catch (e: Exception) {
                // One malformed datagram must never end the loop.
                Log.w(TAG, "receive error", e)
            }
        }
    }

    private fun handleDatagram(buffer: ByteArray, length: Int, from: InetSocketAddress?) {
        // Our own broadcast comes straight back to us on most networks.
        if (PacketCodec.peekNodeId(buffer, length) == selfNodeId) return

        val frame = PacketCodec.deserialize(buffer, length) ?: return

        if (from != null) {
            learnedPeer = from
            publishPeer()
        }
        lastHeardAt = System.currentTimeMillis()
        updateConnected(true)

        when (frame) {
            is PacketCodec.Frame.Hello -> {
                // Liveness only. Deliberately not surfaced to the app: a
                // keepalive is not a message and must never reach the receive
                // log or the speech queue.
                Log.d(TAG, "<- HELLO ${DeviceId.nodeLabel(frame.nodeId)} from $from")
            }
            is PacketCodec.Frame.Data -> {
                Log.d(TAG, "<- ${frame.packet.priority.value} ${frame.packet.language} $length B from $from")
                for (listener in receiveListeners.toList()) listener(frame.packet)
            }
        }
    }

    private suspend fun heartbeatLoop() {
        while (scope.isActive) {
            val now = System.currentTimeMillis()

            // lastHelloAt only advances on a successful send, so while the
            // link is down this retries every tick rather than every
            // interval. That is intended: it is one 11-byte datagram, and it
            // makes the link come back as soon as the network does.
            if (now - lastHelloAt >= HELLO_INTERVAL_MS) sendHello()

            // Re-resolved rather than cached: the operator may join the
            // hotspot after the app is already running.
            val localNow = resolveLocalAddress()
            if (localNow != _localAddress.value) {
                // Our own address changed, so the network underneath us
                // changed. Any address we learned on the old network is
                // meaningless now - and worse than meaningless, because a
                // learned peer outranks the configured one, so keeping it
                // would aim every frame at somewhere unreachable.
                Log.i(TAG, "local address ${_localAddress.value} -> $localNow; forgetting learned peer")
                forgetLearnedPeer()
            }
            _localAddress.value = localNow

            val heard = if (lastHeardAt == 0L) null else now - lastHeardAt
            _lastHeardMs.value = heard

            // A peer we have not heard from for longer than the timeout is
            // not merely "down" - the address itself is suspect. Dropping it
            // lets the configured host, or broadcast, take over again so the
            // link can re-learn. Without this the transport aims at a dead
            // address forever and cannot recover without an app restart,
            // which is exactly what a network toggle used to cause.
            if (heard != null && heard >= PEER_TIMEOUT_MS && learnedPeer != null) {
                Log.i(TAG, "peer silent for ${heard}ms; forgetting learned address")
                forgetLearnedPeer()
            }

            updateConnected(heard != null && heard < PEER_TIMEOUT_MS)

            delay(TICK_MS)
        }
    }

    private suspend fun sendHello() {
        val target = currentTarget() ?: return
        val active = socket ?: openSocket() ?: return
        val frame = PacketCodec.serializeHello(selfNodeId, announcedLanguage)
        try {
            withContext(Dispatchers.IO) {
                active.send(DatagramPacket(frame, frame.size, target))
            }
            lastHelloAt = System.currentTimeMillis()
        } catch (e: Exception) {
            Log.d(TAG, "hello to $target failed", e)
        }
    }

    private fun updateConnected(next: Boolean) {
        if (connected == next) return
        connected = next
        for (listener in connectionListeners.toList()) listener(next)
    }

    private fun publishPeer() {
        val learned = learnedPeer
        val configured = configuredPeer
        when {
            learned != null -> {
                _peerAddress.value = "${learned.address.hostAddress}:${learned.port}"
                _peerSource.value = PeerSource.LEARNED
            }
            configured != null -> {
                _peerAddress.value = "${configured.address?.hostAddress ?: configured.hostString}:${configured.port}"
                _peerSource.value = PeerSource.CONFIGURED
            }
            else -> {
                _peerAddress.value = null
                _peerSource.value = PeerSource.NONE
            }
        }
    }

    /** This device's own IPv4 address on the local network, for the operator to read out. */
    private fun resolveLocalAddress(): String = try {
        NetworkInterface.getNetworkInterfaces()
            .asSequence()
            .filter { it.isUp && !it.isLoopback }
            .flatMap { it.inetAddresses.asSequence() }
            .firstOrNull { !it.isLoopbackAddress && it.hostAddress?.contains(':') == false }
            ?.hostAddress
            ?: "no network"
    } catch (e: Exception) {
        "unavailable"
    }

    fun dispose() {
        scope.cancel()
        try {
            socket?.close()
        } catch (e: Exception) {
            // Closing a socket that is already gone is not worth reporting.
        }
        socket = null
        connectionListeners.clear()
        receiveListeners.clear()
    }

    private companion object {
        const val KEY_PEER_HOST = "peer-host"
        const val KEY_SEQUENCE = "send-sequence"

        /** Ids reserved per launch, so a crash cannot reissue one. */
        const val SEQUENCE_BLOCK = 64
    }
}
