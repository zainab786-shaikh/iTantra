package com.itantra.app.viewmodel

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import com.itantra.app.native.NativeBridge
import com.itantra.app.native.NativeEngine
import com.itantra.app.transport.LORA_SF12_BPS
import com.itantra.app.transport.LinkControl
import com.itantra.app.transport.MockTransport
import com.itantra.app.transport.ThrottleControl
import com.itantra.app.transport.ThrottledTransport
import com.itantra.app.transport.Transport
import com.itantra.app.transport.UdpTransport

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
class AppViewModel(application: Application) : AndroidViewModel(application) {

    /**
     * The phone's native engine (Phase 11): packs, contexts, counter and keys.
     * One instance serves both screens, because in the single-device loopback
     * this phone's receiver authenticates this phone's own stream.
     *
     * Null only when libitantra-native.so or its packs failed to load; both
     * view models then say so instead of sending or decoding anything.
     */
    private val nativeEngine: NativeEngine? = createNativeEngine(application)

    /**
     * Flip to false to fall back to the in-app loopback.
     *
     * [MockTransport] is kept rather than deleted precisely for this: if the
     * wireless link will not come up on the day, everything above the
     * transport — codec, byte accounting, priority interrupt, TTS — still
     * demonstrates end-to-end on a single device, because nothing above this
     * line knows which transport it is talking to.
     */
    private val useRealLink = USE_REAL_LINK

    private val udp: UdpTransport? = if (useRealLink) UdpTransport(application) else null
    private val base: Transport = udp ?: MockTransport()

    /**
     * The simulated link rate wraps whatever the real transport is.
     *
     * Both view models receive this outer instance and neither knows it
     * exists - the whole point of the decorator. Turning the throttle off
     * removes the delay from the path without changing the chain.
     */
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

    // In the loopback the listener is this phone's own receiver, so its language
    // setting is the one the sender's cross-language rule must use (`tier §11.3`).
    val transmitter = TransmitterViewModel(application, transport, nativeEngine, listenerLanguage = { receiver.language.value })

    override fun onCleared() {
        transmitter.dispose()
        receiver.dispose()
        udp?.dispose()
        nativeEngine?.close()
    }

    private companion object {
        /**
         * false: the in-app loopback, for Phase 11.
         *
         * The native session is keyed per launch from this phone's own
         * randomness (NativeEngine.beginLoopbackSession). PSK provisioning and
         * HELLO — which let two phones derive the same session key
         * (`packet §6.7`, `context §18.1`) — are Phase 12. Until then a second
         * phone cannot authenticate this phone's payloads and its receiver
         * discards them, as it must (`receiver §3②`). Phase 11's exit criterion
         * is speech → speech on a single device, so the loopback is the link.
         * Set back to true once HELLO exists.
         */
        const val USE_REAL_LINK = false
        const val KEY_THROTTLE_BPS = "throttle-bps"
        const val KEY_THROTTLE_ON = "throttle-on"

        fun createNativeEngine(application: Application): NativeEngine? {
            if (!NativeBridge.isNativeLoaded()) {
                Log.e(TAG, "libitantra-native.so is not loaded: no native pipeline")
                return null
            }
            return try {
                val packs = NativeBridge.readPacks(application.assets)
                NativeBridge.createEngine(packs).also { engine ->
                    engine.beginLoopbackSession()
                    Log.i(
                        TAG,
                        "${NativeBridge.version()}; ${packs.size} pack files, languages ${engine.languages}; " +
                            "loopback session started",
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "native engine failed to load", e)
                null
            }
        }
    }
}
