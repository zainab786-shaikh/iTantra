package com.itantra.app.viewmodel

import android.app.Application
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.native.NativeBridge
import com.itantra.app.packet.PacketPriority
import com.itantra.app.stt.SttEngineKind
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.tts.TtsModelManager
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsVitsModelConfig
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

private const val TAG = "TwoDeviceTest"

/**
 * [P] Phase 12 on two physical phones over UDP, through the app's own
 * AppViewModel (HELLO pairing, native send, UdpTransport, native receive, TTS).
 * Start [receive] on one phone, then [send] on the other; both need the same PSK
 * provisioned in files/itantra-psk.hex.
 */
@RunWith(AndroidJUnit4::class)
class TwoDeviceTest {

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    private fun app(compatibilityOverride: IntArray? = null): AppViewModel {
        lateinit var app: AppViewModel
        instrumentation.runOnMainSync { app = AppViewModel(context.applicationContext as Application, compatibilityOverride) }
        return app
    }

    /** C-34 on both phones: pairing is refused visibly, no session, nothing can be sent or received. */
    private fun assertPairingRefused(app: AppViewModel) {
        await("pairing refusal", 120_000) { app.pairingError.value != null }
        Log.i(TAG, "C-34 pairing refused: ${app.pairingError.value}")
        assertTrue("no session derived", !app.sessionReady)
        val sendRefused = try {
            runBlocking { app.transmitter.transmit("Police move", "en-IN", 900, 0, false) }
            false
        } catch (e: IllegalStateException) {
            Log.i(TAG, "C-34 send refused: ${e.message}")
            true
        }
        assertTrue("no packet can be sealed without a session", sendRefused)
        Thread.sleep(20_000)
        assertTrue("still no session", !app.sessionReady)
        assertEquals("nothing sent", 0, app.transmitter.log.value.size)
        assertEquals("nothing received", 0, app.receiver.messages.value.size)
    }

    /** C-34, the incompatible side: announces a packet format version +1. Run with [c34CompatibleSideRefuses] on the peer. */
    @Test
    fun c34IncompatibleSideRefuses() {
        val local = NativeBridge.createEngine(NativeBridge.readPacks(context.assets)).use { it.compatibility() }
        assertPairingRefused(app(local.copyOf().also { it[0] += 1 }))
    }

    /** C-34, the normal build facing the incompatible peer. */
    @Test
    fun c34CompatibleSideRefuses() {
        assertPairingRefused(app())
    }

    private fun await(what: String, timeoutMs: Long, condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (condition()) return
            Thread.sleep(100)
        }
        fail("timed out waiting for $what")
    }

    @Test
    fun receive() {
        val app = app()
        instrumentation.runOnMainSync { app.receiver.setLanguage("hi-IN") }
        await("session", 120_000) { app.sessionReady }
        await("6 messages", 300_000) { app.receiver.messages.value.size >= 6 }
        Thread.sleep(20_000) // let TTS work through the queue
        val rows = app.receiver.messages.value.sortedBy { it.receivedAt }
        rows.forEach { Log.i(TAG, "row tier ${it.tier} ${it.priority} ${it.textLanguage} state ${it.state} \"${it.text}\" ${it.error ?: ""}") }
        assertTrue("every message decoded", rows.all { it.text.isNotEmpty() && it.tier != 0 })
        assertTrue("Tier 1 rendered in the receiver's language, CRITICAL",
            rows.any { it.tier == 1 && it.textLanguage == "hi-IN" && it.priority == PacketPriority.CRITICAL })
        assertEquals("inherited Tier 1 decoded under a matching context hash", 4, rows.count { it.tier == 1 })
        assertTrue("Tier 2 in the sender's words", rows.any { it.tier == 2 && it.text == "all is quiet here" && it.textLanguage == "en-IN" })
    }

    @Test
    fun send() {
        val app = app()
        await("session", 120_000) { app.sessionReady && app.peerLanguage != null }
        Thread.sleep(3_000)
        runBlocking {
            val tx = app.transmitter
            val t1 = tx.transmit("Fire at the north gate", "en-IN", 900, 0, false).packets.single()
            assertEquals(1, t1.native!!.tier)
            assertEquals(PacketPriority.CRITICAL, t1.priority)
            assertEquals(1, tx.transmit("Send an ambulance to the hospital", "en-IN", 900, 0, false).packets.single().native!!.tier)
            // Same location again: inherited, so the receiver must hold the same context (hash).
            assertEquals(1, tx.transmit("Send an ambulance to the hospital", "en-IN", 900, 0, false).packets.single().native!!.tier)
            assertEquals(1, tx.transmit("Police move", "en-IN", 900, 0, false).packets.single().native!!.tier)
            assertEquals(2, tx.transmit("all is quiet here", "en-IN", NativeBridge.CONFIDENCE_UNAVAILABLE, 0, false).packets.single().native!!.tier)

            // Speech: synthesised by this phone's voice, recognised by its STT, sent as handleSegment does.
            val voicePath = resolveTtsModelForLanguage("en-IN")?.let { TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(it) }
            val stt = SttEngineProvider(File(context.filesDir, "itantra-models"))
            assertTrue("English voice and STT installed", voicePath != null && stt.prepare("en-IN").kind == SttEngineKind.SHERPA_ONNX)
            val dir = File(voicePath!!)
            val tts = OfflineTts(config = OfflineTtsConfig(model = OfflineTtsModelConfig(
                vits = OfflineTtsVitsModelConfig(
                    model = dir.listFiles()!!.first { it.name.endsWith(".onnx") }.absolutePath, lexicon = "",
                    tokens = File(dir, "tokens.txt").absolutePath,
                    dataDir = File(dir, "espeak-ng-data").let { if (it.exists()) it.absolutePath else "" }),
                numThreads = 2, debug = false, provider = "cpu")))
            val audio = tts.generate("send water to the hospital", sid = 0, speed = 1.0f)
            tts.release()
            val out = FloatArray((audio.samples.size.toLong() * 16_000 / audio.sampleRate).toInt()) {
                audio.samples[(it.toLong() * audio.sampleRate / 16_000).toInt().coerceAtMost(audio.samples.size - 1)]
            }
            val heard = stt.transcribe(out, "en-IN").text
            stt.dispose()
            Log.i(TAG, "STT heard \"$heard\"")
            assertTrue(tx.transmit(heard, "en-IN", NativeBridge.CONFIDENCE_UNAVAILABLE, 0, false).packets.isNotEmpty())

            Thread.sleep(3_000)
            val log = tx.log.value
            log.forEach { Log.i(TAG, "sent tier ${it.native?.tier} ${it.priority} counter ${it.native?.counter} delivered ${it.delivered} \"${it.text}\"") }
            assertTrue(log.all { it.delivered })
        }
    }
}
