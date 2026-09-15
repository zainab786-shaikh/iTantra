package com.itantra.app.packet

import com.itantra.app.native.NativeEngine
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
     * What the operator actually said — for a native packet, this clause of it.
     * Stays on this device — the packet carries only the encoded payload.
     */
    val text: String,
    /** UTF-8 size of [text]. This is M-01 (`contract §6.1`). Never transmitted. */
    val originalBytes: Int,
    /**
     * The message's priority.
     *
     * From Phase 11 the authoritative copy is the 1-bit field inside the native
     * payload (`packet §3.1`); this is the value the native sender wrote into it.
     */
    val priority: PacketPriority,
    /** The sender's configured language. Sender-side for the same reason. */
    val language: String,
    /** What the native sender did with this clause. Null only for a frame built around a payload from elsewhere. */
    val native: NativeSendInfo? = null,
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
 * From Phase 11 its caller is [buildNativePackets], which supplies the payload
 * the native engine produced.
 *
 * ## The round-trip self-check is gone
 *
 * It used to decode the payload back through the Kotlin codec and compare.
 * The native sender now verifies its own payloads before releasing them —
 * Tier 1 by decoding its own frame, Tier 2 by re-reading its metadata
 * (`select/select.h`) — so there is nothing for this layer to re-check.
 */
fun buildPacket(
    text: String,
    language: String,
    senderId: String,
    /** The assembled native payload — `[ metadata ][ symbols ][ flush ][ pad ][ tag ]`. */
    payload: ByteArray,
    /** Override the keyword-derived priority. Never lowers it (`packet §11.1`). */
    priority: PacketPriority? = null,
    native: NativeSendInfo? = null,
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
        native = native,
    )
}

/**
 * What the native sender did with one clause (`native/src/api/engine.h`).
 * Sender-side only and never transmitted — the payload itself is authoritative
 * for tier and priority (`packet §1.3`).
 */
class NativeSendInfo(
    /** 1 or 2 (`packet §3.1`). */
    val tier: Int,
    /** Why Tier 1 was not safe (`tier §8.1`); `None` when it was. */
    val safetyTrigger: String,
    /** Plaintext native payload bytes — M-02, before the tag. */
    val plaintextBytes: Int,
    /** The complete native packets tier selection compared (C-19); 0 = that tier had none. */
    val tier1PacketBytes: Int,
    val tier2PacketBytes: Int,
    /** The wide counter behind the payload's `seq` and nonce (`packet §3.6`). */
    val counter: Long,
)

/** A clause the native sender produced no payload for. The operator must be told (`tier §8`). */
class NativeRefusal(val text: String, val outcome: String, val safetyTrigger: String)

class NativeSend(val packets: List<BuiltPacket>, val refused: List<NativeRefusal>)

/**
 * The Phase 11 payload producer: one utterance in, one outer frame per clause out.
 *
 * One JNI crossing for the whole utterance. The native layer segments it into
 * clauses (`context §7`), selects a tier per clause (`tier §8`), seals and commits
 * each (`packet §6`, `context §10`), and returns them in order. Clauses stay in
 * that order here, because the receiver's context commits in the same order.
 *
 * Priority is the native one — `is_alert` of a verified Tier 1 intent, or
 * [sendAsCritical] (`packet §11.1`) — which retires the keyword classifier on
 * this path.
 */
fun buildNativePackets(
    engine: NativeEngine,
    text: String,
    language: String,
    listenerLanguage: String,
    senderId: String,
    sttConfidence: Long,
    sendAsCritical: Boolean,
): NativeSend {
    val utf8 = text.toByteArray(Charsets.UTF_8)
    val clauses = engine.sendUtterance(utf8, language, listenerLanguage, sttConfidence, sendAsCritical)
    val packets = mutableListOf<BuiltPacket>()
    val refused = mutableListOf<NativeRefusal>()
    for (clause in clauses) {
        val clauseText = String(utf8, clause.textStart, clause.textEnd - clause.textStart, Charsets.UTF_8)
        if (!clause.sent) {
            refused += NativeRefusal(clauseText, clause.outcome, clause.trigger)
            continue
        }
        packets += buildPacket(
            text = clauseText,
            language = language,
            senderId = senderId,
            payload = clause.payload,
            priority = if (clause.priority == 1) PacketPriority.CRITICAL else PacketPriority.NORMAL,
            native = NativeSendInfo(
                tier = clause.tier,
                safetyTrigger = clause.trigger,
                plaintextBytes = clause.plaintextBytes,
                tier1PacketBytes = clause.tier1PacketBytes,
                tier2PacketBytes = clause.tier2PacketBytes,
                counter = clause.counter,
            ),
        )
    }
    return NativeSend(packets, refused)
}
