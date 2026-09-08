package com.itantra.app.transport

import com.itantra.app.config.languageFromWireId
import com.itantra.app.config.languageWireId
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority

/**
 * The over-the-air representation of an [ITantraPacket].
 *
 * This is the byte layout that actually crosses the link, and it is
 * deliberately tiny. The point of the whole prototype is that a compressed
 * payload can be 2 bytes; wrapping that in a JSON envelope, or in the
 * packet's own in-memory field set (a 36-char UUID, an epoch-millisecond
 * timestamp, a 12-char sender string, a 5-char BCP-47 tag = 60+ bytes),
 * would make the payload size irrelevant and the demo's central claim
 * decorative.
 *
 * ```
 * byte  0     magic 'I' (0x49)         drops stray traffic that lands on the port
 * byte  1     version:4 | mode:4       version = 1
 *                                      mode = 0 RAW | 1 PACK7 | 2 PHRASE | 15 HELLO
 * byte  2     priority:2 | langId:6    the SENDER's language, as its LANGUAGES index
 * bytes 3-4   nodeId        u16 BE     sender identity (DeviceId.getNodeId)
 * bytes 5-6   msgSeq        u16 BE     per-sender counter; wraps; used for dedup
 * bytes 7-8   originalBytes u16 BE     UTF-8 size of the source text, for the readout
 * bytes 9-10  payloadLen    u16 BE
 * bytes 11+   payload
 * ```
 *
 * Three fields the in-memory packet has are deliberately NOT transmitted:
 *
 * - **the UUID.** `id` is reconstructed on arrival as `nodeId-msgSeq`, which
 *   is unique per sender and is exactly what the receiver already dedups on.
 *   3 bytes instead of 16.
 * - **the timestamp.** The receiver stamps arrival. On a live link the
 *   difference is milliseconds, and it saves 8 bytes.
 * - **the sender string.** [DeviceId.getNodeId] is its 16-bit form; the
 *   receiver renders it back with [DeviceId.nodeLabel], so the same identity
 *   reads the same on both ends.
 *
 * No CRC, no FEC, no fragmentation, no sequence repair — all explicitly
 * deferred (prototype-for-demo.md §11). A frame that does not parse is
 * dropped, not repaired.
 */
object PacketCodec {

    const val HEADER_BYTES = 11

    /** Largest frame we will put on the wire — one datagram, never fragmented. */
    const val MAX_FRAME_BYTES = 1024

    private const val MAGIC: Byte = 0x49 // 'I'
    private const val VERSION = 1

    /** Payload is UTF-8 text, uncompressed. */
    const val MODE_RAW: Byte = 0

    /**
     * Not a message: a keepalive announcing "node N is here, at this address".
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

        /** A keepalive from [nodeId]. Updates link state; never reaches the app. */
        data class Hello(val nodeId: Short) : Frame()
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
        // Level 1: the payload is the transcript as UTF-8 and the mode is RAW.
        // Level 2 replaces these two lines with the codec's output and the
        // mode it selected; nothing else in this file changes.
        val payload = packet.text.toByteArray(Charsets.UTF_8)
        val mode = MODE_RAW
        val originalBytes = payload.size

        if (HEADER_BYTES + payload.size > MAX_FRAME_BYTES) return null

        val frame = ByteArray(HEADER_BYTES + payload.size)
        writeHeader(
            frame = frame,
            mode = mode,
            priority = packet.priority,
            languageCode = packet.language,
            nodeId = nodeId,
            seq = seq,
            originalBytes = originalBytes,
            payloadLen = payload.size,
        )
        payload.copyInto(frame, HEADER_BYTES)
        return frame
    }

    /** Build a keepalive frame for [nodeId], announcing [languageCode]. */
    fun serializeHello(nodeId: Short, languageCode: String): ByteArray {
        val frame = ByteArray(HEADER_BYTES)
        writeHeader(
            frame = frame,
            mode = MODE_HELLO,
            priority = PacketPriority.NORMAL,
            languageCode = languageCode,
            nodeId = nodeId,
            seq = 0,
            originalBytes = 0,
            payloadLen = 0,
        )
        return frame
    }

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

        val priorityAndLang = buffer[2].toInt() and 0xFF
        val priorityIndex = priorityAndLang ushr 6
        val langId = priorityAndLang and 0x3F

        val nodeId = readU16(buffer, 3).toShort()
        val seq = readU16(buffer, 5)
        val originalBytes = readU16(buffer, 7)
        val payloadLen = readU16(buffer, 9)

        if (mode == MODE_HELLO) return Frame.Hello(nodeId)

        // A truncated datagram means the far end and this end disagree about
        // the frame, which is exactly the case where guessing is worst.
        if (HEADER_BYTES + payloadLen != length) return null

        val payload = buffer.copyOfRange(HEADER_BYTES, HEADER_BYTES + payloadLen)

        // An id this build does not know cannot be rendered, spoken, or even
        // labelled honestly — drop it rather than silently substituting a
        // language the sender did not use.
        val language = languageFromWireId(langId) ?: return null
        val priority = PacketPriority.entries.getOrNull(priorityIndex) ?: return null

        val packet = ITantraPacket(
            id = frameId(nodeId, seq),
            senderId = DeviceId.nodeLabel(nodeId),
            timestamp = System.currentTimeMillis(),
            language = language.code,
            text = String(payload, Charsets.UTF_8),
            priority = priority,
            isCompressed = mode != MODE_RAW,
        )
        return Frame.Data(packet)
    }

    /** The reconstructed packet id: unique per sender, and the receiver's dedup key. */
    fun frameId(nodeId: Short, seq: Int): String =
        "%04X-%04X".format(nodeId.toInt() and 0xFFFF, seq and 0xFFFF)

    /** The sending node's id, read straight out of a frame. Used to drop our own echoes. */
    fun peekNodeId(buffer: ByteArray, length: Int): Short? {
        if (length < HEADER_BYTES || buffer[0] != MAGIC) return null
        return readU16(buffer, 3).toShort()
    }

    private fun writeHeader(
        frame: ByteArray,
        mode: Byte,
        priority: PacketPriority,
        languageCode: String,
        nodeId: Short,
        seq: Int,
        originalBytes: Int,
        payloadLen: Int,
    ) {
        val langId = languageWireId(languageCode).coerceIn(0, 63)
        frame[0] = MAGIC
        frame[1] = ((VERSION shl 4) or (mode.toInt() and 0x0F)).toByte()
        frame[2] = ((priority.ordinal shl 6) or langId).toByte()
        writeU16(frame, 3, nodeId.toInt() and 0xFFFF)
        writeU16(frame, 5, seq and 0xFFFF)
        writeU16(frame, 7, originalBytes.coerceAtMost(0xFFFF))
        writeU16(frame, 9, payloadLen)
    }

    private fun writeU16(target: ByteArray, offset: Int, value: Int) {
        target[offset] = ((value ushr 8) and 0xFF).toByte()
        target[offset + 1] = (value and 0xFF).toByte()
    }

    private fun readU16(source: ByteArray, offset: Int): Int =
        ((source[offset].toInt() and 0xFF) shl 8) or (source[offset + 1].toInt() and 0xFF)
}
