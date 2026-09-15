package com.itantra.app.transport

import com.itantra.app.config.languageFromWireId
import com.itantra.app.config.languageWireId
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.ITantraPacket

/**
 * The over-the-air representation of an [ITantraPacket].
 *
 * ## Phase 0: four fields left this header
 *
 * `packet-security-transport-spec.md` §1.3 is the reason. The native payload
 * is authoritative for `tier`, `symbol_count`, `seq`, `hash_present`,
 * `priority`, `negation`, `language` and `context_hash`, and the outer frame
 * "must **not** carry copies of these". This header carried three of them —
 * `mode`, `priority`, `language` — plus `originalBytes`, a sender-side fact
 * the receiver has no use for. All four are gone.
 *
 * > Transmitting the same fact twice wastes bytes on packets measured in
 * > single digits, and creates two sources of truth that can disagree. (§1.3)
 *
 * Header 11 B → **8 B**. On a Tier 1 message whose complete native payload is
 * ~11 B (`packet §9`), that is not a rounding error, and it is the figure M-04
 * and M-33 will be measured against.
 *
 * ```
 * byte  0     magic 'I' (0x49)         drops stray traffic that lands on the port
 * byte  1     version:4 | mode:4       version = 1
 *                                      mode = 0 DATA | 15 HELLO
 * bytes 2-3   nodeId        u16 BE     sender identity (DeviceId.getNodeId)
 * bytes 4-5   msgSeq        u16 BE     per-sender counter; wraps; used for dedup
 * bytes 6-7   payloadLen    u16 BE
 * bytes 8+    payload                  DATA:  the opaque native payload
 *                                      HELLO: 1 byte, the announced language id
 * ```
 *
 * ## `mode` is now a frame kind, not a codec
 *
 * It used to select between RAW / PACK7 / PHRASE. Those are replaced by the
 * native tier system (`packet §1.4`, `tier §9.2`), and the tier lives inside
 * the payload where the decoder reads it with zero coder state (`packet §3.4`).
 * The field survives only to separate a data frame from a keepalive.
 *
 * ## Why HELLO still carries a language
 *
 * A keepalive is not a message, so §1.3 does not apply to it — and the language
 * announcement is actually *required*: `tier §11.3` and `receiver §8.3` both
 * depend on the sender knowing the receiver's language from HELLO in order to
 * decide the cross-language context rule (C-35). It rides in HELLO's own
 * payload rather than in the shared header, so data frames pay nothing for it.
 *
 * ## `msgSeq` is not the protocol `seq`
 *
 * This is the transport's own per-sender counter, used to rebuild a dedup key
 * (`nodeId-msgSeq`). The protocol's 8-bit `seq`, its wide local counter and the
 * nonce derived from it are inside the native payload (`packet §3.6`, §6.5) and
 * are none of this layer's business.
 *
 * Three fields the in-memory packet has are deliberately NOT transmitted:
 *
 * - **the UUID.** `id` is reconstructed on arrival as `nodeId-msgSeq`, which
 *   is unique per sender and is exactly what the receiver already dedups on.
 *   3 bytes instead of 16.
 * - **the timestamp.** `ITantraPacket.localTimestamp` is assigned locally at
 *   both ends and never crosses the link (§1.3). `packet §9` notes that "at
 *   these payload sizes an eight-byte timestamp would dominate everything this
 *   document optimises".
 * - **the sender string.** [DeviceId.getNodeId] is its 16-bit form; the
 *   receiver renders it back with [DeviceId.nodeLabel].
 *
 * No CRC, no FEC, no fragmentation, no sequence repair at this layer. Integrity
 * is the native payload's AEAD tag, which replaces the CRC outright
 * (`packet §6.2`); FEC is not implemented in the current phase because
 * `UdpTransport` provides none (`packet §7.4`). A frame that does not parse is
 * dropped, not repaired.
 */
object PacketCodec {

