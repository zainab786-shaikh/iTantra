package com.itantra.app.packet

import com.itantra.app.codec.TextCodec
import java.util.UUID

/**
 * A packet, together with the sender-side facts that do not travel with it.
 *
 * [text] is what the operator actually said. It stays on this device — the
 * packet carries only the encoded payload — and exists here so the
 * transmitter's own log can show what was sent alongside what it cost.
 */
class BuiltPacket(
    val packet: ITantraPacket,
    val text: String,
    /**
     * Whether decoding this packet's own payload reproduces [text] exactly.
     *
     * The codec already refuses to select a mode that does not round-trip, so
     * this should always be true. It is computed and displayed anyway: the
     * demo claims lossless reconstruction out loud, and a claim that is
     * asserted by the running code is worth more than one asserted by a
     * comment. A false here means something is genuinely wrong.
     */
    val roundTripOk: Boolean,
)

/**
 * Build a wire packet from a finalized utterance.
 *
 * Priority is still classified from the **plaintext, before encoding** — the
 * classifier reads words, and the payload no longer contains any.
 */
fun buildPacket(
    text: String,
    language: String,
    senderId: String,
    codec: TextCodec,
    /** Override the keyword-derived band. */
    priority: PacketPriority? = null,
): BuiltPacket {
    val encoded = codec.encode(text, language)

    val packet = ITantraPacket(
        id = UUID.randomUUID().toString(),
        senderId = senderId,
        timestamp = System.currentTimeMillis(),
        language = language,
        payload = encoded.bytes,
        mode = encoded.mode,
        originalBytes = encoded.originalBytes,
        priority = priority ?: classifyPriority(text),
    )

    val roundTripOk = try {
        codec.decode(encoded.mode, encoded.bytes, language, language).text == text
    } catch (e: Exception) {
        false
    }

    return BuiltPacket(packet = packet, text = text, roundTripOk = roundTripOk)
}
