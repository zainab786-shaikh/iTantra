package com.itantra.app.viewmodel

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.native.NativeBridge
import com.itantra.app.native.NativeEngine
import com.itantra.app.packet.PacketPriority
import com.itantra.app.receiver.ReceivedMessage
import com.itantra.app.receiver.ReceivedMessageState
import com.itantra.app.stt.SttEngineKind
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.transport.MockTransport
import com.itantra.app.tts.TtsModelManager
import com.k2fsa.sherpa.onnx.GeneratedAudio
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsVitsModelConfig
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

private const val TAG = "LoopbackEndToEndTest"

/**
 * Phase 11 exit criterion — "end-to-end speech → speech works on a single device
 * (loopback)" — through the app's own classes, on the device:
 *
 * ```
 * TransmitterViewModel.transmit      (what handleSegment runs after STT)
 *   → buildNativePackets → NativeEngine.sendUtterance        JNI → native send
 *   → MockTransport loopback                                  (the Phase 11 link)
 *   → ReceiverViewModel               NativeEngine.receive     JNI → native receive
 *   → TtsManager                      speech out
 * ```
 *
 * [speechToSpeechOnOneDevice] closes the loop at both ends with the device's
 * own models: speech synthesised by the installed voice, recognised by the
 * app's STT provider, sent through the pipeline above and spoken back.
 */
@RunWith(AndroidJUnit4::class)
class LoopbackEndToEndTest {

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    private lateinit var engine: NativeEngine
    private lateinit var receiver: ReceiverViewModel
    private lateinit var transmitter: TransmitterViewModel

    @Before
    fun setUp() {
        engine = NativeBridge.createEngine(NativeBridge.readPacks(context.assets))
        engine.beginLoopbackSession()
        val transport = MockTransport()
        instrumentation.runOnMainSync {
            receiver = ReceiverViewModel(context, transport, engine)
            transmitter = TransmitterViewModel(context, transport, engine, listenerLanguage = { receiver.language.value })
        }
    }

    @After
    fun tearDown() {
        instrumentation.runOnMainSync {
            transmitter.dispose()
            receiver.dispose()
        }
        engine.close()
    }

    @Test
    fun recognisedTextTravelsThroughTheNativePipelineAndIsSpokenBack() {
        runBlocking {
            val utterance = "Fire at the north gate, then send blankets to the relief camp"
            val sends = NativeBridge.sendCrossings.get()
            val receives = NativeBridge.receiveCrossings.get()

            // Exactly what handleSegment passes: a recogniser with no confidence.
            val send = transmitter.transmit(utterance, "en-IN", NativeBridge.CONFIDENCE_UNAVAILABLE, 0, false)
            assertEquals(sends + 1, NativeBridge.sendCrossings.get())
            assertEquals(2, send.packets.size)
            assertTrue(send.refused.isEmpty())
            // The clauses' Tier 2 inputs concatenate back to the utterance (tier §6.1).
            assertEquals(utterance, send.packets.joinToString("") { it.text })
            for (built in send.packets) {
                val info = checkNotNull(built.native)
                assertEquals("C-25 through the app path", 2, info.tier)
                assertEquals("LowSttConfidence", info.safetyTrigger)
                assertEquals("sealed = plaintext + 4-byte tag", info.plaintextBytes + 4, built.packet.payload.size)
            }

            val rows = awaitRows(2)
            assertEquals(receives + 2, NativeBridge.receiveCrossings.get())
            assertEquals(send.packets.map { it.text }.toSet(), rows.map { it.text }.toSet())
            for (row in rows) {
                assertEquals(2, row.tier)
                assertEquals("en-IN", row.textLanguage)
                assertEquals(PacketPriority.NORMAL, row.priority)
                assertTrue(row.unresolved.isEmpty())
            }
            assertEquals(2, transmitter.log.value.count { it.native?.tier == 2 })

            awaitSpeech(rows, "en-IN")
        }
    }

    @Test
    fun aSafeTier1ClauseIsRenderedInTheReceiversLanguage() {
        runBlocking {
            receiver.setLanguage("hi-IN")
            val spoken = "Fire at the north gate"
            val send = transmitter.transmit(spoken, "en-IN", 900, 0, false)
            val built = send.packets.single()
            val info = checkNotNull(built.native)
            Log.i(TAG, "Tier 1 packet ${info.tier1PacketBytes} B, Tier 2 packet ${info.tier2PacketBytes} B, sent tier ${info.tier}")
            assertEquals("Tier 1 safe", "None", info.safetyTrigger)
            assertEquals("the selector sends the smaller complete packet (C-19)", 1, info.tier)
            assertEquals("is_alert → CRITICAL (packet §11.1)", PacketPriority.CRITICAL, built.priority)

            val row = awaitRows(1).single()
            Log.i(TAG, "Tier 1 rendered for the hi-IN receiver: \"${row.text}\"")
            assertEquals(1, row.tier)
            assertEquals("receiver §7.2: Tier 1 is in the receiver's language", "hi-IN", row.textLanguage)
            assertTrue(row.text.isNotEmpty())
            assertNotEquals("rendered from concept IDs, not carried as text", spoken, row.text)
            assertEquals(PacketPriority.CRITICAL, row.priority)
            assertTrue(row.unresolved.isEmpty())
        }
    }

