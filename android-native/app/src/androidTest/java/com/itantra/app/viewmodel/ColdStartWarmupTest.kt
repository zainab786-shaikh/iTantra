package com.itantra.app.viewmodel

import android.app.Application
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.core.ModelReadiness
import com.itantra.app.packet.PacketPriority
import com.itantra.app.stt.SttEngineKind
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.tts.TtsManager
import com.itantra.app.tts.TtsModelManager
import com.itantra.app.tts.TtsPlaybackPhase
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsVitsModelConfig
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.BeforeClass
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections

private const val TAG = "ColdStartWarmup"

/**
 * [D] Phase 14.2: background warm-up of this phone's STT model and own TTS voice.
 * Needs the en-IN STT model and the hi-IN / en-IN voices installed.
 */
@RunWith(AndroidJUnit4::class)
class ColdStartWarmupTest {

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val ttsRoot = File(context.filesDir, "itantra-tts-models")
    private val sttRoot = File(context.filesDir, "itantra-models")

    companion object {
        /** "fire at the north gate", synthesised once per test process at 16 kHz. */
        private lateinit var speech: FloatArray

        @BeforeClass
        @JvmStatic
        fun synthesise() {
            val context = InstrumentationRegistry.getInstrumentation().targetContext
            val dir = File(TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(resolveTtsModelForLanguage("en-IN")!!)!!)
            val tts = OfflineTts(config = OfflineTtsConfig(model = OfflineTtsModelConfig(
                vits = OfflineTtsVitsModelConfig(
                    model = dir.listFiles()!!.first { it.name.endsWith(".onnx") }.absolutePath, lexicon = "",
                    tokens = File(dir, "tokens.txt").absolutePath,
                    dataDir = File(dir, "espeak-ng-data").let { if (it.exists()) it.absolutePath else "" }),
                numThreads = 2, debug = false, provider = "cpu")))
            val audio = tts.generate("fire at the north gate", sid = 0, speed = 1.0f)
            tts.release()
            speech = FloatArray((audio.samples.size.toLong() * 16_000 / audio.sampleRate).toInt()) {
                audio.samples[(it.toLong() * audio.sampleRate / 16_000).toInt().coerceAtMost(audio.samples.size - 1)]
            }
        }
    }

