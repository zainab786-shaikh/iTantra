package com.itantra.app.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import com.itantra.app.transport.MockTransport

/**
 * Direct port of App.tsx's root-component ownership: one MockTransport
 * instance shared by both the transmitter and the receiver, and both
 * controllers constructed unconditionally regardless of which screen is
 * currently shown.
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
 */
class AppViewModel(application: Application) : AndroidViewModel(application) {
    private val transport = MockTransport()

    val transmitter = TransmitterViewModel(application, transport)
    val receiver = ReceiverViewModel(application, transport)

    override fun onCleared() {
        transmitter.dispose()
        receiver.dispose()
    }
}
