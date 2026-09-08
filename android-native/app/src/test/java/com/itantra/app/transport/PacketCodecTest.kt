package com.itantra.app.transport

import com.itantra.app.config.LANGUAGES
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The frame is what actually crosses the link, so a mistake here is not a
 * degraded result — it is text in the wrong script, spoken in the wrong
 * voice, or a link that silently carries nothing.
 */
class PacketCodecTest {

    private val nodeId: Short = 0x3F1A

    private fun packet(
        text: String,
        language: String = "en-IN",
        priority: PacketPriority = PacketPriority.NORMAL,
    ) = ITantraPacket(
        id = "local-id",
        senderId = "ITX-ABCDEF12",
        timestamp = 1_700_000_000_000L,
        language = language,
        text = text,
        priority = priority,
        isCompressed = false,
    )

    private fun roundTrip(source: ITantraPacket, seq: Int = 7): ITantraPacket {
        val frame = PacketCodec.serialize(source, nodeId, seq)
        assertNotNull("frame should serialize", frame)
        val decoded = PacketCodec.deserialize(frame!!, frame.size)
        assertTrue("frame should decode as data", decoded is PacketCodec.Frame.Data)
        return (decoded as PacketCodec.Frame.Data).packet
    }

    @Test
    fun `header is exactly 11 bytes`() {
        val frame = PacketCodec.serialize(packet("hi"), nodeId, 0)!!
        assertEquals(PacketCodec.HEADER_BYTES + 2, frame.size)
        assertEquals(11, PacketCodec.HEADER_BYTES)
    }

    @Test
    fun `text survives the round trip`() {
        assertEquals("send help immediately", roundTrip(packet("send help immediately")).text)
    }

    @Test
    fun `every language survives the round trip in its own script`() {
        // Indic text is where a byte-level mistake shows up as mojibake rather
        // than as an obvious failure, so each language gets a real sample.
        val samples = mapOf(
            "en-IN" to "fire at north gate",
            "hi-IN" to "उत्तर द्वार पर आग लग गई है",
            "mr-IN" to "उत्तर दरवाजाला आग लागली आहे",
            "gu-IN" to "ઉત્તર દરવાજા પર આગ લાગી છે",
            "kn-IN" to "ಉತ್ತರ ದ್ವಾರದಲ್ಲಿ ಬೆಂಕಿ",
            "ml-IN" to "വടക്കേ ഗേറ്റിൽ തീ",
            "ta-IN" to "வடக்கு வாசலில் தீ",
            "te-IN" to "ఉత్తర ద్వారం వద్ద మంటలు",
            "or-IN" to "ଉତ୍ତର ଦ୍ୱାରରେ ନିଆଁ",
            "bn-IN" to "উত্তর গেটে আগুন",
        )
        assertEquals("every registered language needs a sample", LANGUAGES.size, samples.size)

        for ((code, text) in samples) {
            val decoded = roundTrip(packet(text, language = code))
            assertEquals("text for $code", text, decoded.text)
            assertEquals("language for $code", code, decoded.language)
        }
    }

    @Test
    fun `every priority band survives the round trip`() {
        for (priority in PacketPriority.entries) {
            assertEquals(priority, roundTrip(packet("status", priority = priority)).priority)
        }
    }

    @Test
    fun `sender identity and sequence become the packet id`() {
        val decoded = roundTrip(packet("report"), seq = 0x0042)
        assertEquals("3F1A-0042", decoded.id)
        assertEquals("NODE-3F1A", decoded.senderId)
    }

    @Test
    fun `hello frames are recognised and carry no message`() {
        val frame = PacketCodec.serializeHello(nodeId, "hi-IN")
        assertEquals(PacketCodec.HEADER_BYTES, frame.size)
        val decoded = PacketCodec.deserialize(frame, frame.size)
        assertTrue("hello must not decode as data", decoded is PacketCodec.Frame.Hello)
        assertEquals(nodeId, (decoded as PacketCodec.Frame.Hello).nodeId)
    }

    @Test
    fun `node id can be read without decoding the whole frame`() {
        val frame = PacketCodec.serialize(packet("x"), nodeId, 1)!!
        assertEquals(nodeId, PacketCodec.peekNodeId(frame, frame.size))
    }

    @Test
    fun `foreign and malformed traffic is rejected rather than guessed at`() {
        val good = PacketCodec.serialize(packet("area clear"), nodeId, 3)!!

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
            "language id this build does not know",
            PacketCodec.deserialize(
                good.copyOf().also { it[2] = ((it[2].toInt() and 0xC0) or 63).toByte() },
                good.size,
            ),
        )
    }

    @Test
    fun `an oversized message is refused rather than truncated`() {
        val huge = packet("x".repeat(PacketCodec.MAX_FRAME_BYTES + 1))
        assertNull(PacketCodec.serialize(huge, nodeId, 0))
    }

    @Test
    fun `sequence wraps within its 16 bits instead of corrupting the header`() {
        val decoded = roundTrip(packet("wrap"), seq = 0x1_0005)
        assertEquals("3F1A-0005", decoded.id)
    }
}
