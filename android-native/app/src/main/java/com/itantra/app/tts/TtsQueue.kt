package com.itantra.app.tts

import com.itantra.app.packet.PacketPriority

/**
 * Direct port of src/core/tts/TtsQueue.ts.
 *
 * Ordering for received messages waiting to be spoken. NORMAL/MEDIUM/HIGH
 * messages play in arrival order; a CRITICAL message jumps ahead of
 * everything queued and, if something is already playing, the caller
 * (TtsManager) interrupts it — this class only owns ordering, not playback.
 */
class TtsQueue {

    private companion object {
        /** How many recent ids are remembered for duplicate rejection. */
        const val SEEN_WINDOW = 256
    }

    private val normal = ArrayDeque<SpeakRequest>()
    private val critical = ArrayDeque<SpeakRequest>()

    /**
     * Ids already accepted, as a bounded rolling window rather than a set
     * that remembers forever.
     *
     * An unbounded set is a trap: any id collision, from any cause, silences
     * that message permanently and silently - it arrives, it is displayed,
     * and it is never spoken, with no error anywhere. That is exactly what
     * happened when the sender's sequence counter restarted at zero on app
     * launch and replayed ids this queue had already spoken.
     *
     * The sender no longer reuses ids (see UdpTransport), so this window is
     * defence in depth: it still rejects the genuine duplicate a
     * retransmission would produce, but it forgets old ids, so no single
     * collision can mute the link for the rest of the session.
     */
    private val seen = LinkedHashSet<String>()

    /** @return false if [request]'s id was already enqueued or spoken (duplicate packet). */
    fun enqueue(request: SpeakRequest): Boolean {
        if (seen.contains(request.id)) return false
        seen.add(request.id)
        // LinkedHashSet keeps insertion order, so the head is the oldest id.
        while (seen.size > SEEN_WINDOW) {
            val iterator = seen.iterator()
            iterator.next()
            iterator.remove()
        }

        if (request.priority == PacketPriority.CRITICAL) {
            critical.addLast(request)
        } else {
            normal.addLast(request)
        }
        return true
    }

    /** True if a CRITICAL message is waiting — callers use this to decide whether to interrupt current playback. */
    fun hasPendingCritical(): Boolean = critical.isNotEmpty()

    val length: Int
        get() = critical.size + normal.size

    /** Pop the next request to speak: critical messages always win. */
    fun dequeue(): SpeakRequest? {
        if (critical.isNotEmpty()) return critical.removeFirst()
        if (normal.isNotEmpty()) return normal.removeFirst()
        return null
    }

    /**
     * Put an in-flight normal-band request back at the front of its band,
     * because a CRITICAL message just interrupted it. Bypasses the duplicate
     * check — this is the same message being restored, not a new arrival.
     */
    fun requeueFront(request: SpeakRequest) {
        if (request.priority == PacketPriority.CRITICAL) {
            critical.addFirst(request)
        } else {
            normal.addFirst(request)
        }
    }

    fun clear() {
        normal.clear()
        critical.clear()
        // `seen` is intentionally not cleared: a cleared queue should not let
        // a duplicate of an already-delivered packet back in.
    }
}
