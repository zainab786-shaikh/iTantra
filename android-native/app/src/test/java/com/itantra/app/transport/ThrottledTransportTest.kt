package com.itantra.app.transport

import com.itantra.app.codec.CodecMode
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority
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

    private fun packet(
        payloadBytes: Int,
        originalBytes: Int = payloadBytes,
        mode: CodecMode = CodecMode.PACK7,
    ) = ITantraPacket(
        id = "id",
        senderId = "NODE-0001",
        timestamp = 0L,
        language = "hi-IN",
        payload = ByteArray(payloadBytes),
        mode = mode,
        originalBytes = originalBytes,
        priority = PacketPriority.NORMAL,
    )

    // -----------------------------------------------------------------------
    // Airtime arithmetic
    // -----------------------------------------------------------------------

    @Test
    fun `airtime is bytes times eight over bitrate`() {
        // 250 bps is 31.25 bytes per second, so 1000 bytes takes 32 seconds.
        assertEquals(32_000L, airtimeMs(1000, 250))
        assertEquals(8_000L, airtimeMs(250, 250))
        assertEquals(1_568L, airtimeMs(49, 250))   // a real PACK7 Hindi frame
        assertEquals(3_712L, airtimeMs(116, 250))  // the same message uncompressed
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
            mode = CodecMode.PACK7,
            headerBytes = PacketCodec.HEADER_BYTES,
            outbound = true,
        )
        // Racing 49 B against the bare 105 B of text would flatter the codec;
        // the honest baseline is what an uncompressed system would transmit,
        // header included.
        assertEquals(116, cost.uncompressedFrameBytes)
        assertEquals(49, cost.sentFrameBytes)
    }

    @Test
    fun `english is not flattered - the header can outweigh what PACK7 saves`() {
        // Measured case: 37 B of English text packs to 35 B, but the frame is
        // 46 B against an uncompressed frame of 48 B. Still a win, but a small
        // one, and the UI must be able to show that honestly.
        val cost = FrameCost(37, 35, CodecMode.PACK7, PacketCodec.HEADER_BYTES, true)
        assertEquals(48, cost.uncompressedFrameBytes)
        assertEquals(46, cost.sentFrameBytes)
        assertTrue(cost.sentFrameBytes < cost.uncompressedFrameBytes)
    }

    // -----------------------------------------------------------------------
    // Behaviour
    // -----------------------------------------------------------------------

    @Test
    fun `an enabled throttle actually delays the send`() {
        val inner = RecordingTransport()
        // 1000 bits/s => a 61-byte frame (50 payload + 11 header) is ~488 ms.
        val throttle = ThrottledTransport(inner, initialBitsPerSecond = 1000)

        val elapsed = runBlocking {
            val start = System.nanoTime()
            throttle.sendPacket(packet(payloadBytes = 50))
            (System.nanoTime() - start) / 1_000_000
        }

        assertEquals(1, inner.sent.size)
        assertTrue("expected roughly 488 ms, took $elapsed ms", elapsed >= 400)
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

        runBlocking { throttle.sendPacket(packet(payloadBytes = 38, originalBytes = 105)) }
        val outbound = throttle.lastFrame.value
        assertNotNull(outbound)
        assertEquals(105, outbound!!.originalBytes)
        assertEquals(38, outbound.payloadBytes)
        assertTrue(outbound.outbound)

        inner.receiveListener!!(packet(payloadBytes = 2, originalBytes = 47, mode = CodecMode.PHRASE))
        val inbound = throttle.lastFrame.value!!
        assertEquals(CodecMode.PHRASE, inbound.mode)
        assertEquals(2, inbound.payloadBytes)
        assertTrue("a received frame is not outbound", !inbound.outbound)
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
