package com.itantra.app.viewmodel

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import com.itantra.app.transport.LORA_SF12_BPS
import com.itantra.app.transport.LinkControl
import com.itantra.app.transport.MockTransport
import com.itantra.app.transport.ThrottleControl
import com.itantra.app.transport.ThrottledTransport
import com.itantra.app.transport.Transport
import com.itantra.app.transport.UdpTransport

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

    val transmitter = TransmitterViewModel(application, transport)
    val receiver = ReceiverViewModel(application, transport)

    override fun onCleared() {
        transmitter.dispose()
        receiver.dispose()
        udp?.dispose()
    }

    private companion object {
        const val USE_REAL_LINK = true
        const val KEY_THROTTLE_BPS = "throttle-bps"
        const val KEY_THROTTLE_ON = "throttle-on"
    }
}