    private fun await(what: String, timeoutMs: Long, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            Thread.sleep(20)
        }
        fail("timed out waiting for $what")
    }

    private fun sttProviderOf(app: AppViewModel): SttEngineProvider =
        TransmitterViewModel::class.java.getDeclaredField("sttProvider").let { it.isAccessible = true; it.get(app.transmitter) as SttEngineProvider }

    /** Speak and wait until the queue is idle again; returns the errors reported. */
    private fun speakAndWait(tts: TtsManager, id: String, text: String, language: String): List<String> {
        val errors = Collections.synchronizedList(mutableListOf<String>())
        val spoken = Collections.synchronizedList(mutableListOf<String>())
        val unsubscribe = tts.subscribe { s ->
            if (s.requestId == id && s.phase == TtsPlaybackPhase.ERROR) errors.add(s.error ?: "error")
            if (s.requestId == id && s.phase == TtsPlaybackPhase.SPEAKING) spoken.add(id)
        }
        tts.speakText(text, language, PacketPriority.NORMAL, id)
        await("$id finished", 60_000) { (spoken.isNotEmpty() || errors.isNotEmpty()) && tts.getState().phase == TtsPlaybackPhase.IDLE }
        unsubscribe()
        return errors.toList()
    }

    @Test
    fun startupWarmsTheSpeechModelAndOwnVoiceThenFirstUseLoadsNothing() {
        lateinit var app: AppViewModel
        val t0 = SystemClock.elapsedRealtime()
        instrumentation.runOnMainSync {
            app = AppViewModel(context.applicationContext as Application)
            app.receiver.setLanguage("hi-IN")
        }
        await("speech model ready", 60_000) { app.transmitter.sttReadiness.value == ModelReadiness.READY }
        val sttReadyMs = SystemClock.elapsedRealtime() - t0
        await("own voice ready", 60_000) { app.receiver.voiceReadiness.value == ModelReadiness.READY }
        val voiceReadyMs = SystemClock.elapsedRealtime() - t0
        Log.i(TAG, "STT ready +$sttReadyMs ms, voice ready +$voiceReadyMs ms")
        val tts = app.receiver.ttsManager
        val hi = resolveTtsModelForLanguage("hi-IN")!!.id
        assertEquals("only the receiver's own voice was warmed", listOf(hi), tts.residentVoices())
        assertEquals(1, tts.voiceLoads)

        // Repeated language selections do not create a second instance.
        repeat(3) { instrumentation.runOnMainSync { app.receiver.setLanguage("hi-IN") } }
        Thread.sleep(500)
        assertEquals(1, tts.voiceLoads)

        // First STT: the resident model, no load.
        val provider = sttProviderOf(app)
        assertEquals(SttEngineKind.SHERPA_ONNX, provider.status.kind)
        var t = SystemClock.elapsedRealtime()
        val heard = provider.transcribe(speech, "en-IN").text
        Log.i(TAG, "first STT after warm-up ${SystemClock.elapsedRealtime() - t} ms: \"$heard\"")
        assertTrue(heard, heard.contains("fire"))

        // First TTS: a hit on the warmed voice.
        t = SystemClock.elapsedRealtime()
        val errors = speakAndWait(tts, "first", "उत्तर द्वार पर आग", "hi-IN")
        Log.i(TAG, "first message spoken in ${SystemClock.elapsedRealtime() - t} ms (incl. playback)")
        assertTrue(errors.toString(), errors.isEmpty())
        assertEquals("first message loaded no voice", 1, tts.voiceLoads)
        assertEquals(1, tts.voiceHits)
        assertEquals(hi, tts.lastSpokenWith)
    }

    @Test
    fun aWarmUpThatCannotRunFallsBackToTheNormalLazyLoad() {
        val tts = TtsManager(context, ttsRoot)
        try {
            // No voice registered for this language: the warm-up reports UNAVAILABLE and loads nothing.
            tts.setPrimaryLanguage("xx-XX")
            await("warm-up unavailable", 10_000) { tts.primaryVoice.value == ModelReadiness.UNAVAILABLE }
            assertEquals(0, tts.voiceLoads)
            // The first message still loads its voice lazily, exactly as before Phase 14.2.
            assertTrue(speakAndWait(tts, "lazy", "पुलिस को बुलाओ", "hi-IN").isEmpty())
            assertEquals(1, tts.voiceLoads)
        } finally {
            tts.dispose()
        }
    }

    @Test
    fun aMissingVoiceIsReportedAndNothingCrashes() {
        val empty = File(context.cacheDir, "no-voices").apply { deleteRecursively(); mkdirs() }
        val tts = TtsManager(context, empty)
        try {
            tts.setPrimaryLanguage("hi-IN")
            await("warm-up unavailable", 10_000) { tts.primaryVoice.value == ModelReadiness.UNAVAILABLE }
            val errors = speakAndWait(tts, "missing", "पुलिस को बुलाओ", "hi-IN")
            assertEquals(1, errors.size)
            assertEquals(0, tts.voiceLoads)
        } finally {
            tts.dispose()
        }
    }

    @Test
    fun aMessageDuringTheWarmUpWaitsForItAndLoadsTheVoiceOnce() {
        val tts = TtsManager(context, ttsRoot)
        try {
            tts.setPrimaryLanguage("hi-IN")
            // Immediately: the warm-up is still loading (1–2.5 s).
            assertTrue(speakAndWait(tts, "during", "पुलिस को बुलाओ", "hi-IN").isEmpty())
            assertEquals("one instance, shared by warm-up and message", 1, tts.voiceLoads)
            assertEquals(resolveTtsModelForLanguage("hi-IN")!!.id, tts.lastSpokenWith)
        } finally {
            tts.dispose()
        }
    }

    @Test
    fun aLanguageChangeDuringTheWarmUpEndsReadyForTheNewLanguage() {
        val tts = TtsManager(context, ttsRoot)
        try {
            tts.setPrimaryLanguage("hi-IN")
            tts.setPrimaryLanguage("en-IN")
            val en = resolveTtsModelForLanguage("en-IN")!!.id
            await("en voice ready", 60_000) { tts.primaryVoice.value == ModelReadiness.READY && en in tts.residentVoices() }
            Thread.sleep(3_000)
            assertTrue("at most the two voices involved", tts.voiceLoads <= 2)
            assertTrue(tts.residentVoices().size <= 2)
        } finally {
            tts.dispose()
        }
    }

    @Test
    fun disposingDuringTheWarmUpIsSafe() {
        repeat(2) {
            val tts = TtsManager(context, ttsRoot)
            tts.setPrimaryLanguage("hi-IN")
            Thread.sleep(200)
            tts.dispose()
        }
        Thread.sleep(3_000)
        val tts = TtsManager(context, ttsRoot)
        try {
            assertTrue(speakAndWait(tts, "after-dispose", "पुलिस को बुलाओ", "hi-IN").isEmpty())
        } finally {
            tts.dispose()
        }
    }

    @Test
    fun anUtteranceBeforeAnyLoadIsDecodedNotReplacedByThePlaceholder() {
        val provider = SttEngineProvider(sttRoot)
        try {
            val heard = provider.transcribe(speech, "en-IN").text
            assertTrue(heard, heard.contains("fire"))
            assertEquals(SttEngineKind.SHERPA_ONNX, provider.status.kind)
        } finally {
            provider.dispose()
        }
    }

    @Test
    fun anUtteranceDuringTheModelLoadWaitsAndIsDecoded() {
        val provider = SttEngineProvider(sttRoot)
        try {
            val loader = Thread { provider.prepare("en-IN") }
            loader.start()
            Thread.sleep(50)
            val heard = provider.transcribe(speech, "en-IN").text
            loader.join()
            assertTrue(heard, heard.contains("fire"))
        } finally {
            provider.dispose()
        }
    }
}
