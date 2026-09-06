package com.itantra.app.receiver

import com.itantra.app.packet.ITantraPacket

/** Direct port of src/core/receiver/types.ts. Lifecycle of one received message through the TTS pipeline. */
enum class ReceivedMessageState(val value: String) {
    RECEIVED("received"),
    QUEUED("queued"),
    SPEAKING("speaking"),
    SPOKEN("spoken"),
    ERROR("error"),
}

data class ReceivedMessage(
    val packet: ITantraPacket,
    val state: ReceivedMessageState,
    /** Set when [state] is ERROR — a short, user-facing message. */
    val error: String?,
    val receivedAt: Long,
)
