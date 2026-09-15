package com.itantra.app.native

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.lang.reflect.Modifier

/**
 * [D] Phase 11 tests that run on the device, through the real JNI boundary and
 * the libitantra-native.so the app ships:
 *
 * ```
 * C-25   low-confidence STT never selects Tier 1
 * C-26   NORMAL and CRITICAL survive the round trip; CRITICAL uses the higher read-back bar
 * unit   the JNI boundary is one call per clause, not per token
 * ```
 *
 * Every payload is sealed by the sending half and authenticated and decoded by
 * the receiving half of the same loopback session. The packs are the synthetic
 * fixture packs (hi, ta, en) packaged in Phase 11; the utterances are rows of
 * `native/test/fixtures/synthetic/corpus/tier1.tsv`.
 */
@RunWith(AndroidJUnit4::class)
class NativeConformanceTest {

    private lateinit var engine: NativeEngine

    @Before
    fun loadEngine() {
        assertTrue("libitantra-native.so is loaded", NativeBridge.isNativeLoaded())
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        engine = NativeBridge.createEngine(NativeBridge.readPacks(context.assets))
        engine.beginLoopbackSession()
    }

    @After
    fun closeEngine() {
        engine.close()
    }

    private class Delivered(val sent: NativeClauseEncoding, val received: NativeReceiveResult)

    /** Send one utterance and receive every payload it produced, as the loopback does. */
    private fun roundTrip(
        language: String,
        text: String,
        confidence: Long,
        critical: Boolean = false,
        listener: String = language,
    ): List<Delivered> =
        engine.sendUtterance(text.toByteArray(Charsets.UTF_8), language, listener, confidence, critical)
            .filter { it.sent }
            .map { Delivered(it, engine.receive(listener, it.payload)) }

    private fun single(language: String, text: String, confidence: Long, critical: Boolean = false): Delivered {
        // Each case starts from the context spec §5.3 initial state, as the
        // corpus rows assume.
        engine.beginLoopbackSession()
        val delivered = roundTrip(language, text, confidence, critical)
        assertEquals("one clause sent for \"$text\"", 1, delivered.size)
        return delivered.single()
    }

    @Test
    fun c25_lowConfidenceSttNeverSelectsTier1() {
        // stt_confidence_threshold, lang/<code>/meta.json of the fixture packs.
        val thresholds = mapOf("en" to 700L, "hi" to 600L, "ta" to 550L)
        var checked = 0
        for ((language, text) in TIER1_SAFE_AT_900) {
            // The gate is meaningful only if Tier 1 is otherwise available.
            assertEquals("Tier 1 is safe for \"$text\" at confidence 900", "None", single(language, text, 900).sent.trigger)

            val threshold = thresholds.getValue(language)
            for (confidence in listOf(NativeBridge.CONFIDENCE_UNAVAILABLE, 0L, threshold - 1)) {
                val low = single(language, text, confidence)
                val label = "\"$text\" at confidence $confidence"
                assertEquals(label, 2, low.sent.tier)
                assertEquals(label, "LowSttConfidence", low.sent.trigger)
                assertEquals(label, ReceiveStatus.OK, low.received.status)
                assertEquals(label, 2, low.received.mode)
                assertArrayEquals("Tier 2 returns $label exactly", text.toByteArray(Charsets.UTF_8), low.received.text)
                checked++
            }
        }
        assertEquals(TIER1_SAFE_AT_900.size * 3, checked)
    }

