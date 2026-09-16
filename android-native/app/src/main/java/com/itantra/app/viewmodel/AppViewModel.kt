package com.itantra.app.viewmodel

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.itantra.app.native.NativeBridge
import com.itantra.app.native.NativeEngine
import com.itantra.app.transport.LORA_SF12_BPS
import com.itantra.app.transport.LinkControl
import com.itantra.app.transport.MockTransport
import com.itantra.app.transport.ThrottleControl
import com.itantra.app.transport.ThrottledTransport
import com.itantra.app.transport.Transport
import com.itantra.app.transport.UdpTransport
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File
import java.security.SecureRandom

private const val TAG = "AppViewModel"

/**
 * Direct port of App.tsx's root-component ownership: one transport instance
 * shared by both the transmitter and the receiver, and both controllers
 * constructed unconditionally regardless of which screen is currently shown.
 *
 * App.tsx's own comment explains why this matters and is reproduced here
 * verbatim because the same constraint applies to this Kotlin port: "Both
 * controllers are owned here, not inside their screens... That is
 * required, not a style choice: TransmitterScreen and ReceiverScreen are
 * each mounted only while their mode is active, so a hook living inside
 * either one loses all its state (and, for the receiver, its
 * transport.onPacketReceived subscription) the moment the operator
 * switches away." An Android ViewModel scoped to the Activity (not to a
 * particular Compose screen/route) reproduces exactly this lifetime.
 *
 * This is also the single place where the transport is chosen. Swapping the
 * link is one line, by design — see [USE_REAL_LINK].
 */
