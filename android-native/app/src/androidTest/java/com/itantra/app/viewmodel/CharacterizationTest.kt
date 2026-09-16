package com.itantra.app.viewmodel

import android.app.Application
import android.content.Context
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.native.NativeBridge
import com.itantra.app.stt.SttEngineKind
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.tts.TtsEngine
import com.itantra.app.tts.TtsModelManager
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsVitsModelConfig
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

private const val TAG = "Characterization"

/** The Phase 12 pair script: the representative demo conversation (Tier 1 and Tier 2). */
private val CONVERSATION = listOf(
    "Send an ambulance to the hospital" to 1000L,
    "Send medicine to the hospital" to 1000L,
    "Send water to the hospital" to 1000L,
    "Fire at the north gate" to 1000L,
    "Send an ambulance to the hospital" to 1000L,
    "all is quiet here" to NativeBridge.CONFIDENCE_UNAVAILABLE,
    "Send medicine to the hospital" to 1000L,
    "Police move away" to 1000L,
    "The weather is good today" to NativeBridge.CONFIDENCE_UNAVAILABLE,
    "Send water to the hospital" to 1000L,
)

/** Spoken inputs per STT language — the fixture packs' own phrases, synthesised by the phone's voice. */
private val SPOKEN = mapOf(
    "en-IN" to listOf(
        "fire at the north gate", "send an ambulance to the hospital", "police move away",
        "call the police", "send help immediately", "the weather is good today",
    ),
    "hi-IN" to listOf("उत्तर द्वार पर आग", "अस्पताल में एम्बुलेंस भेजो", "पुलिस दूर हटो", "पुलिस को बुलाओ"),
    "ta-IN" to listOf("வடக்கு வாயிலில் தீ", "காவல்துறையை அழை"),
    // No Marathi language pack exists (native packs: en, hi, ta): speech-only characterisation.
    "mr-IN" to listOf("उत्तर दरवाजावर आग लागली आहे", "रुग्णालयात रुग्णवाहिका पाठवा", "पोलिसांना बोलवा", "लगेच मदत पाठवा"),
)

/**
 * [D] / [P] Phase 13 characterisation (validation contract §6.2): recorded, not graded.
 *
 * Every figure is logged as `CHAR <id>\t<value>\t<unit>\t<breakdown>` (logcat tag
 * `Characterization`). Device-local methods are meant to run one per `am instrument`
 * invocation, so each starts in a fresh process ("process-cold"; the OS page cache is
 * not controlled). Measurement points are defined here, once:
 *
 *   native     NativeEngine.sendUtterance / receive, per clause, JNI crossing included
 *   STT load   SttEngineProvider.prepare (model load); inference = transcribe()
 *   TTS load   TtsEngine.load; speak() = synthesis + WAV + MediaPlayer start (audible start)
 *   E2E        sender: endpointer fired → STT text → transmit() returned (encode + seal + send);
 *              receiver: UDP arrival (UdpTransport log) → native result → TTS SPEAKING state →
 *              audio actually playing (AudioManager playback callback)
 */
@RunWith(AndroidJUnit4::class)
class CharacterizationTest {

    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val args = InstrumentationRegistry.getArguments()
    private val device = "${Build.MODEL}"

    private fun record(id: String, value: Double, unit: String, breakdown: String) {
        Log.i(TAG, "CHAR $id\t${"%.3f".format(value)}\t$unit\t$device $breakdown")
    }

    private fun status(key: String): Long =
        File("/proc/self/status").readLines().firstOrNull { it.startsWith("$key:") }
            ?.split(Regex("\\s+"))?.get(1)?.toLong() ?: -1L

    private fun rssMb() = status("VmRSS") / 1024.0
    private fun peakRssMb() = status("VmHWM") / 1024.0

    private fun median(v: List<Double>) = v.sorted().let { if (it.isEmpty()) 0.0 else it[it.size / 2] }
    private fun p95(v: List<Double>) = v.sorted().let { if (it.isEmpty()) 0.0 else it[((it.size - 1) * 0.95).toInt()] }

    private fun ttsVoiceDir(language: String): File? =
        resolveTtsModelForLanguage(language)?.let { TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(it) }?.let(::File)

