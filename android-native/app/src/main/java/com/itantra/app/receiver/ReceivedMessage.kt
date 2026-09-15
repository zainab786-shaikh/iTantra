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
 * `status` — as the native receive pipeline returned it (Phase 11). `status`
 * other than ok shows as [ReceivedMessageState.ERROR] with [error].
 *
 * `unresolved[]` brings a hard contract with it (§7.1): a slot listed there
 * must never be spoken, displayed or defaulted. Only its name is held here;
 * no value for it exists anywhere in this object, and [text] is empty.
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
    /**
     * 1 or 2 — which tier delivered (`receiver §7.3`: surfaced so the operator
     * knows whether this is their own language or the sender's); 0 when unknown.
     */
    val tier: Int = 0,
    /** Names of the slots in `unresolved[]` (`receiver §7.1`, C-31). Never values. */
    val unresolved: List<String> = emptyList(),
)
