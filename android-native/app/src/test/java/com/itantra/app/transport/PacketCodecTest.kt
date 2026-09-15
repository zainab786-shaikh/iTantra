package com.itantra.app.transport

import com.itantra.app.config.LANGUAGES
import com.itantra.app.packet.ITantraPacket
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The frame is what actually crosses the link, so a mistake here is not a
 * degraded result — it is a link that silently carries nothing.
 *
 * ## What Phase 0 changed here
 *
 * The header lost `mode`, `priority`, `language` and `originalBytes`
 * (`packet §1.3`), so every test that round-tripped one of those is either
 * deleted or inverted into a check that it is **not** on the wire.
 *
 * Two deletions worth naming, because they are not oversights:
 *
 * - *"every priority band survives the round trip"* — priority is now a 1-bit
 *   field inside the native payload. Its replacement is **C-26** (Phase 11).
 * - *"a compressed payload survives the wire and still decodes"* — this layer
 *   no longer decodes anything; the payload is opaque to Kotlin
 *   (`packet §1.1`). Its replacements are **C-05** and **C-06** (Phase 7).
 */
class PacketCodecTest {

    private val nodeId: Short = 0x3F1A

    /**
     * Builds a frame-ready packet. [payload] stands in for the native payload
     * and is deliberately opaque — this layer must not know what is in it.
     */
    private fun packet(payload: ByteArray): ITantraPacket = ITantraPacket(
        id = "local-id",
        senderId = "ITX-ABCDEF12",
        localTimestamp = 1_700_000_000_000L,
        payload = payload,
    )

    private fun packet(size: Int): ITantraPacket =
        packet(ByteArray(size) { (it * 31 + 7).toByte() })