    private fun offlineTts(dir: File) = OfflineTts(config = OfflineTtsConfig(model = OfflineTtsModelConfig(
        vits = OfflineTtsVitsModelConfig(
            model = dir.listFiles()!!.first { it.name.endsWith(".onnx") }.absolutePath, lexicon = "",
            tokens = File(dir, "tokens.txt").absolutePath,
            dataDir = File(dir, "espeak-ng-data").let { if (it.exists()) it.absolutePath else "" }),
        numThreads = 2, debug = false, provider = "cpu")))

    /** Synthesised speech at 16 kHz, the STT input rate. */
    private fun speech16k(tts: OfflineTts, text: String): FloatArray {
        val audio = tts.generate(text, sid = 0, speed = 1.0f)
        return FloatArray((audio.samples.size.toLong() * 16_000 / audio.sampleRate).toInt()) {
            audio.samples[(it.toLong() * audio.sampleRate / 16_000).toInt().coerceAtMost(audio.samples.size - 1)]
        }
    }

    private fun words(s: String) = s.lowercase().replace(Regex("[\\p{P}\\p{S}]"), " ").split(Regex("\\s+")).filter { it.isNotEmpty() }

    /** Word error rate by edit distance. */
    private fun wer(reference: String, hypothesis: String): Double {
        val r = words(reference)
        val h = words(hypothesis)
        val d = Array(r.size + 1) { IntArray(h.size + 1) }
        for (i in 0..r.size) d[i][0] = i
        for (j in 0..h.size) d[0][j] = j
        for (i in 1..r.size) for (j in 1..h.size) {
            d[i][j] = minOf(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + if (r[i - 1] == h[j - 1]) 0 else 1)
        }
        return if (r.isEmpty()) 0.0 else d[r.size][h.size].toDouble() / r.size
    }

