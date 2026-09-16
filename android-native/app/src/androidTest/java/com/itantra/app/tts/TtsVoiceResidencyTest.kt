package com.itantra.app.tts

import android.content.Context
import android.media.AudioManager
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.packet.PacketPriority
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections

private const val TAG = "TtsVoiceResidency"

/**
 * [D] Phase 14.1: the receiver's TTS keeps its own voice and the most recently used other
 * voice resident. Real TtsManager, real Piper voices (hi-IN, en-IN must be installed).
 */
@RunWith(AndroidJUnit4::class)
class TtsVoiceResidencyTest {

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val audio = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val stream = AudioManager.STREAM_MUSIC
    private lateinit var tts: TtsManager
    private val errors = Collections.synchronizedList(mutableListOf<String>())
    private val spoken = Collections.synchronizedList(mutableListOf<String>())
    private var operatorVolume = 0
    private var operatorMuted = false

    private val hi = resolveTtsModelForLanguage("hi-IN")!!.id
    private val en = resolveTtsModelForLanguage("en-IN")!!.id

    @Before
    fun setUp() {
        operatorVolume = audio.getStreamVolume(stream)
        operatorMuted = audio.isStreamMute(stream)
        tts = TtsManager(context, File(context.filesDir, "itantra-tts-models"))
        tts.subscribe { s ->
            if (s.phase == TtsPlaybackPhase.ERROR) errors.add("${s.requestId}: ${s.error}")
            if (s.phase == TtsPlaybackPhase.SPEAKING) s.requestId?.let(spoken::add)
        }
        // Phase 14.1 counts are exact only without the Phase 14.2 warm-up (ColdStartWarmupTest covers it).
        tts.setPrimaryLanguage("hi-IN", prewarm = false)
    }

    @After
    fun tearDown() {
        tts.dispose()
        audio.setStreamVolume(stream, operatorVolume, 0)
        if (audio.isStreamMute(stream) != operatorMuted) {
            audio.adjustStreamVolume(stream, if (operatorMuted) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE, 0)
        }
    }

    private var n = 0

    /** Speak one message, wait until the queue has drained, and return the voice that spoke it and the wall time. */
    private fun speak(language: String, priority: PacketPriority = PacketPriority.NORMAL): Pair<String?, Long> {
        val id = "msg-${n++}"
        val text = if (language == "hi-IN") "पुलिस को बुलाओ" else "call the police"
        val t0 = SystemClock.elapsedRealtime()
        assertTrue(tts.speakText(text, language, priority, id))
        val deadline = t0 + 60_000
        while (SystemClock.elapsedRealtime() < deadline) {
            if (id in spoken && tts.getState().phase == TtsPlaybackPhase.IDLE) {
                return tts.lastSpokenWith to (SystemClock.elapsedRealtime() - t0)
            }
            Thread.sleep(20)
        }
        fail("message $id in $language was not spoken")
        error("unreachable")
    }

    @Test
    fun sameLanguageConsecutiveMessagesLoadTheVoiceOnce() {
        repeat(4) { assertEquals(hi, speak("hi-IN").first) }
        assertEquals(1, tts.voiceLoads)
        assertEquals(3, tts.voiceHits)
        assertTrue(errors.toString(), errors.isEmpty())
    }

    @Test
    fun alternatingLanguagesStopReloadingOnceBothVoicesAreResident() {
        val sequence = listOf("hi-IN", "en-IN", "hi-IN", "en-IN", "hi-IN", "en-IN", "en-IN", "hi-IN")
        val expectedLoads = listOf(1, 2, 2, 2, 2, 2, 2, 2)
        sequence.forEachIndexed { i, language ->
            val (voice, ms) = speak(language)
            Log.i(TAG, "message $i $language voice=$voice loads=${tts.voiceLoads} hits=${tts.voiceHits} total=${ms}ms")
            assertEquals("message $i spoken with the $language voice", if (language == "hi-IN") hi else en, voice)
            assertEquals("voice loads after message $i", expectedLoads[i], tts.voiceLoads)
        }
        assertEquals(6, tts.voiceHits)
        assertEquals(setOf(hi, en), tts.residentVoices().toSet())
        assertTrue(errors.toString(), errors.isEmpty())
    }

    @Test
    fun aThirdLanguageLoadsCorrectlyAndKeepsTheReceiversOwnVoice() {
        val ta = resolveTtsModelForLanguage("ta-IN")!!.id
        assertEquals(hi, speak("hi-IN").first)
        assertEquals(en, speak("en-IN").first)
        val third = speak("ta-IN").first
        if (errors.isNotEmpty()) return   // Tamil voice not installed on this phone: nothing more to check
        assertEquals(ta, third)
        assertEquals(3, tts.voiceLoads)
        assertEquals(setOf(hi, ta), tts.residentVoices().toSet())
        assertEquals(hi, speak("hi-IN").first)
        assertEquals("the receiver's own voice was not reloaded", 3, tts.voiceLoads)
    }

    @Test
    fun volumeZeroIsRestoredAfterAlternatingMessages() {
        if (audio.isStreamMute(stream)) audio.adjustStreamVolume(stream, AudioManager.ADJUST_UNMUTE, 0)
        audio.setStreamVolume(stream, 0, 0)
        speak("hi-IN")
        speak("en-IN")
        assertEquals("operator's volume 0 restored", 0, audio.getStreamVolume(stream))
    }

    @Test
    fun muteIsRestoredAfterAlternatingMessages() {
        audio.setStreamVolume(stream, 3, 0)
        audio.adjustStreamVolume(stream, AudioManager.ADJUST_MUTE, 0)
        assertTrue("stream muted for the test", audio.isStreamMute(stream))
        speak("en-IN")
        speak("hi-IN")
        assertTrue("mute restored", audio.isStreamMute(stream))
    }

    @Test
    fun criticalBoostIsRestoredAcrossAVoiceSwitch() {
        if (audio.isStreamMute(stream)) audio.adjustStreamVolume(stream, AudioManager.ADJUST_UNMUTE, 0)
        audio.setStreamVolume(stream, 1, 0)
        speak("en-IN", PacketPriority.CRITICAL)
        speak("hi-IN", PacketPriority.CRITICAL)
        assertEquals("operator's volume restored after critical", 1, audio.getStreamVolume(stream))
        assertEquals(2, tts.voiceLoads)
    }
}