    @Test
    fun c26_normalAndCriticalSurviveTheRoundTrip() {
        class Case(val language: String, val text: String, val confidence: Long, val override: Boolean, val critical: Boolean, val why: String)
        val unavailable = NativeBridge.CONFIDENCE_UNAVAILABLE
        val cases = listOf(
            Case("en", "Fire at the north gate", 900, false, true, "is_alert intent, Tier 1 safe"),
            Case("en", "Police move", 900, false, false, "a NORMAL intent"),
            Case("en", "Police move", 900, true, true, "the operator's override on a Tier 1 clause"),
            Case("en", "okay", 900, false, false, "Tier 2, no override"),
            Case("en", "okay", 900, true, true, "the override, the only path to CRITICAL for Tier 2"),
            Case("hi", "उत्तर द्वार पर आग", unavailable, true, true, "Tier 2 CRITICAL in Devanagari"),
            Case("ta", "வடக்கு வாசலில் தீ", 900, false, true, "is_alert intent in Tamil"),
            Case("en", "Fire at the north gate", unavailable, false, false, "an alert Tier 1 could not verify does not raise priority"),
        )
        for (c in cases) {
            val d = single(c.language, c.text, c.confidence, c.override)
            assertEquals("sent: ${c.why}", if (c.critical) 1 else 0, d.sent.priority)
            assertEquals("received: ${c.why}", d.sent.priority, d.received.priority)
            assertEquals("status: ${c.why}", ReceiveStatus.OK, d.received.status)
            assertEquals("tier: ${c.why}", d.sent.tier, d.received.mode)
        }

        // CRITICAL uses the higher read-back bar (tier §5.8): corpus rows e16 and e17.
        assertEquals("None", single("en", "Send food market", 900, critical = false).sent.trigger)
        val critical = single("en", "Send food market", 900, critical = true)
        assertEquals("ReadbackLostMeaning", critical.sent.trigger)
        assertEquals(2, critical.sent.tier)
        assertEquals(1, critical.received.priority)
    }

    @Test
    fun jniBoundaryIsOneCallPerClauseNotPerToken() {
        val sends = NativeBridge.sendCrossings.get()
        val receives = NativeBridge.receiveCrossings.get()

        // Two clauses — clause punctuation "," and the conjunction "then"
        // (lang/en/normalize.json): one crossing out, one result per clause back.
        val two = engine.sendUtterance(
            "Fire at the north gate, then send blankets to the relief camp".toByteArray(Charsets.UTF_8),
            "en", "en", 900, false,
        )
        assertEquals(sends + 1, NativeBridge.sendCrossings.get())
        assertEquals(2, two.size)
        assertTrue(two.all { it.sent })
        for (clause in two) assertEquals(ReceiveStatus.OK, engine.receive("en", clause.payload).status)
        assertEquals(receives + 2, NativeBridge.receiveCrossings.get())

        // A hundred-word clause — hundreds of coded symbols — is still one crossing
        // each way.
        val long = (1..100).joinToString(" ") { "word$it" }
        val one = engine.sendUtterance(long.toByteArray(Charsets.UTF_8), "en", "en", 900, false)
        assertEquals(sends + 2, NativeBridge.sendCrossings.get())
        assertEquals(1, one.size)
        assertTrue(one.single().sent)
        assertArrayEquals(long.toByteArray(Charsets.UTF_8), engine.receive("en", one.single().payload).text)
        assertEquals(receives + 3, NativeBridge.receiveCrossings.get())

        // Structurally: the only JNI entry points take a whole utterance or a
        // whole payload. There is nothing per token or per symbol to call.
        fun natives(type: Class<*>) = type.declaredMethods.filter { Modifier.isNative(it.modifiers) }.map { it.name }.toSet()
        assertEquals(setOf("nativeVersion", "nativeCreate"), natives(NativeBridge::class.java))
        assertEquals(
            setOf("nativeDestroy", "nativeLanguages", "nativeBeginLoopbackSession", "nativeBeginSession", "nativeCompatibility", "nativeSendUtterance", "nativeReceive"),
            natives(NativeEngine::class.java),
        )
    }

    private companion object {
        /** corpus/tier1.tsv rows expecting Tier 1 at confidence 900, with no context and no pending query. */
        val TIER1_SAFE_AT_900 = listOf(
            "en" to "Fire at the north gate",
            "en" to "Send an ambulance to the hospital",
            "en" to "Send blankets to the relief camp",
            "en" to "Police move",
            "en" to "Do not send water",
            "en" to "Tell Ravi to move",
            "en" to "3 injured",
            "en" to "Evacuate school",
            "en" to "How many injured",
            "hi" to "उत्तर द्वार पर आग",
            "hi" to "अस्पताल में एम्बुलेंस भेजो",
            "hi" to "पानी मत भेजो",
            "hi" to "३ घायल",
            "ta" to "வடக்கு வாசலில் தீ",
            "ta" to "மருத்துவமனையில் ஆம்புலன்ஸ் அனுப்புங்க",
            "ta" to "தண்ணீர் அனுப்ப வேண்டாம்",
            "ta" to "போலீஸ் நகர்",
        )
    }
}