    const val HEADER_BYTES = 8

    /** Largest frame we will put on the wire — one datagram, never fragmented. */
    const val MAX_FRAME_BYTES = 1024

    private const val MAGIC: Byte = 0x49 // 'I'
    private const val VERSION = 1

    /** An ordinary message. Its payload is the opaque native payload. */
    const val MODE_DATA: Byte = 0

    /**
     * Not a message: a keepalive announcing "node N is here, at this address,
     * speaking language L".
     *
     * It exists because one end of the link may sit behind a NAT that only
     * holds a return path open while traffic flows — see [UdpTransport]. It
     * is a transport-level concern and never surfaces to the application.
     */
    const val MODE_HELLO: Byte = 15

    /** What a received datagram turned out to be. */
    sealed class Frame {
        /** A real message. [packet] is ready for the receive pipeline. */
        data class Data(val packet: ITantraPacket) : Frame()

        /**
         * A keepalive from [nodeId]. Updates link state; never reaches the app.
         *
         * [languageCode] is what the peer announced, or null if this build does
         * not know that id. Phase 11 feeds it to the cross-language context
         * rule (`tier §11.3`); Phase 0 only logs it.
         */
        data class Hello(
            val nodeId: Short,
            val languageCode: String?,
            val sessionNonce: ByteArray? = null,
            /** C-34 compatibility descriptor (NativeEngine.compatibility), when announced. */
            val compatibility: IntArray? = null,
        ) : Frame()
    }

    /**
     * Encode [packet] for transmission.
     *
     * [nodeId] and [seq] are supplied by the transport rather than read off
     * the packet: identity-on-the-wire and sequencing are properties of the
     * link, not of the utterance.
     *
     * @return the frame, or null if it would exceed [MAX_FRAME_BYTES].
     */
    fun serialize(packet: ITantraPacket, nodeId: Short, seq: Int): ByteArray? {
        val payload = packet.payload
        if (HEADER_BYTES + payload.size > MAX_FRAME_BYTES) return null

        val frame = ByteArray(HEADER_BYTES + payload.size)
        writeHeader(frame, MODE_DATA, nodeId, seq, payload.size)
        payload.copyInto(frame, HEADER_BYTES)
        return frame
    }

    /** HELLO nonce size (`packet §6.7`, crypto/kdf.h kHelloNonceBytes). */
    const val HELLO_NONCE_BYTES = 32

    /**
     * Build a keepalive frame for [nodeId], announcing [languageCode] and, from
     * Phase 12, this phone's session nonce: payload = language byte ‖ 32-byte nonce.
     */
    fun serializeHello(
        nodeId: Short,
        languageCode: String,
        sessionNonce: ByteArray? = null,
        /** C-34: [COMPATIBILITY_FIELDS] big-endian ints after the nonce. */
        compatibility: IntArray? = null,
    ): ByteArray {
        val nonce = sessionNonce ?: ByteArray(0)
        val descriptor = if (sessionNonce != null) compatibility else null
        val extra = (descriptor?.size ?: 0) * 4
        val frame = ByteArray(HEADER_BYTES + 1 + nonce.size + extra)
        writeHeader(frame, MODE_HELLO, nodeId, seq = 0, payloadLen = 1 + nonce.size + extra)
        frame[HEADER_BYTES] = languageWireId(languageCode).coerceIn(0, 63).toByte()
        nonce.copyInto(frame, HEADER_BYTES + 1)
        descriptor?.forEachIndexed { i, v ->
            val at = HEADER_BYTES + 1 + nonce.size + i * 4
            writeU16(frame, at, (v ushr 16) and 0xFFFF)
            writeU16(frame, at + 2, v and 0xFFFF)
        }
        return frame
    }

    /** Fields of the C-34 compatibility descriptor carried in HELLO. */
    const val COMPATIBILITY_FIELDS = 6

