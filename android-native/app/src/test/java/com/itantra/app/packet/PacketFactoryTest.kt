package com.itantra.app.packet

import com.itantra.app.codec.ITantraCodec
import com.itantra.app.codec.PhraseDictionary
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The manual CRITICAL override, and the guarantee that it does not disturb
 * the automatic classification it sits on top of.
 */
class PacketFactoryTest {

    private val codec = ITantraCodec()

    private fun build(text: String, override: PacketPriority? = null) = buildPacket(
        text = text,
        language = "en-IN",
        senderId = "ITX-TEST0001",
        codec = codec,
        priority = override,
    )

    // -----------------------------------------------------------------------
    // Automatic classification — must be unchanged
    // -----------------------------------------------------------------------

    @Test
    fun `without an override the keyword classifier still decides`() {
        assertEquals(PacketPriority.CRITICAL, build("there is a fire at the gate").packet.priority)
        assertEquals(PacketPriority.HIGH, build("send help immediately").packet.priority)
        assertEquals(PacketPriority.MEDIUM, build("report status").packet.priority)
        assertEquals(PacketPriority.NORMAL, build("all is quiet here").packet.priority)
    }

    @Test
    fun `normal traffic is untouched by the feature existing`() {
        val packet = build("all is quiet here").packet
        assertEquals(PacketPriority.NORMAL, packet.priority)
    }

    // -----------------------------------------------------------------------
    // Manual override
    // -----------------------------------------------------------------------

    @Test
    fun `the override raises a message the classifier would have called NORMAL`() {
        // The whole point: an operator can flag something urgent that happens
        // to contain none of the trigger words.
        val text = "all is quiet here"
        assertEquals(PacketPriority.NORMAL, build(text).packet.priority)
        assertEquals(
            PacketPriority.CRITICAL,
            build(text, PacketPriority.CRITICAL).packet.priority,
        )
    }

    @Test
    fun `the override does not disturb a message already classified CRITICAL`() {
        val text = "there is a fire at the gate"
        assertEquals(
            PacketPriority.CRITICAL,
            build(text, PacketPriority.CRITICAL).packet.priority,
        )
    }

    @Test
    fun `an overridden packet is otherwise identical - same payload, same mode`() {
        // The override must touch priority and nothing else: same bytes on
        // the wire, same codec decision, same reconstruction.
        val text = PhraseDictionary.surfaceFor(4, "en-IN")!!
        val plain = build(text)
        val urgent = build(text, PacketPriority.CRITICAL)

        assertTrue(plain.packet.payload.contentEquals(urgent.packet.payload))
        assertEquals(plain.packet.mode, urgent.packet.mode)
        assertEquals(plain.packet.originalBytes, urgent.packet.originalBytes)
        assertEquals(plain.text, urgent.text)
        assertTrue(plain.roundTripOk && urgent.roundTripOk)
    }

    @Test
    fun `priority survives the wire in both directions`() {
        // Priority rides in 2 bits of the header; an override has to arrive
        // as CRITICAL at the far end or the receiver will not pre-empt.
        for (band in PacketPriority.entries) {
            val packet = build("all is quiet here", band).packet
            val frame = com.itantra.app.transport.PacketCodec
                .serialize(packet, nodeId = 0x1234, seq = 1)!!
            val decoded = com.itantra.app.transport.PacketCodec.deserialize(frame, frame.size)
            assertTrue(decoded is com.itantra.app.transport.PacketCodec.Frame.Data)
            assertEquals(
                band,
                (decoded as com.itantra.app.transport.PacketCodec.Frame.Data).packet.priority,
            )
        }
    }
}
