package com.itantra.app.transport

import com.itantra.app.codec.CodecMode
import com.itantra.app.packet.ITantraPacket
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** LoRa SF12 — the slowest common long-range setting, and the default here. */
const val LORA_SF12_BPS = 250

/** Rates the operator can select, with what each one stands for. */
val LINK_RATE_PRESETS: List<Pair<String, Int>> = listOf(
    "250 bps" to LORA_SF12_BPS,
    "1 kbps" to 1_000,
    "5 kbps" to 5_000,
)

/**
 * What one message cost, in bytes, on both sides of the comparison.
 *
 * The two figures are deliberately like-for-like: **both include the frame
 * header**. A compressed frame raced against naked UTF-8 text would flatter
 * the codec on Indic scripts and be outright wrong on English, where the
 * 11-byte header can exceed what PACK7 saves. Any real system carries a
 * header, so both sides of the race carry one.
 */
data class FrameCost(
    /** UTF-8 size of the source text — what an uncompressed system would send. */
    val originalBytes: Int,
    /** What this codec actually put on the wire. */
    val payloadBytes: Int,
    val mode: CodecMode,
    val headerBytes: Int,
    /** True for a message this device sent, false for one it received. */
    val outbound: Boolean,
) {
    /** The frame an uncompressed system would have transmitted. */
    val uncompressedFrameBytes: Int get() = headerBytes + originalBytes

    /** The frame that actually went out. */
    val sentFrameBytes: Int get() = headerBytes + payloadBytes
}

/** Airtime is arithmetic: bytes x 8 / bits per second. Not a fudge factor. */
fun airtimeMs(bytes: Int, bitsPerSecond: Int): Long =
    if (bitsPerSecond <= 0) 0L else (bytes * 8L * 1000L) / bitsPerSecond

/** Operator controls for the simulated link rate. */
interface ThrottleControl {
    val bitsPerSecond: StateFlow<Int>
    val enabled: StateFlow<Boolean>

    /** The most recent message this device sent or received, for the race view. */
    val lastFrame: StateFlow<FrameCost?>

    fun setRate(bitsPerSecond: Int)
    fun setEnabled(enabled: Boolean)
}

/**
 * A decorator that makes any [Transport] take as long as a slow radio would.
 *
 * ```
 *        app
 *         |
 *  ThrottledTransport      delays by simulated airtime
 *         |
 *     UdpTransport         DatagramSocket
 *         |
 *       Wi-Fi
 * ```
 *
 * ## This is a rate limiter, not a radio, and it is labelled as one
 *
 * The delay is not invented: at a given bitrate, the time a payload occupies
 * the channel is `bytes x 8 / bitsPerSecond` and nothing else. Set to 250 bps
 * it reproduces the airtime of LoRa at SF12, a documented spreading-factor
 * rate. What it does **not** reproduce is anything else about a radio — no
 * propagation, no interference, no packet loss, no duty cycle. It exists to
 * make the consequence of a smaller payload visible on camera, and the UI
 * says so on screen.
 *
 * ## It delays on the full frame
 *
 * Not on the payload. A channel carries the header too, so charging airtime
 * for the payload alone would quietly overstate the saving.
 *
 * Because it is a decorator, nothing else in the pipeline knows it exists,
 * and [setEnabled] with false removes it from the path entirely — the same
 * build demonstrates the throttled and unthrottled link.
 */
class ThrottledTransport(
    private val inner: Transport,
    initialBitsPerSecond: Int = LORA_SF12_BPS,
    initialEnabled: Boolean = true,
    /**
     * Called whenever the operator changes the rate, so the choice can be
     * persisted.
     *
     * Kept as a lambda rather than taking a Context: this class has no other
     * Android dependency and is unit-testable because of it. The owner
     * supplies storage.
     */
    private val onSettingsChanged: (bitsPerSecond: Int, enabled: Boolean) -> Unit = { _, _ -> },
) : Transport, ThrottleControl {

    /**
     * Deliberately **not** `"${inner.name} @250bps"`.
     *
     * The plan called for the rate to ride in the transport's name so it
     * would always be on screen. It cannot: the view models capture
     * `transport.name` once, into a `val`, at construction — so a rate the
     * operator changed later would still read as the startup value, which is
     * worse than not showing it at all. The live rate is surfaced by the
     * screens' own subtitle instead, from [bitsPerSecond].
     */
    override val name: String = inner.name

    private val _bitsPerSecond = MutableStateFlow(initialBitsPerSecond)
    override val bitsPerSecond: StateFlow<Int> = _bitsPerSecond.asStateFlow()

    private val _enabled = MutableStateFlow(initialEnabled)
    override val enabled: StateFlow<Boolean> = _enabled.asStateFlow()

    private val _lastFrame = MutableStateFlow<FrameCost?>(null)
    override val lastFrame: StateFlow<FrameCost?> = _lastFrame.asStateFlow()

    override fun setRate(bitsPerSecond: Int) {
        _bitsPerSecond.value = bitsPerSecond.coerceAtLeast(1)
        onSettingsChanged(_bitsPerSecond.value, _enabled.value)
    }

    override fun setEnabled(enabled: Boolean) {
        _enabled.value = enabled
        onSettingsChanged(_bitsPerSecond.value, _enabled.value)
    }

    override suspend fun sendPacket(packet: ITantraPacket): Boolean {
        val cost = costOf(packet, outbound = true)
        _lastFrame.value = cost

        if (_enabled.value) {
            // Delayed before delegating, so the far end genuinely receives it
            // only after the airtime has elapsed.
            delay(airtimeMs(cost.sentFrameBytes, _bitsPerSecond.value))
        }
        return inner.sendPacket(packet)
    }

    /**
     * Received frames are observed for the race view but never delayed: the
     * airtime was already spent by the sender, and charging for it twice
     * would be double counting.
     */
    override fun onPacketReceived(listener: (ITantraPacket) -> Unit): () -> Unit =
        inner.onPacketReceived { packet ->
            _lastFrame.value = costOf(packet, outbound = false)
            listener(packet)
        }

    private fun costOf(packet: ITantraPacket, outbound: Boolean) = FrameCost(
        originalBytes = packet.originalBytes,
        payloadBytes = packet.payload.size,
        mode = packet.mode,
        headerBytes = PacketCodec.HEADER_BYTES,
        outbound = outbound,
    )

    override fun isConnected(): Boolean = inner.isConnected()

    override fun onConnectionChange(listener: (Boolean) -> Unit): () -> Unit =
        inner.onConnectionChange(listener)
}