    /**
     * Decode [length] bytes of [buffer].
     *
     * @return the frame, or null if it is not one of ours or is malformed.
     *   Never throws: this runs inside the receive loop, where an exception
     *   would take the link down for the rest of the session.
     */
    fun deserialize(buffer: ByteArray, length: Int): Frame? {
        if (length < HEADER_BYTES) return null
        if (buffer[0] != MAGIC) return null

        val versionAndMode = buffer[1].toInt() and 0xFF
        if ((versionAndMode ushr 4) != VERSION) return null
        val mode = (versionAndMode and 0x0F).toByte()

        val nodeId = readU16(buffer, 2).toShort()
        val seq = readU16(buffer, 4)
        val payloadLen = readU16(buffer, 6)

        // A truncated datagram means the far end and this end disagree about
        // the frame, which is exactly the case where guessing is worst.
        if (HEADER_BYTES + payloadLen != length) return null

        if (mode == MODE_HELLO) {
            val language = if (payloadLen >= 1) {
                languageFromWireId(buffer[HEADER_BYTES].toInt() and 0x3F)?.code
            } else {
                null
            }
            val nonce = if (payloadLen >= 1 + HELLO_NONCE_BYTES) {
                buffer.copyOfRange(HEADER_BYTES + 1, HEADER_BYTES + 1 + HELLO_NONCE_BYTES)
            } else {
                null
            }
            val descriptorAt = HEADER_BYTES + 1 + HELLO_NONCE_BYTES
            val compatibility = if (payloadLen == 1 + HELLO_NONCE_BYTES + COMPATIBILITY_FIELDS * 4) {
                IntArray(COMPATIBILITY_FIELDS) { i ->
                    (readU16(buffer, descriptorAt + i * 4) shl 16) or readU16(buffer, descriptorAt + i * 4 + 2)
                }
            } else {
                null
            }
            return Frame.Hello(nodeId, language, nonce, compatibility)
        }

        // A mode this build does not know is dropped, not misread as data.
        if (mode != MODE_DATA) return null

        val payload = buffer.copyOfRange(HEADER_BYTES, HEADER_BYTES + payloadLen)

        // No tier, priority or language is read here, and none is guessed at.
        // They are inside the payload and belong to the native parser
        // (`packet §1.3`, `receiver §1.1`).
        val packet = ITantraPacket(
            id = frameId(nodeId, seq),
            senderId = DeviceId.nodeLabel(nodeId),
            localTimestamp = System.currentTimeMillis(),
            payload = payload,
        )
        return Frame.Data(packet)
    }

    /** The reconstructed packet id: unique per sender, and the receiver's dedup key. */
    fun frameId(nodeId: Short, seq: Int): String =
        "%04X-%04X".format(nodeId.toInt() and 0xFFFF, seq and 0xFFFF)

    /** The sending node's id, read straight out of a frame. Used to drop our own echoes. */
    fun peekNodeId(buffer: ByteArray, length: Int): Short? {
        if (length < HEADER_BYTES || buffer[0] != MAGIC) return null
        return readU16(buffer, 2).toShort()
    }

    private fun writeHeader(
        frame: ByteArray,
        mode: Byte,
        nodeId: Short,
        seq: Int,
        payloadLen: Int,
    ) {
        frame[0] = MAGIC
        frame[1] = ((VERSION shl 4) or (mode.toInt() and 0x0F)).toByte()
        writeU16(frame, 2, nodeId.toInt() and 0xFFFF)
        writeU16(frame, 4, seq and 0xFFFF)
        writeU16(frame, 6, payloadLen)
    }

    private fun writeU16(target: ByteArray, offset: Int, value: Int) {
        target[offset] = ((value ushr 8) and 0xFF).toByte()
        target[offset + 1] = (value and 0xFF).toByte()
    }

    private fun readU16(source: ByteArray, offset: Int): Int =
        ((source[offset].toInt() and 0xFF) shl 8) or (source[offset + 1].toInt() and 0xFF)
}