    private fun roundTrip(source: ITantraPacket, seq: Int = 7): ITantraPacket {
        val frame = PacketCodec.serialize(source, nodeId, seq)
        assertNotNull("frame should serialize", frame)
        val decoded = PacketCodec.deserialize(frame!!, frame.size)
        assertTrue("frame should decode as data", decoded is PacketCodec.Frame.Data)
        return (decoded as PacketCodec.Frame.Data).packet
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------

    @Test
    fun `header is exactly 8 bytes`() {
        // Was 11. The three bytes are `mode`, `priority | langId` and
        // `originalBytes` leaving the wire (packet §1.3). On a ~11 B native
        // payload that is not a rounding error, and it is the figure M-04 and
        // M-33 are measured against.
        val p = packet(3)
        val frame = PacketCodec.serialize(p, nodeId, 0)!!
        assertEquals(8, PacketCodec.HEADER_BYTES)
        assertEquals(PacketCodec.HEADER_BYTES + p.payload.size, frame.size)
    }

    @Test
    fun `the payload survives the round trip byte for byte`() {
        // The only thing the data frame carries, and it crosses opaquely.
        val sent = packet(byteArrayOf(0x00, 0x7F, -0x80, -0x01, 0x2A))
        val received = roundTrip(sent)
        assertTrue(sent.payload.contentEquals(received.payload))
    }

    @Test
    fun `an empty payload round trips and is still a data frame`() {
        val received = roundTrip(packet(0))
        assertEquals(0, received.payload.size)
    }

    @Test
    fun `sender identity and sequence become the packet id`() {
        val decoded = roundTrip(packet(2), seq = 0x0042)
        assertEquals("3F1A-0042", decoded.id)
        assertEquals("NODE-3F1A", decoded.senderId)
    }

    @Test
    fun `sequence wraps within its 16 bits instead of corrupting the header`() {
        assertEquals("3F1A-0005", roundTrip(packet(2), seq = 0x1_0005).id)
    }

    @Test
    fun `node id can be read without decoding the whole frame`() {
        val frame = PacketCodec.serialize(packet(1), nodeId, 1)!!
        assertEquals(nodeId, PacketCodec.peekNodeId(frame, frame.size))
    }

    // -----------------------------------------------------------------------
    // packet §1.3 — no duplicated fields
    // -----------------------------------------------------------------------

    @Test
    fun `the frame carries no tier, priority, language or source size`() {
        // The structural half of §1.3: there is nowhere on the received packet
        // for a duplicate to live, so it cannot disagree with the payload.
        val fields = ITantraPacket::class.java.declaredFields.map { it.name }.toSet()
        for (forbidden in listOf("mode", "tier", "priority", "language", "originalBytes")) {
            assertTrue(
                "the outer frame must not carry '$forbidden' (packet §1.3)",
                forbidden !in fields,
            )
        }
    }

    @Test
    fun `two packets differing only in payload produce frames differing only in payload`() {
        // The header is a pure function of (nodeId, seq, payloadLen). Nothing
        // about the message's meaning leaks into it — which is also what
        // `packet §6.2` wants once the payload is encrypted: an observer must
        // not be able to read priority or language off the outside.
        val a = PacketCodec.serialize(packet(byteArrayOf(1, 2, 3)), nodeId, 9)!!
        val b = PacketCodec.serialize(packet(byteArrayOf(9, 8, 7)), nodeId, 9)!!
        assertEquals(a.size, b.size)
        for (i in 0 until PacketCodec.HEADER_BYTES) {
            assertEquals("header byte $i must not depend on payload content", a[i], b[i])
        }
    }

    // -----------------------------------------------------------------------
    // HELLO
    // -----------------------------------------------------------------------

    @Test
    fun `hello frames are recognised and carry no message`() {
        val frame = PacketCodec.serializeHello(nodeId, "hi-IN")
        val decoded = PacketCodec.deserialize(frame, frame.size)
        assertTrue("hello must not decode as data", decoded is PacketCodec.Frame.Hello)
        assertEquals(nodeId, (decoded as PacketCodec.Frame.Hello).nodeId)
    }

    @Test
    fun `hello announces the language for every registered language`() {
        // A keepalive is not a message, so §1.3 does not apply to it — and the
        // announcement is required: tier §11.3 and receiver §8.3 both depend on
        // the sender learning the receiver's language from HELLO, which is what
        // decides the cross-language context rule (C-35).
        for (language in LANGUAGES) {
            val frame = PacketCodec.serializeHello(nodeId, language.code)
            val decoded = PacketCodec.deserialize(frame, frame.size)
            assertTrue(decoded is PacketCodec.Frame.Hello)
            assertEquals(
                "language for ${language.code}",
                language.code,
                (decoded as PacketCodec.Frame.Hello).languageCode,
            )
        }
    }

    @Test
    fun `a language id this build does not know is reported as unknown, not guessed`() {
        val frame = PacketCodec.serializeHello(nodeId, "en-IN")
        frame[PacketCodec.HEADER_BYTES] = 63
        val decoded = PacketCodec.deserialize(frame, frame.size)
        assertTrue(decoded is PacketCodec.Frame.Hello)
        assertNull((decoded as PacketCodec.Frame.Hello).languageCode)
    }

    @Test
    fun `hello is not mistaken for a zero length message`() {
        val hello = PacketCodec.serializeHello(nodeId, "en-IN")
        val empty = PacketCodec.serialize(packet(0), nodeId, 0)!!
        assertTrue(PacketCodec.deserialize(hello, hello.size) is PacketCodec.Frame.Hello)
        assertTrue(PacketCodec.deserialize(empty, empty.size) is PacketCodec.Frame.Data)
    }

    // -----------------------------------------------------------------------
    // Rejection
    // -----------------------------------------------------------------------

    @Test
    fun `foreign and malformed traffic is rejected rather than guessed at`() {
        val good = PacketCodec.serialize(packet(6), nodeId, 3)!!

        assertNull("empty datagram", PacketCodec.deserialize(ByteArray(0), 0))
        assertNull("shorter than a header", PacketCodec.deserialize(good, 4))
        assertNull(
            "wrong magic",
            PacketCodec.deserialize(good.copyOf().also { it[0] = 0x5A }, good.size),
        )
        assertNull(
            "wrong version",
            PacketCodec.deserialize(good.copyOf().also { it[1] = 0x70 }, good.size),
        )
        assertNull(
            "truncated payload",
            PacketCodec.deserialize(good, good.size - 1),
        )
        assertNull(
            // A newer build's frame kind must be dropped, not misread as data
            // and handed to the native parser as a payload it never wrote.
            "a frame kind this build does not know",
            PacketCodec.deserialize(
                good.copyOf().also { it[1] = ((it[1].toInt() and 0xF0) or 7).toByte() },
                good.size,
            ),
        )
    }

    @Test
    fun `an oversized message is refused rather than truncated`() {
        assertNull(PacketCodec.serialize(packet(PacketCodec.MAX_FRAME_BYTES + 1), nodeId, 0))
    }
}