    @Test
    fun speechToSpeechOnOneDevice() {
        runBlocking {
            val voice = resolveTtsModelForLanguage("en-IN")
            val voicePath = voice?.let { TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(it) }
            assumeTrue("an English voice is installed on this device", voicePath != null)
            val stt = SttEngineProvider(File(context.filesDir, "itantra-models"))
            try {
                assumeTrue("an English STT model is installed", stt.prepare("en-IN").kind == SttEngineKind.SHERPA_ONNX)

                // 1  speech, from this device's own voice model
                val said = "fire at the north gate"
                val audio = synthesize(checkNotNull(voicePath), said)
                // 2  recognised by the app's STT provider, as handleSegment does
                val heard = stt.transcribe(resample(audio.samples, audio.sampleRate, 16_000), "en-IN").text
                Log.i(TAG, "synthesised \"$said\" (${audio.samples.size} samples @ ${audio.sampleRate} Hz), recognised \"$heard\"")
                assertTrue("the STT recognised the synthesised speech", heard.isNotBlank())

                // 3  through the transmitter, the native pipeline, the loopback and the receiver
                val send = transmitter.transmit(heard, "en-IN", NativeBridge.CONFIDENCE_UNAVAILABLE, 0, false)
                assertTrue(send.packets.isNotEmpty())
                assertEquals(heard, send.packets.joinToString("") { it.text })
                val rows = awaitRows(send.packets.size)
                assertEquals(send.packets.map { it.text }.toSet(), rows.map { it.text }.toSet())

                // 4  spoken back by the receiver's TTS
                awaitSpeech(rows, "en-IN")
            } finally {
                stt.dispose()
            }
        }
    }

    private fun awaitRows(count: Int, timeoutMs: Long = 15_000): List<ReceivedMessage> {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val rows = receiver.messages.value
            if (rows.size >= count) return rows.take(count)
            Thread.sleep(50)
        }
        fail("expected $count received rows, got ${receiver.messages.value.size}")
        error("unreachable")
    }

    /**
     * Wait for the receiver's TTS to finish every row. On a device with no voice
     * for [language], the rows must instead report exactly that — TTS was asked
     * to speak and could not, which is still the native pipeline delivering.
     */
    private fun awaitSpeech(rows: List<ReceivedMessage>, language: String, timeoutMs: Long = 90_000) {
        val voiceInstalled = resolveTtsModelForLanguage(language)
            ?.let { TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(it) } != null
        val ids = rows.map { it.packet.id }.toSet()
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val current = receiver.messages.value.filter { it.packet.id in ids }
            val done = if (voiceInstalled) {
                current.all { it.state == ReceivedMessageState.SPOKEN }
            } else {
                current.all { it.state == ReceivedMessageState.ERROR && it.error?.contains("Language model unavailable") == true }
            }
            current.firstOrNull { voiceInstalled && it.state == ReceivedMessageState.ERROR }?.let {
                fail("speech failed for a decoded row: ${it.error}")
            }
            if (done && current.size == ids.size) {
                Log.i(TAG, "speech: ${current.map { it.state }} (voice installed: $voiceInstalled)")
                return
            }
            Thread.sleep(100)
        }
        fail("speech did not complete: ${receiver.messages.value.filter { it.packet.id in ids }.map { it.state to it.error }}")
    }

    /** The same VITS configuration TtsEngine loads, returning samples instead of playing them. */
    private fun synthesize(voicePath: String, text: String): GeneratedAudio {
        val dir = File(voicePath)
        val onnx = checkNotNull(dir.listFiles()?.firstOrNull { it.name.endsWith(".onnx") })
        val dataDir = File(dir, "espeak-ng-data")
        val tts = OfflineTts(
            config = OfflineTtsConfig(
                model = OfflineTtsModelConfig(
                    vits = OfflineTtsVitsModelConfig(
                        model = onnx.absolutePath,
                        lexicon = "",
                        tokens = File(dir, "tokens.txt").absolutePath,
                        dataDir = if (dataDir.exists()) dataDir.absolutePath else "",
                    ),
                    numThreads = 2,
                    debug = false,
                    provider = "cpu",
                ),
            ),
        )
        try {
            return tts.generate(text, sid = 0, speed = 1.0f)
        } finally {
            tts.release()
        }
    }

    /** Linear resampling to the recogniser's 16 kHz. */
    private fun resample(input: FloatArray, from: Int, to: Int): FloatArray {
        if (from == to || input.isEmpty()) return input
        val out = FloatArray((input.size.toLong() * to / from).toInt())
        for (i in out.indices) {
            val position = i.toDouble() * from / to
            val j = position.toInt().coerceAtMost(input.size - 1)
            val k = (j + 1).coerceAtMost(input.size - 1)
            val fraction = (position - j).toFloat()
            out[i] = input[j] + (input[k] - input[j]) * fraction
        }
        return out
    }
}