class AppViewModel @JvmOverloads constructor(
    application: Application,
    /** Test only (C-34): announce and require this descriptor instead of the engine's. */
    private val compatibilityOverride: IntArray? = null,
) : AndroidViewModel(application) {

    /**
     * The phone's native engine: packs, contexts, counter and keys.
     * Null only when libitantra-native.so or its packs failed to load; both
     * view models then say so instead of sending or decoding anything.
     */
    private val nativeEngine: NativeEngine? = createNativeEngine(application)

    private val useRealLink = USE_REAL_LINK

    private val udp: UdpTransport? = if (useRealLink) UdpTransport(application) else null
    private val base: Transport = udp ?: MockTransport()

    /** Phase 12: this launch's HELLO nonce (`packet §6.7`), fresh local randomness. */
    private val sessionNonce = ByteArray(32).also { SecureRandom().nextBytes(it) }
    @Volatile private var peerNonce: ByteArray? = null

    /** The peer's language, from its HELLO (`context §18.1`); the sender's cross-language rule uses it. */
    @Volatile var peerLanguage: String? = null
        private set

    /** True once a session key has been derived with the peer. */
    @Volatile var sessionReady: Boolean = false
        private set

    /** C-34: why pairing was refused (version / codebook mismatch); null when not refused. */
    private val _pairingError = MutableStateFlow<String?>(null)
    val pairingError: StateFlow<String?> = _pairingError.asStateFlow()

    init {
        if (udp != null && nativeEngine != null) startPairing(application, udp, nativeEngine)
    }

    // Persisted, so a rate chosen for filming survives an app restart rather
    // than silently reverting to the default between takes.
    private val prefs = application.getSharedPreferences("itantra-link", Context.MODE_PRIVATE)
    private val throttled = ThrottledTransport(
        inner = base,
        initialBitsPerSecond = prefs.getInt(KEY_THROTTLE_BPS, LORA_SF12_BPS),
        initialEnabled = prefs.getBoolean(KEY_THROTTLE_ON, true),
        onSettingsChanged = { bps, enabled ->
            prefs.edit().putInt(KEY_THROTTLE_BPS, bps).putBoolean(KEY_THROTTLE_ON, enabled).apply()
        },
    )
    private val transport: Transport = throttled

    /** Non-null only when the active transport has an address to configure. */
    val link: LinkControl? = udp

    /** Always available: the throttle wraps every transport. */
    val throttle: ThrottleControl = throttled

    val receiver = ReceiverViewModel(application, transport, nativeEngine)

    // The listener's language: the peer's, from HELLO; in the loopback, this phone's receiver.
    val transmitter = TransmitterViewModel(
        application, transport, nativeEngine,
        listenerLanguage = { peerLanguage ?: receiver.language.value },
    )

    init {
        // The receiver's language is what this phone announces in HELLO.
        if (udp != null) {
            viewModelScope.launch {
                receiver.language.collect {
                    udp.announcedLanguage = it
                    udp.announceNow()
                }
            }
        }
    }

    /**
     * Phase 12 HELLO (`packet §6.7`, `context §18.1`): every keepalive carries this
     * phone's nonce. When the peer's nonce is new, both phones derive the session
     * from the provisioned PSK and the two nonces ordered by role — the lower node
     * id is the initiator — and each sends in its role's direction.
     */
    private fun startPairing(application: Application, udp: UdpTransport, engine: NativeEngine) {
        val psk = readPsk(application)
        if (psk == null) {
            Log.e(TAG, "no PSK provisioned (files/$PSK_FILE, 64 hex digits): no session can be established")
            return
        }
        // C-34 (packet §8.3, context §18.1): versions and codebook are compared at
        // HELLO; a mismatch fails pairing visibly and no session key is derived, so
        // no application packet can be sealed for, or authenticated from, that peer.
        val local = compatibilityOverride ?: engine.compatibility()
        udp.compatibility = local
        udp.sessionNonce = sessionNonce
        udp.onHello { node, language, nonce, compatibility ->
            if (language != null) peerLanguage = language
            if (nonce == null) return@onHello
            if (compatibility == null || !compatibility.contentEquals(local)) {
                val reason = describeMismatch(local, compatibility)
                if (_pairingError.value != reason) {
                    Log.e(TAG, "pairing refused with node ${"%04X".format(node.toInt() and 0xFFFF)}: $reason")
                }
                _pairingError.value = reason
                return@onHello
            }
            _pairingError.value = null
            if (nonce.contentEquals(peerNonce)) return@onHello
            peerNonce = nonce
            val initiator = (udp.nodeId.toInt() and 0xFFFF) < (node.toInt() and 0xFFFF)
            val initiatorNonce = if (initiator) sessionNonce else nonce
            val responderNonce = if (initiator) nonce else sessionNonce
            engine.beginSession(psk, initiatorNonce, responderNonce, initiator)
            sessionReady = true
            Log.i(TAG, "paired with node ${"%04X".format(node.toInt() and 0xFFFF)} as ${if (initiator) "initiator" else "responder"}, peer language $language")
            udp.announceNow()
        }
        udp.announceNow()
    }

    /** Test only (C-10, C-16, C-35): `{ sender context hash, receiver context hash }`; null without an engine. */
    internal fun contextHashes(): IntArray? = nativeEngine?.contextHashes()

    override fun onCleared() {
        transmitter.dispose()
        receiver.dispose()
        udp?.dispose()
        nativeEngine?.close()
    }

    private companion object {
        /**
         * true: UDP between two phones, with a session from HELLO and the
         * provisioned PSK (Phase 12). false: the in-app loopback of Phase 11.
         */
        const val USE_REAL_LINK = true
        const val KEY_THROTTLE_BPS = "throttle-bps"
        const val KEY_THROTTLE_ON = "throttle-on"

        /** PSK provisioned at pairing (`packet §6.7`): 64 hex digits in the app's files dir. */
        const val PSK_FILE = "itantra-psk.hex"

        val COMPATIBILITY_NAMES = listOf("packet format", "coder", "KDF", "cipher suite", "schema", "pack digest")

        fun describeMismatch(local: IntArray, peer: IntArray?): String =
            if (peer == null || peer.size != local.size) {
                "incompatible peer: no compatibility descriptor in HELLO"
            } else {
                "incompatible peer: " + local.indices.filter { local[it] != peer[it] }
                    .joinToString { "${COMPATIBILITY_NAMES[it]} ${peer[it]} (here ${local[it]})" }
            }

        fun readPsk(application: Application): ByteArray? {
            val hex = File(application.filesDir, PSK_FILE).takeIf { it.exists() }?.readText()?.trim() ?: return null
            if (hex.length != 64 || !hex.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return null
            return ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        }

        fun createNativeEngine(application: Application): NativeEngine? {
            if (!NativeBridge.isNativeLoaded()) {
                Log.e(TAG, "libitantra-native.so is not loaded: no native pipeline")
                return null
            }
            return try {
                val packs = NativeBridge.readPacks(application.assets)
                NativeBridge.createEngine(packs).also { engine ->
                    if (!USE_REAL_LINK) engine.beginLoopbackSession()
                    Log.i(TAG, "${NativeBridge.version()}; ${packs.size} pack files, languages ${engine.languages}")
                }
            } catch (e: Exception) {
                Log.e(TAG, "native engine failed to load", e)
                null
            }
        }
    }
}