    private fun await(what: String, timeoutMs: Long, condition: () -> Boolean) {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (condition()) return
            Thread.sleep(50)
        }
        fail("timed out waiting for $what")
    }

    private fun environment() {
        Log.i(TAG, "CHAR ENV\t$device\t${Build.MANUFACTURER} ${Build.HARDWARE} ${Build.BOARD}\tAndroid ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})" +
            "\t${Runtime.getRuntime().availableProcessors()} cpus")
    }

    // -----------------------------------------------------------------------
    // M-23 / M-24 / M-25 [D]: the native engine
    // -----------------------------------------------------------------------

    @Test
    fun nativeEngine() {
        environment()
        val rss0 = rssMb()
        var t = SystemClock.elapsedRealtimeNanos()
        val packs = NativeBridge.readPacks(context.assets)
        val readMs = (SystemClock.elapsedRealtimeNanos() - t) / 1e6
        t = SystemClock.elapsedRealtimeNanos()
        val engine = NativeBridge.createEngine(packs)
        val createMs = (SystemClock.elapsedRealtimeNanos() - t) / 1e6
        record("LOAD", readMs, "ms", "native packs read from assets (${packs.size} files)")
        record("LOAD", createMs, "ms", "native engine create (pack parse/validate)")
        record("M-25", rssMb() - rss0, "MB RSS delta", "native engine loaded")
        engine.beginLoopbackSession()

        for (listener in listOf("en-IN", "hi-IN")) {
            val encode = mutableListOf<Double>()
            val decode = mutableListOf<Double>()
            val encodeCpu = mutableListOf<Double>()
            repeat(5) {
                for ((text, confidence) in CONVERSATION) {
                    val bytes = text.toByteArray()
                    val c0 = SystemClock.currentThreadTimeMillis()
                    t = SystemClock.elapsedRealtimeNanos()
                    val clauses = engine.sendUtterance(bytes, "en-IN", listener, confidence, false)
                    encode.add((SystemClock.elapsedRealtimeNanos() - t) / 1e6)
                    encodeCpu.add((SystemClock.currentThreadTimeMillis() - c0).toDouble())
                    for (c in clauses.filter { it.sent }) {
                        t = SystemClock.elapsedRealtimeNanos()
                        engine.receive(listener, c.payload)
                        decode.add((SystemClock.elapsedRealtimeNanos() - t) / 1e6)
                    }
                }
            }
            // The first round includes first-touch costs; report steady state separately.
            record("M-23", encode.first(), "ms", "encode first call en->$listener")
            record("M-23", median(encode.drop(10)), "ms median", "encode per clause en->$listener (JNI incl., n=${encode.size - 10})")
            record("M-23", p95(encode.drop(10)), "ms p95", "encode per clause en->$listener")
            record("M-23", encodeCpu.drop(10).average(), "ms thread CPU mean", "encode en->$listener")
            record("M-24", decode.first(), "ms", "decode first call en->$listener")
            record("M-24", median(decode.drop(10)), "ms median", "decode per payload en->$listener (JNI incl., n=${decode.size - 10})")
            record("M-24", p95(decode.drop(10)), "ms p95", "decode per payload en->$listener")
        }
        record("M-25", rssMb(), "MB RSS", "process after native runs")
        record("M-25", peakRssMb(), "MB peak RSS", "process after native runs")
        engine.close()
    }

    // -----------------------------------------------------------------------
    // STT [D]: model load, cold / warm inference, RTF, CPU, memory, WER on synthetic speech
    // -----------------------------------------------------------------------

    @Test
    fun stt() {
        environment()
        val language = args.getString("lang") ?: "en-IN"
        val voice = ttsVoiceDir(language) ?: return fail("no $language voice to synthesise test speech")
        val phrases = SPOKEN.getValue(language)
        val tts = offlineTts(voice)
        val inputs = phrases.map { speech16k(tts, it) }
        tts.release()
        System.gc()
        Thread.sleep(1_000)

        val rss0 = rssMb()
        val provider = SttEngineProvider(File(context.filesDir, "itantra-models"))
        var t = SystemClock.elapsedRealtime()
        val kind = provider.prepare(language).kind
        val loadMs = (SystemClock.elapsedRealtime() - t).toDouble()
        assertTrue("real decoder for $language", kind == SttEngineKind.SHERPA_ONNX)
        record("STT", loadMs, "ms", "$language model load (prepare, process-cold)")
        record("STT", rssMb() - rss0, "MB RSS delta", "$language model loaded")

        val latencies = mutableListOf<Double>()
        val rtf = mutableListOf<Double>()
        val cores = mutableListOf<Double>()
        val errors = mutableListOf<Double>()
        for (round in 0 until 3) {
            inputs.forEachIndexed { i, samples ->
                val cpu0 = Process.getElapsedCpuTime()
                t = SystemClock.elapsedRealtime()
                val heard = provider.transcribe(samples, language).text
                val ms = (SystemClock.elapsedRealtime() - t).toDouble()
                val audioMs = samples.size / 16.0
                if (round == 0 && i == 0) {
                    record("STT", ms, "ms", "$language first inference (cold), ${"%.0f".format(audioMs)} ms audio")
                } else {
                    latencies.add(ms)
                    rtf.add(ms / audioMs)
                    cores.add((Process.getElapsedCpuTime() - cpu0) / ms)
                }
                if (round == 0) {
                    errors.add(wer(phrases[i], heard))
                    Log.i(TAG, "STT $language \"${phrases[i]}\" -> \"$heard\" (${"%.0f".format(ms)} ms)")
                }
            }
        }
        record("STT", median(latencies), "ms median", "$language warm inference (n=${latencies.size})")
        record("STT", p95(latencies), "ms p95", "$language warm inference")
        record("STT", median(rtf), "RTF median", "$language warm (inference ms / audio ms)")
        record("STT", cores.average(), "CPU cores busy (process)", "$language during inference")
        record("M-34*", errors.average() * 100, "% WER", "$language on TTS-synthesised speech, n=${errors.size} (not real speech)")
        record("M-25", peakRssMb(), "MB peak RSS", "process with $language STT")
        provider.dispose()
    }

    // -----------------------------------------------------------------------
    // TTS [D]: voice load, synthesis RTF, audible start, warm vs cold, memory
    // -----------------------------------------------------------------------

    @Test
    fun tts() {
        environment()
        val language = args.getString("lang") ?: "en-IN"
        val model = resolveTtsModelForLanguage(language)!!
        val path = TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(model)
            ?: return fail("no $language voice installed")
        val rss0 = rssMb()
        val engine = TtsEngine()
        var t = SystemClock.elapsedRealtime()
        engine.load(model, path)
        record("TTS", (SystemClock.elapsedRealtime() - t).toDouble(), "ms", "$language voice load ${model.id} (process-cold)")
        record("TTS", rssMb() - rss0, "MB RSS delta", "$language voice loaded")

        val synth = mutableListOf<Double>()
        val start = mutableListOf<Double>()
        val rtf = mutableListOf<Double>()
        var first = true
        for (round in 0 until 2) for (text in SPOKEN.getValue(language)) {
            val done = CountDownLatch(1)
            t = SystemClock.elapsedRealtime()
            val result = engine.speak(text, context.cacheDir, onFinished = { done.countDown() }, onPlaybackError = { done.countDown() })
            val audible = (SystemClock.elapsedRealtime() - t).toDouble()
            if (first) {
                record("TTS", result.synthesisMs.toDouble(), "ms", "$language first synthesis (cold)")
                record("TTS", audible, "ms", "$language first request -> audio playing (cold)")
                first = false
            } else {
                synth.add(result.synthesisMs.toDouble())
                start.add(audible)
                rtf.add(result.synthesisMs / result.audioDurationMs)
            }
            done.await(30, TimeUnit.SECONDS)
        }
        record("TTS", median(synth), "ms median", "$language warm synthesis (n=${synth.size})")
        record("TTS", median(start), "ms median", "$language warm request -> audio playing (synthesis + WAV + MediaPlayer)")
        record("TTS", median(start) - median(synth), "ms median", "$language WAV write + MediaPlayer prepare/start")
        record("TTS", median(rtf), "RTF median", "$language warm (synthesis ms / audio ms)")
        record("M-25", peakRssMb(), "MB peak RSS", "process with $language voice")
        engine.dispose()
    }

    // -----------------------------------------------------------------------
    // Phase 14.1 RAM gate: can two TTS voices stay resident next to the app's STT model?
    // `-e stt <STT language>` `-e voices <lang,lang>`. Android's own low-memory signal decides.
    // -----------------------------------------------------------------------

    @Test
    fun residentVoicesMemory() {
        environment()
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
        fun system(label: String) {
            val info = android.app.ActivityManager.MemoryInfo().also(am::getMemoryInfo)
            record("RAM", info.availMem / 1048576.0, "MB available", "$label (threshold ${info.threshold / 1048576} MB, " +
                "lowMemory=${info.lowMemory}, total ${info.totalMem / 1048576} MB, process RSS ${"%.0f".format(rssMb())} MB)")
        }
        val sttLanguage = args.getString("stt") ?: "hi-IN"
        val voices = (args.getString("voices") ?: "hi-IN,en-IN").split(',')
        system("baseline")
        val packs = NativeBridge.readPacks(context.assets)
        val engine = NativeBridge.createEngine(packs)
        val stt = SttEngineProvider(File(context.filesDir, "itantra-models"))
        check(stt.prepare(sttLanguage).kind == SttEngineKind.SHERPA_ONNX)
        system("native engine + $sttLanguage STT")
        val loaded = mutableListOf<TtsEngine>()
        for (language in voices) {
            val model = resolveTtsModelForLanguage(language)!!
            val path = TtsModelManager(File(context.filesDir, "itantra-tts-models")).resolvePath(model)!!
            loaded.add(TtsEngine().also { it.load(model, path) })
            // A resident voice must also have run once: first inference allocates its arena.
            val done = CountDownLatch(1)
            loaded.last().speak(SPOKEN.getValue(language).first(), context.cacheDir, onFinished = { done.countDown() }, onPlaybackError = { done.countDown() })
            done.await(30, TimeUnit.SECONDS)
            system("+ voice ${model.id} (${loaded.size} resident)")
        }
        record("M-25", peakRssMb(), "MB peak RSS", "$sttLanguage STT + voices $voices")
        loaded.forEach { it.dispose() }
        stt.dispose()
        engine.close()
    }

    // -----------------------------------------------------------------------
    // M-26 [D]: idle-listening CPU (needs RECORD_AUDIO granted)
    // -----------------------------------------------------------------------

    @Test
    fun idleListening() {
        environment()
        lateinit var app: AppViewModel
        instrumentation.runOnMainSync { app = AppViewModel(context.applicationContext as Application) }
        Thread.sleep(5_000)
        val windowMs = 30_000L
        fun cpuPercentOver(): Double {
            val cpu0 = Process.getElapsedCpuTime()
            val t0 = SystemClock.elapsedRealtime()
            Thread.sleep(windowMs)
            return 100.0 * (Process.getElapsedCpuTime() - cpu0) / (SystemClock.elapsedRealtime() - t0)
        }
        val idle = cpuPercentOver()
        instrumentation.runOnMainSync { app.transmitter.startPtt(context) }
        Thread.sleep(2_000)
        assertTrue("capture running", app.transmitter.isActive.value)
        val listening = cpuPercentOver()
        instrumentation.runOnMainSync { app.transmitter.stopPtt() }
        record("M-26", idle, "% of one core", "app idle, link up, not listening (${windowMs / 1000} s)")
        record("M-26", listening, "% of one core", "listening: capture + VAD, no speech expected (${windowMs / 1000} s)")
        record("M-26", listening - idle, "% of one core", "capture + VAD attributable")
        record("M-26", (listening - idle) / Runtime.getRuntime().availableProcessors(), "% of all cores", "capture + VAD attributable")
        record("M-25", peakRssMb(), "MB peak RSS", "full app (engine, link, TTS manager, STT provider)")
    }

    // -----------------------------------------------------------------------
    // M-20 / M-21 / M-22 [P]: end-to-end over the real link
    // -----------------------------------------------------------------------

    private val e2ePhrases = listOf(
        "fire at the north gate", "the weather is good today", "send an ambulance to the hospital",
        "hello this is a test message", "police move away", "call the police",
    )

    /** Sender. `-e lang <receiver language>` `-e throttle on|off` `-e rounds N`. Start [e2eReceive] on the peer first. */
    @Test
    fun e2eSend() {
        environment()
        val receiverLanguage = args.getString("lang") ?: "hi-IN"
        val throttle = args.getString("throttle") == "on"
        val rounds = args.getString("rounds")?.toInt() ?: 2
        val tts = offlineTts(ttsVoiceDir("en-IN")!!)
        val inputs = e2ePhrases.map { speech16k(tts, it) }
        tts.release()
        val stt = SttEngineProvider(File(context.filesDir, "itantra-models"))
        check(stt.prepare("en-IN").kind == SttEngineKind.SHERPA_ONNX)
        stt.transcribe(inputs[0], "en-IN")   // warm: the app keeps the decoder loaded between utterances

        lateinit var app: AppViewModel
        instrumentation.runOnMainSync { app = AppViewModel(context.applicationContext as Application) }
        await("session", 120_000) { app.sessionReady && app.peerLanguage == receiverLanguage }
        Thread.sleep(3_000)
        val wasOn = app.throttle.enabled.value
        instrumentation.runOnMainSync { app.throttle.setEnabled(throttle) }
        val sttMs = mutableListOf<Double>()
        val sendMs = mutableListOf<Double>()
        try {
            runBlocking {
                for (round in 0 until rounds) inputs.forEachIndexed { i, samples ->
                    // t0 = the endpointer firing (speech end + the 750 ms end-of-speech silence).
                    val t0 = SystemClock.elapsedRealtimeNanos()
                    val heard = stt.transcribe(samples, "en-IN").text
                    val t1 = SystemClock.elapsedRealtimeNanos()
                    val send = app.transmitter.transmit(heard, "en-IN", sttConfidenceFor(SttEngineKind.SHERPA_ONNX), 0, false)
                    val t2 = SystemClock.elapsedRealtimeNanos()
                    sttMs.add((t1 - t0) / 1e6)
                    sendMs.add((t2 - t1) / 1e6)
                    val p = send.packets.joinToString(";") { "tier=${it.native?.tier} counter=${it.native?.counter} packet=${it.packet.payload.size + 8}B" }
                    Log.i(TAG, "E2E tx phrase=\"${e2ePhrases[i]}\" heard=\"$heard\" stt=${"%.1f".format((t1 - t0) / 1e6)}ms " +
                        "encode+send=${"%.1f".format((t2 - t1) / 1e6)}ms throttle=$throttle sentAt=${System.currentTimeMillis()} $p")
                    Thread.sleep(9_000)   // let the receiver finish speaking: measure unqueued latency
                }
            }
        } finally {
            instrumentation.runOnMainSync { app.throttle.setEnabled(wasOn) }
        }
        record("M-20", median(sttMs), "ms median", "endpointer -> STT text (en-IN, warm, n=${sttMs.size})")
        record("M-20", median(sendMs), "ms median", "STT text -> transmit returned (encode+seal+UDP send, throttle=$throttle)")
        record("M-20", median(sttMs.zip(sendMs) { a, b -> a + b }), "ms median", "endpointer -> packet on the air (throttle=$throttle)")
        stt.dispose()
    }

    /** Receiver. `-e lang <this phone's language>` `-e count N`. */
    @Test
    fun e2eReceive() {
        environment()
        val language = args.getString("lang") ?: "hi-IN"
        val count = args.getString("count")?.toInt() ?: 12
        lateinit var app: AppViewModel
        instrumentation.runOnMainSync { app = AppViewModel(context.applicationContext as Application) }
        data class Row(val counter: Long, val tier: Int, val status: Int, val lang: String?, val text: String, val decodedAt: Long) {
            var speakingAt = 0L
            var playingAt = 0L
        }
        val rows = mutableListOf<Row>()
        app.receiver.onNativeResult = { r ->
            if (r.emit) synchronized(rows) { rows.add(Row(r.counter, r.mode, r.status, r.languageCode, String(r.text), System.currentTimeMillis())) }
        }
        val audio = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        var playing = false
        val callback = object : AudioManager.AudioPlaybackCallback() {
            override fun onPlaybackConfigChanged(configs: MutableList<android.media.AudioPlaybackConfiguration>) {
                val now = System.currentTimeMillis()
                if (configs.isNotEmpty() && !playing) synchronized(rows) { rows.lastOrNull { it.playingAt == 0L }?.playingAt = now }
                playing = configs.isNotEmpty()
            }
        }
        audio.registerAudioPlaybackCallback(callback, Handler(Looper.getMainLooper()))
        val collector = Thread {
            var last: String? = null
            while (!Thread.currentThread().isInterrupted) {
                val s = app.receiver.ttsState.value
                if (s.phase.name == "SPEAKING" && s.requestId != last) {
                    last = s.requestId
                    synchronized(rows) { rows.lastOrNull { it.speakingAt == 0L }?.speakingAt = System.currentTimeMillis() }
                }
                try { Thread.sleep(5) } catch (e: InterruptedException) { break }
            }
        }
        collector.start()
        instrumentation.runOnMainSync { app.receiver.setLanguage(language) }
        await("session", 120_000) { app.sessionReady }
        await("$count messages", 900_000) { synchronized(rows) { rows.size >= count } }
        Thread.sleep(10_000)
        collector.interrupt()
        audio.unregisterAudioPlaybackCallback(callback)
        val all = synchronized(rows) { rows.toList() }
        all.forEach {
            Log.i(TAG, "E2E rx counter=${it.counter} tier=${it.tier} status=${it.status} lang=${it.lang} decodedAt=${it.decodedAt} " +
                "speaking=+${it.speakingAt - it.decodedAt}ms playing=+${it.playingAt - it.decodedAt}ms \"${it.text}\"")
        }
        val played = all.filter { it.playingAt > 0 }
        record("M-21*", median(played.map { (it.playingAt - it.decodedAt).toDouble() }), "ms median",
            "native result -> audio playing ($language, n=${played.size}, voice switches included)")
        record("M-21*", median(played.drop(1).filter { it.tier == 1 }.map { (it.playingAt - it.decodedAt).toDouble() }), "ms median", "Tier 1 -> audio playing")
        record("M-21*", median(played.drop(1).filter { it.tier == 2 }.map { (it.playingAt - it.decodedAt).toDouble() }), "ms median", "Tier 2 -> audio playing")
        // Phase 14.1 split: a message whose output language differs from the previous one's
        // needs a different voice; same-language messages reuse the one just used.
        val switched = played.drop(1).filter { m -> all.getOrNull(all.indexOf(m) - 1)?.lang != m.lang }
        val same = played.drop(1).filter { m -> all.getOrNull(all.indexOf(m) - 1)?.lang == m.lang }
        record("E2E", median(switched.map { (it.playingAt - it.decodedAt).toDouble() }), "ms median", "language switch -> audio playing ($language, n=${switched.size})")
        record("E2E", median(same.map { (it.playingAt - it.decodedAt).toDouble() }), "ms median", "same language -> audio playing ($language, n=${same.size})")
        record("E2E", (played.firstOrNull()?.let { it.playingAt - it.decodedAt } ?: 0L).toDouble(), "ms", "first message -> audio playing ($language)")
        record("M-25", rssMb(), "MB RSS", "receiver after the run ($language)")
        record("M-25", peakRssMb(), "MB peak RSS", "receiver during the run ($language)")
    }
}
