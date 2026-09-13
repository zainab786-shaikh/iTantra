package com.itantra.app.transport

import com.itantra.app.packet.ITantraPacket
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The throttle makes a claim on camera — "this is what that many bytes costs
 * at this bitrate" — so the arithmetic behind it is worth pinning down.
 */
class ThrottledTransportTest {

    /** Records what reached the real transport, and when. */
    private class RecordingTransport : Transport {
        override val name = "test://inner"
        val sent = mutableListOf<ITantraPacket>()
        var receiveListener: ((ITantraPacket) -> Unit)? = null

        override suspend fun sendPacket(packet: ITantraPacket): Boolean {
            sent.add(packet)
            return true
        }

        override fun isConnected() = true
        override fun onConnectionChange(listener: (Boolean) -> Unit): () -> Unit = {}
        override fun onPacketReceived(listener: (ITantraPacket) -> Unit): () -> Unit {
            receiveListener = listener
            return { receiveListener = null }
        }
    }

    private fun packet(payloadBytes: Int) = ITantraPacket(
        id = "id",
        senderId = "NODE-0001",
        localTimestamp = 0L,
        payload = ByteArray(payloadBytes),
    )

    // -----------------------------------------------------------------------
    // Airtime arithmetic
    // -----------------------------------------------------------------------

    @Test
    fun `airtime is bytes times eight over bitrate`() {
        // 250 bps is 31.25 bytes per second, so 1000 bytes takes 32 seconds.
        assertEquals(32_000L, airtimeMs(1000, 250))
        assertEquals(8_000L, airtimeMs(250, 250))
        assertEquals(1_472L, airtimeMs(46, 250))   // a 38 B payload in the 8 B frame
        assertEquals(3_616L, airtimeMs(113, 250))  // the same message uncompressed
        assertEquals(0L, airtimeMs(0, 250))
    }

    @Test
    fun `a faster link is proportionally quicker`() {
        assertEquals(airtimeMs(100, 250) / 4, airtimeMs(100, 1000))
    }

    @Test
    fun `a nonsensical bitrate does not divide by zero`() {
        assertEquals(0L, airtimeMs(100, 0))
        assertEquals(0L, airtimeMs(100, -1))
    }

    // -----------------------------------------------------------------------
    // Like-for-like comparison
    // -----------------------------------------------------------------------

    @Test
    fun `both sides of the comparison carry the frame header`() {
        val cost = FrameCost(
            originalBytes = 105,
            payloadBytes = 38,
            headerBytes = PacketCodec.HEADER_BYTES,
            outbound = true,
        )
        // Racing 46 B against the bare 105 B of text would flatter the codec;
        // the honest baseline is what an uncompressed system would transmit,
        // header included.
        assertEquals(113, cost.uncompressedFrameBytes)
        assertEquals(46, cost.sentFrameBytes)
    }

    @Test
    fun `english is not flattered - the header can outweigh what is saved`() {
        // The header is smaller after Phase 0 (8 B, not 11), so this margin
        // widened. The point of the test is unchanged: the comparison must
        // stay like-for-like, and the UI must be able to show a thin win
        // honestly rather than hiding it.
        val cost = FrameCost(37, 35, PacketCodec.HEADER_BYTES, true)
        assertEquals(45, cost.uncompressedFrameBytes)
        assertEquals(43, cost.sentFrameBytes)
        assertTrue(cost.sentFrameBytes < cost.uncompressedFrameBytes)
    }

    @Test
    fun `an inbound frame reports no source size rather than a wrong one`() {
        // packet §1.3 took originalBytes off the wire, so a received frame
        // genuinely does not carry it. Reporting 0 and flagging hasOriginal
        // false is honest; inventing a figure would put a fabricated number
        // on screen next to real measurements.
        val inbound = FrameCost(0, 12, PacketCodec.HEADER_BYTES, outbound = false)
        assertTrue(!inbound.hasOriginal)
        assertTrue(FrameCost(105, 38, PacketCodec.HEADER_BYTES, outbound = true).hasOriginal)
    }

    // -----------------------------------------------------------------------
    // Behaviour
    // -----------------------------------------------------------------------

    @Test
    fun `an enabled throttle actually delays the send`() {
        val inner = RecordingTransport()
        // 1000 bits/s => a 58-byte frame (50 payload + 8 header) is ~464 ms.
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 1000)

        val elapsed = runBlocking {
            val start = System.nanoTime()
            throttle.sendPacket(packet(payloadBytes = 50))
            (System.nanoTime() - start) / 1_000_000
        }

        assertEquals(1, inner.sent.size)
        assertTrue("expected roughly 464 ms, took $elapsed ms", elapsed >= 400)
    }

    @Test
    fun `a disabled throttle is out of the path entirely`() {
        val inner = RecordingTransport()
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 250)
        throttle.setEnabled(false)

        val elapsed = runBlocking {
            val start = System.nanoTime()
            // At 250 bps this frame would otherwise take over 16 seconds.
            throttle.sendPacket(packet(payloadBytes = 500))
            (System.nanoTime() - start) / 1_000_000
        }

        assertEquals(1, inner.sent.size)
        assertTrue("expected no delay, took $elapsed ms", elapsed < 500)
    }

    @Test
    fun `the throttle delegates rather than reimplementing the link`() {
        val inner = RecordingTransport()
        val throttle = ThrottledTransport(inner)
        assertEquals(inner.name, throttle.name)
        assertEquals(inner.isConnected(), throttle.isConnected())
    }

    @Test
    fun `sends and receives are both recorded for the race view`() {
        val inner = RecordingTransport()
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 100_000)
        throttle.onPacketReceived { }

        // The sender states the source size; the transport can no longer
        // read it off the packet (packet §1.3).
        throttle.noteOutbound(105)
        runBlocking { throttle.sendPacket(packet(payloadBytes = 38)) }
        val outbound = throttle.lastFrame.value
        assertNotNull(outbound)
        assertEquals(105, outbound!!.originalBytes)
        assertEquals(38, outbound.payloadBytes)
        assertTrue(outbound.outbound)

        inner.receiveListener!!(packet(payloadBytes = 2))
        val inbound = throttle.lastFrame.value!!
        assertEquals(2, inbound.payloadBytes)
        assertEquals("inbound source size is not knowable here", 0, inbound.originalBytes)
        assertTrue("a received frame is not outbound", !inbound.outbound)
    }

    @Test
    fun `a noted source size applies to one send only`() {
        val inner = RecordingTransport()
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 100_000)

        throttle.noteOutbound(105)
        runBlocking { throttle.sendPacket(packet(payloadBytes = 38)) }
        assertEquals(105, throttle.lastFrame.value!!.originalBytes)

        // A second send that declares nothing must not inherit the first
        // message's figure — that would attribute one utterance's size to a
        // different utterance.
        runBlocking { throttle.sendPacket(packet(payloadBytes = 38)) }
        assertEquals(0, throttle.lastFrame.value!!.originalBytes)
    }

    @Test
    fun `a received packet is never delayed - its airtime was spent by the sender`() {
        val inner = RecordingTransport()
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 250)
        var delivered: ITantraPacket? = null
        throttle.onPacketReceived { delivered = it }

        val start = System.nanoTime()
        inner.receiveListener!!(packet(payloadBytes = 500))
        val elapsed = (System.nanoTime() - start) / 1_000_000

        assertNotNull(delivered)
        assertTrue("receive must not be throttled, took $elapsed ms", elapsed < 200)
    }
}
