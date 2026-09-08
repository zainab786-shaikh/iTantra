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
    /** The reconstructed text, produced by decoding [packet]'s payload and nothing else. */
    val text: String,
    /**
     * Which language [text] is actually in.
     *
     * Not always [packet]'s language: a PHRASE message arrives as an id and
     * is rendered in the receiver's own language. This is what selects the
     * TTS voice, and what the language chip must show.
     */
    val textLanguage: String,
    val state: ReceivedMessageState,
    /** Set when [state] is ERROR — a short, user-facing message. */
    val error: String?,
    val receivedAt: Long,
)
