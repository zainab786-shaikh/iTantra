package com.itantra.app.receiver

import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority

/** Direct port of src/core/receiver/types.ts. Lifecycle of one received message through the TTS pipeline. */
enum class ReceivedMessageState(val value: String) {
    RECEIVED("received"),
    QUEUED("queued"),
    SPEAKING("speaking"),
    SPOKEN("spoken"),
    ERROR("error"),
}

/**
 * One received message.
 *
 * This is the Kotlin side of what `receiver-pipeline-spec.md` §7 calls the
 * output interface — `text`, `language id`, `mode`, `priority`, `unresolved[]`,
 * `status`. Phase 0 carries the first four in the shape below; `mode` (the
 * tier) and `unresolved[]` arrive with the native receive pipeline in
 * Phases 10-11, and `unresolved[]` brings a hard contract with it (§7.1): a
 * slot listed there must never be spoken, displayed or defaulted.
 */
data class ReceivedMessage(
    val packet: ITantraPacket,
    /** The reconstructed text, produced by decoding [packet]'s payload and nothing else. */
    val text: String,
    /**
     * Which language [text] is actually in, and what selects the TTS voice.
     *
     * **Not the receiver's setting**, and not readable from the outer frame —
     * it comes out of the decoded native payload. `receiver §7.2` fixes it per
     * tier: Tier 1 renders in the *receiver's* language, Tier 2 in the
     * *sender's*. A Tier 2 message from a Marathi speaker must be spoken with
     * a Marathi voice even on a Gujarati-configured phone.
     */
    val textLanguage: String,
    /**
     * `NORMAL` or `CRITICAL` — there is no third state (`receiver §7.4`).
     *
     * Read from the native payload's 1-bit metadata field, not from the outer
     * frame, which no longer carries it (`packet §1.3`).
     */
    val priority: PacketPriority,
    val state: ReceivedMessageState,
    /** Set when [state] is ERROR — a short, user-facing message. */
    val error: String?,
    val receivedAt: Long,
)
