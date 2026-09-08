package com.itantra.app.packet

import com.itantra.app.codec.CodecMode

/**
 * Direct port of PacketPriority in src/core/types.ts. Priority band
 * carried on the wire. Higher bands may pre-empt lower ones downstream
 * (see tts/TtsQueue, Phase 8).
 *
 * The string value matches the TS union's literal exactly, so anything
 * that needs the wire-format string (logging, future serialization) gets
 * the same text the RN app used, not an enum name that happens to look
 * similar.
 */
enum class PacketPriority(val value: String) {
    NORMAL("NORMAL"),
    MEDIUM("MEDIUM"),
    HIGH("HIGH"),
    CRITICAL("CRITICAL"),
}

/**
 * One transmission, as it actually crosses the link.
 *
 * ## The packet carries bytes, not text
 *
 * [payload] replaced what used to be a plain `text: String`. That is the
 * central change of this prototype and it is deliberate: the packet is the
 * transmitted representation, so if it also carried the plaintext it claims
 * to have compressed away, every byte count shown on screen would be
 * decoration rather than measurement.
 *
 * The plaintext still exists — it simply lives where it honestly belongs:
 *
 * - on the sending side, in `core.LogEntry.text`, because the transmitter
 *   knows what was said;
 * - on the receiving side, in `receiver.ReceivedMessage.text`, produced by
 *   decoding this payload and nothing else.
 *
 * ## [mode] is authoritative
 *
 * The decoder cannot start until it knows how the payload was encoded, so
 * the mode lives here, in the header, and is never repeated inside the
 * payload.
 *
 * ## [language] is the sender's
 *
 * Unchanged in meaning. For RAW it is what selects the receiver's TTS voice;
 * for PACK7 the payload's own langId serves that purpose; for PHRASE neither
 * is used on playback, because the receiver renders the phrase in its own
 * language. The codec returns which language applies, so no call site has to
 * work this out (see `codec.DecodedText`).
 */
class ITantraPacket(
    /** Unique per sender. Locally a UUID; reconstructed as `NODE-SEQ` on arrival. */
    val id: String,
    /** Device/User unique ID, for display. */
    val senderId: String,
    val timestamp: Long,
    /** The SENDER's language. */
    val language: String,
    /** The encoded utterance. Mode-specific; see `codec.ITantraCodec`. */
    val payload: ByteArray,
    val mode: CodecMode,
    /** UTF-8 size of the source text, so the receiver can show what was saved. */
    val originalBytes: Int,
    val priority: PacketPriority,
) {
    /**
     * True when the payload is genuinely smaller than the source text.
     *
     * Previously a hand-set flag that the codebase documented as never
     * actually being set. It is now derived from what the codec really did,
     * so it can no longer disagree with the payload.
     */
    val isCompressed: Boolean get() = mode != CodecMode.RAW

    /**
     * Content-based, unlike the array identity a `data class` would have
     * generated. Nothing today compares packets — the receiver dedups by
     * [id] — but a `Set<ITantraPacket>` that silently kept duplicates would
     * be a miserable thing to debug later.
     */
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is ITantraPacket) return false
        return id == other.id &&
            senderId == other.senderId &&
            timestamp == other.timestamp &&
            language == other.language &&
            payload.contentEquals(other.payload) &&
            mode == other.mode &&
            originalBytes == other.originalBytes &&
            priority == other.priority
    }

    override fun hashCode(): Int {
        var result = id.hashCode()
        result = 31 * result + senderId.hashCode()
        result = 31 * result + timestamp.hashCode()
        result = 31 * result + language.hashCode()
        result = 31 * result + payload.contentHashCode()
        result = 31 * result + mode.hashCode()
        result = 31 * result + originalBytes
        result = 31 * result + priority.hashCode()
        return result
    }

    override fun toString(): String =
        "ITantraPacket($id, $senderId, $language, ${mode.name}, " +
            "$originalBytes B -> ${payload.size} B, ${priority.value})"
}
