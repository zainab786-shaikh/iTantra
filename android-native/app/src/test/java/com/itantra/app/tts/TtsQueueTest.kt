package com.itantra.app.tts

import com.itantra.app.packet.PacketPriority
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Regression cover for the failure that silenced the link: a packet id the
 * queue had seen before was refused, so the message arrived, was displayed,
 * and was never spoken — with no error anywhere.
 */
class TtsQueueTest {

    private fun request(id: String, priority: PacketPriority = PacketPriority.NORMAL) =
        SpeakRequest(id = id, text = "text for $id", language = "en-IN", priority = priority)

    @Test
    fun `a genuine duplicate is still refused`() {
        val queue = TtsQueue()
        assertTrue(queue.enqueue(request("NODE-0001")))
        assertFalse("the same id must not be spoken twice", queue.enqueue(request("NODE-0001")))
        assertEquals(1, queue.length)
    }

    @Test
    fun `distinct ids are all accepted`() {
        val queue = TtsQueue()
        repeat(50) { assertTrue(queue.enqueue(request("NODE-%04d".format(it)))) }
        assertEquals(50, queue.length)
    }

    @Test
    fun `the duplicate window is bounded, so one collision cannot mute the link forever`() {
        // The original bug: the sender's sequence restarted at zero on every
        // app launch, so ids repeated and an unbounded "seen" set refused
        // them for the rest of the receiver's session. The sender no longer
        // reuses ids, but the queue must not be a permanent trap either.
        val queue = TtsQueue()
        val firstId = "NODE-0000"
        assertTrue(queue.enqueue(request(firstId)))
        while (queue.dequeue() != null) { /* drain, so length does not mask the check */ }

        // Push far more ids than the window retains.
        for (i in 1..400) {
            queue.enqueue(request("FILL-%04d".format(i)))
            queue.dequeue()
        }

        assertTrue(
            "an id older than the window must be accepted again",
            queue.enqueue(request(firstId)),
        )
    }

    @Test
    fun `critical messages jump ahead of everything queued`() {
        val queue = TtsQueue()
        queue.enqueue(request("n1"))
        queue.enqueue(request("n2"))
        queue.enqueue(request("c1", PacketPriority.CRITICAL))

        assertEquals("c1", queue.dequeue()?.id)
        assertEquals("n1", queue.dequeue()?.id)
        assertEquals("n2", queue.dequeue()?.id)
    }
}
