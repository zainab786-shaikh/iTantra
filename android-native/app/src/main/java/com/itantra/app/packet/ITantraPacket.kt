package com.itantra.app.packet

/**
 * Message priority. **Two states, and only two.**
 *
 * `HIGH` and `MEDIUM` used to exist here and no longer do. Four specifications
 * say the same thing in nearly the same words — `packet-security-transport-spec.md`
 * §11, `receiver-pipeline-spec.md` §7.4, `language-layer-spec.md` §11.2 and
 * `tier-1-2-spec.md` register #22:
 *
 * > `HIGH` does not exist — not on the wire, not in the API, not in the UI.
 *
 * The wire field is one bit (`packet §3.1`), so an enum with four entries could
 * only ever have encoded two of them. Keeping the other two would have meant the
 * UI rendering bands that cannot arrive.
 *
 * Not to be confused with `rule_priority`, the Tier 1 rule table's
 * evaluation-order field, which never leaves the sender (`tier §5.4`).
 */
enum class PacketPriority(val value: String) {
    NORMAL("NORMAL"),
    CRITICAL("CRITICAL"),
}

/**
 * The outer frame — Kotlin's, and deliberately almost empty.
 *
 * ## What this class is now
 *
 * `packet-security-transport-spec.md` §1.1 draws the layering boundary:
 *
 * ```
 * KOTLIN   ITantraPacket outer frame          this class
 *            contains
 * C++      native payload                     authoritative for everything
 *            carried by                       the decoder needs
 * KOTLIN   UdpTransport / ThrottledTransport  unchanged
 * ```
 *
 * ## Why four fields left
 *
 * §1.3 is explicit: the native payload is authoritative for `tier`,
 * `symbol_count`, `seq`, `hash_present`, `priority`, `negation`, `language` and
 * `context_hash`, and the outer frame "must **not** carry copies of these".
 * So `language`, `mode` and `priority` are gone from here, and `text` was
 * already gone.
 *
 * `originalBytes` went with them. §1.3 does not name it, but it is a derived
 * sender-side fact and §1.3's own rationale applies to it unchanged —
 * transmitting the same fact twice wastes bytes on packets measured in single
 * digits. It is still measured; it just stays on the sender, in [BuiltPacket],
 * where M-01 and the airtime race read it.
 *
 * What remains is exactly what §1.3 permits: `id` and `senderId` for dedup and
 * logging, and a receive timestamp assigned locally.
 *
 * ## [localTimestamp] is never transmitted
 *
 * Renamed from `timestamp` so the name states the contract. §1.3 calls it "a
 * locally-assigned receive timestamp that is never transmitted", and §9 warns
 * that "at these payload sizes an eight-byte timestamp would dominate
 * everything this document optimises". [PacketCodec] does not serialise it.
 */
class ITantraPacket(
    /** Unique per sender. Locally a UUID; reconstructed as `NODE-SEQ` on arrival. */
    val id: String,
    /** Device/User unique ID, for display. */
    val senderId: String,
    /**
     * Assigned locally — on send, when built; on receive, when the datagram
     * arrived. **Never crosses the link.** See the class doc.
     */
    val localTimestamp: Long,
    /**
     * The native payload: `[ metadata ][ symbols ][ flush ][ pad ][ AEAD tag ]`.
     *
     * Opaque to this layer by design (`packet §1.1`). Nothing in Kotlin may
     * parse it — doing so would recreate the second source of truth §1.3
     * removes.
     */
    val payload: ByteArray,
) {
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
            localTimestamp == other.localTimestamp &&
            payload.contentEquals(other.payload)
    }

    override fun hashCode(): Int {
        var result = id.hashCode()
        result = 31 * result + senderId.hashCode()
        result = 31 * result + localTimestamp.hashCode()
        result = 31 * result + payload.contentHashCode()
        return result
    }

    override fun toString(): String =
        "ITantraPacket($id, $senderId, ${payload.size} B)"
}
