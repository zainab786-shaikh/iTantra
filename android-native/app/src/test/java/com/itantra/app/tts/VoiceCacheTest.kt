package com.itantra.app.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * Phase 14.1 residency policy: the receiver's own voice plus the most recently used
 * other voice stay loaded, so alternating output languages stop reloading voices.
 */
class VoiceCacheTest {

    /** A stand-in voice: which id it was loaded for, and whether it has been released. */
    private class Voice(val id: String) {
        var released = false
    }

    private val created = mutableListOf<Voice>()
    private var failNext = false

    private fun cache(capacity: Int = 2, primary: String? = null) = VoiceCache(
        capacity = capacity,
        load = { id ->
            if (failNext) {
                failNext = false
                throw IllegalStateException("model file missing")
            }
            Voice(id).also { created.add(it) }
        },
        release = { it.released = true },
    ).also { it.primary = primary }

    @Test
    fun `same-language consecutive messages load the voice once`() {
        val c = cache(primary = "hi")
        repeat(5) { assertEquals("hi", c.get("hi").id) }
        assertEquals(1, c.loads)
        assertEquals(4, c.hits)
    }

    @Test
    fun `alternating languages load each voice once, then only hit`() {
        val c = cache(primary = "hi")
        val spoken = listOf("hi", "en", "hi", "en", "hi", "en", "en", "hi").map { c.get(it) }
        assertEquals(2, c.loads)
        assertEquals(6, c.hits)
        assertEquals(listOf("hi", "en", "hi", "en", "hi", "en", "en", "hi"), spoken.map { it.id })
        assertTrue(created.none { it.released })
    }

    @Test
    fun `a hit returns the very voice already resident`() {
        val c = cache(primary = "hi")
        val hi = c.get("hi")
        c.get("en")
        assertSame(hi, c.get("hi"))
    }

    @Test
    fun `a miss loads the requested voice and evicts the non-primary one`() {
        val c = cache(primary = "hi")
        c.get("hi")
        val en = c.get("en")
        val ta = c.get("ta")
        assertEquals("ta", ta.id)
        assertEquals(3, c.loads)
        assertTrue("the alternate voice is released", en.released)
        assertEquals(setOf("hi", "ta"), c.residentIds().toSet())
    }

    @Test
    fun `the primary voice survives even when it is least recently used`() {
        val c = cache(primary = "hi")
        val hi = c.get("hi")
        c.get("en")
        c.get("en")
        c.get("ta")        // hi is the least recently used, but primary: en goes
        c.get("mr")        // ta goes
        assertTrue(!hi.released)
        assertEquals(setOf("hi", "mr"), c.residentIds().toSet())
        assertSame(hi, c.get("hi"))
    }

    @Test
    fun `without a primary the least recently used voice is evicted`() {
        val c = cache()
        val hi = c.get("hi")
        c.get("en")
        c.get("hi")
        val en = c.get("en")
        c.get("ta")        // hi was used before en
        assertTrue(hi.released)
        assertTrue(!en.released)
    }

    @Test
    fun `changing the primary makes the previous one evictable`() {
        val c = cache(primary = "hi")
        val hi = c.get("hi")
        c.get("en")
        c.primary = "en"
        c.get("ta")
        assertTrue(hi.released)
        assertEquals(setOf("en", "ta"), c.residentIds().toSet())
    }

    @Test
    fun `never more than capacity voices resident, even across a load`() {
        val c = cache(primary = "hi")
        for (id in listOf("hi", "en", "ta", "mr", "gu", "hi", "bn")) {
            c.get(id)
            assertTrue(c.residentIds().size <= 2)
            assertTrue(created.count { !it.released } <= 2)
        }
    }

    @Test
    fun `a failed load leaves nothing half-loaded behind`() {
        val c = cache(primary = "hi")
        c.get("hi")
        failNext = true
        try {
            c.get("en")
            fail("load should have thrown")
        } catch (e: IllegalStateException) {
            // expected
        }
        assertEquals(listOf("hi"), c.residentIds())
        assertEquals(1, c.loads)
        assertEquals("en", c.get("en").id)   // the retry loads it properly
        assertEquals(2, c.loads)
    }

    @Test
    fun `clear releases every resident voice`() {
        val c = cache(primary = "hi")
        c.get("hi")
        c.get("en")
        c.clear()
        assertTrue(created.all { it.released })
        assertTrue(c.residentIds().isEmpty())
    }

    @Test
    fun `capacity one behaves as the previous single-voice engine`() {
        val c = cache(capacity = 1, primary = "hi")
        listOf("hi", "en", "hi", "en").forEach { c.get(it) }
        assertEquals(4, c.loads)
    }
}
