package com.itantra.app.packet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The manual CRITICAL override, and the guarantee that it does not disturb
 * the automatic classification it sits on top of.
 *
 * ## What Phase 0 changed here
 *
 * `buildPacket` no longer encodes — it wraps an already-assembled native
 * payload (`packet §1.4`) — so the mode/originalBytes/round-trip assertions
 * that used to live in this file are gone with the fields they read.
 *
 * The test `priority survives the wire in both directions` was **deleted, not
 * repaired.** It asserted that priority rides in the outer header, which is
 * precisely what `packet §1.3` forbids: priority is a 1-bit field inside the
 * native payload and the outer frame must not carry a copy. A test asserting
 * the old layout would now be a test asserting a spec violation.
 *
 * Its replacement is **C-26** ("`NORMAL` and `CRITICAL` messages: priority
 * survives round-trip"), which runs against the native payload in Phase 11
 * (`contract §5.5`).
 */
class PacketFactoryTest {

    /** Stands in for the native payload. Opaque here by design (`packet §1.1`). */
    private val payload = byteArrayOf(0x01, 0x02, 0x03)

    private fun build(text: String, override: PacketPriority? = null) = buildPacket(
        text = text,
        language = "en-IN",
        senderId = "ITX-TEST0001",
        payload = payload,
        priority = override,
    )

    // -----------------------------------------------------------------------
    // Automatic classification
    // -----------------------------------------------------------------------

    @Test
    fun `without an override the keyword classifier still decides`() {
        assertEquals(PacketPriority.CRITICAL, build("there is a fire at the gate").priority)
        assertEquals(PacketPriority.NORMAL, build("all is quiet here").priority)
    }

    @Test
    fun `there are exactly two priorities`() {
        // HIGH and MEDIUM are not "unused", they do not exist: packet §11,
        // receiver §7.4, language §11.2, tier register #22.
        assertEquals(2, PacketPriority.entries.size)
        assertTrue(PacketPriority.entries.containsAll(
            listOf(PacketPriority.NORMAL, PacketPriority.CRITICAL),
        ))
    }

    @Test
    fun `words that used to raise HIGH or MEDIUM now classify NORMAL`() {
        // Not an oversight — the bands they mapped to are gone. Anything here
        // that genuinely warrants escalation becomes an is_alert intent in
        // intents.bin (language §11.2), which is a Phase 6 decision made with
        // the codebook in hand, not a word list promoted here.
        assertEquals(PacketPriority.NORMAL, build("send help immediately").priority)
        assertEquals(PacketPriority.NORMAL, build("report status").priority)
    }

    @Test
    fun `normal traffic is untouched by the feature existing`() {
        assertEquals(PacketPriority.NORMAL, build("all is quiet here").priority)
    }

    // -----------------------------------------------------------------------
    // Manual override
    // -----------------------------------------------------------------------

    @Test
    fun `the override raises a message the classifier would have called NORMAL`() {
        // The whole point: an operator can flag something urgent that happens
        // to contain none of the trigger words.
        val text = "all is quiet here"
        assertEquals(PacketPriority.NORMAL, build(text).priority)
        assertEquals(PacketPriority.CRITICAL, build(text, PacketPriority.CRITICAL).priority)
    }

    @Test
    fun `the override does not disturb a message already classified CRITICAL`() {
        val text = "there is a fire at the gate"
        assertEquals(PacketPriority.CRITICAL, build(text, PacketPriority.CRITICAL).priority)
    }

    @Test
    fun `an overridden packet is otherwise identical`() {
        // The override must touch priority and nothing else.
        val text = "all is quiet here"
        val plain = build(text)
        val urgent = build(text, PacketPriority.CRITICAL)

        assertTrue(plain.packet.payload.contentEquals(urgent.packet.payload))
        assertEquals(plain.originalBytes, urgent.originalBytes)
        assertEquals(plain.language, urgent.language)
        assertEquals(plain.text, urgent.text)
    }

    // -----------------------------------------------------------------------
    // Sender-side facts stay sender-side
    // -----------------------------------------------------------------------

    @Test
    fun `originalBytes is measured on the sender and is the UTF-8 length`() {
        // M-01 (contract §6.1). Measured here because packet §1.3 took it off
        // the wire — the receiver has no copy to disagree with.
        val text = "उत्तर द्वार पर आग"
        assertEquals(text.toByteArray(Charsets.UTF_8).size, build(text).originalBytes)
    }

    @Test
    fun `the outer frame carries no copy of the sender-side facts`() {
        // packet §1.3: "The outer frame must not carry copies of these."
        // Enforced structurally — these properties do not exist on the packet.
        val built = build("there is a fire at the gate", PacketPriority.CRITICAL)
        val fields = ITantraPacket::class.java.declaredFields.map { it.name }.toSet()
        for (forbidden in listOf("priority", "language", "mode", "originalBytes", "text")) {
            assertTrue(
                "ITantraPacket must not carry '$forbidden' (packet §1.3)",
                forbidden !in fields,
            )
        }
        // They are still available, on the sending side, where they belong.
        assertEquals(PacketPriority.CRITICAL, built.priority)
        assertEquals("en-IN", built.language)
    }
}
