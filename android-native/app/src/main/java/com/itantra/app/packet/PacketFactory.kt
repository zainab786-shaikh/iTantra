package com.itantra.app.packet

import java.util.UUID

/**
 * A packet, together with the sender-side facts that do not travel with it.
 *
 * This class grew in Phase 0 for the same reason [ITantraPacket] shrank.
 * `packet-security-transport-spec.md` §1.3 took `language`, `mode` and
 * `priority` off the wire, and `originalBytes` went with them — but the sender
 * still legitimately knows all of them, and the transmitter's own log and the
 * M-01/M-05 measurements still need them. They live here, on the sending
 * device, rather than being transmitted back to a receiver that will decode
 * them out of the native payload anyway.
 */
class BuiltPacket(
    val packet: ITantraPacket,
    /**
     * What the operator actually said. Stays on this device — the packet
     * carries only the encoded payload.
     */
    val text: String,
    /** UTF-8 size of [text]. This is M-01 (`contract §6.1`). Never transmitted. */
    val originalBytes: Int,
    /**
     * The message's priority.
     *
     * Sender-side here only because Phase 0 has no native payload yet. From
     * Phase 11 the authoritative copy is the 1-bit field inside the native
     * payload (`packet §3.1`), and this is the value that was written into it.
     */
    val priority: PacketPriority,
    /** The sender's configured language. Sender-side for the same reason. */
    val language: String,
)

/**
 * Build an outer frame around an already-encoded native payload.
 *
 * ## What changed in Phase 0
 *
 * This function used to call `TextCodec.encode` and pick between RAW, PACK7
 * and PHRASE. It no longer encodes anything. `packet §1.4` is explicit that
 * the new format "REPLACES the prototype's RAW / PACK7 / PHRASE payload
 * generation", and `tier §9.2` says the same. The payload now arrives already
 * assembled by the native layer and is opaque here (`packet §1.1`).
 *
 * The native encoder does not exist until Phase 3, and is not reachable from
 * Kotlin until Phase 11. Until then this function has no caller that can
 * supply a real [payload] — which is the expected, documented consequence of
 * Phase 0 (implementation plan, Phase 0 Notes: "Expect the app to be
 * temporarily non-functional end-to-end. That is correct — the payload
 * producer does not exist yet.").
 *
 * ## The round-trip self-check is gone
 *
 * It used to decode the payload back through the Kotlin codec and compare.
 * That check was only meaningful while the Kotlin codec was the producer. Left
 * in place it would decode a native payload with a codec that did not write
 * it, and report a failure that means nothing. Phase 11 can reinstate a real
 * one against the native decoder.
 */
fun buildPacket(
    text: String,
    language: String,
    senderId: String,
    /** The assembled native payload — `[ metadata ][ symbols ][ flush ][ pad ][ tag ]`. */
    payload: ByteArray,
    /** Override the keyword-derived priority. Never lowers it (`packet §11.1`). */
    priority: PacketPriority? = null,
): BuiltPacket {
    val packet = ITantraPacket(
        id = UUID.randomUUID().toString(),
        senderId = senderId,
        localTimestamp = System.currentTimeMillis(),
        payload = payload,
    )

    return BuiltPacket(
        packet = packet,
        text = text,
        originalBytes = text.toByteArray(Charsets.UTF_8).size,
        priority = priority ?: classifyPriority(text),
        language = language,
    )
}
