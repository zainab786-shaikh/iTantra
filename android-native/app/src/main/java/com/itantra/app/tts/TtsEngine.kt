package com.itantra.app.tts

import android.media.MediaPlayer
import com.itantra.app.config.TtsModelDescriptor
import com.k2fsa.sherpa.onnx.GeneratedAudio
import com.k2fsa.sherpa.onnx.OfflineTts
import com.k2fsa.sherpa.onnx.OfflineTtsConfig
import com.k2fsa.sherpa.onnx.OfflineTtsModelConfig
import com.k2fsa.sherpa.onnx.OfflineTtsVitsModelConfig
import java.io.File

data class SpeakResult(val synthesisMs: Long, val audioDurationMs: Double)

/**
 * Direct sherpa-onnx TTS engine, adapted from src/core/tts/TtsEngine.ts.
 *
 * Calls com.k2fsa.sherpa.onnx.* directly (same AAR as Phase 2's STT
 * SttEngine), modeled on react-native-sherpa-onnx's SherpaOnnxTtsHelper.kt
 * "vits" branch (node_modules/react-native-sherpa-onnx/android/src/main/
 * java/com/sherpaonnx/SherpaOnnxTtsHelper.kt, buildTtsConfig()'s "vits"
 * case) — the only TTSModelType this app's TTS registry uses (both Piper
 * and MMS voices are VITS models). No React Native TurboModule bridge.
 *
 * The RN wrapper resolves a model directory's file layout via a native C++
 * "model detect" helper that is internal to that package's own JNI glue,
 * not part of the public sherpa-onnx Kotlin API — so it cannot be called
 * directly from here. Instead this scans the directory using the exact
 * layout already documented in src/config/ttsModels.ts and
 * TtsModelManager.ts: a single `*.onnx` file (the VITS model), `tokens.txt`,
 * and — for Piper voices only, which need espeak-ng for text-to-phoneme
 * conversion — a shared `espeak-ng-data/` subdirectory as `dataDir` (left
 * empty for MMS voices, which the source's own comment states do not need
 * it). `lexicon` is left empty for both, matching the source registry
 * (neither packaging documents a lexicon.txt).
 *
 * Playback uses android.media.MediaPlayer in place of expo-audio's
 * AudioPlayer — the same kind of native-Android replacement Phase 3 made
 * for audio capture (AudioRecord replacing expo-audio's stream) — since
 * expo-audio is an Expo/RN library with no place in a from-scratch native
 * Kotlin app.
 */
class TtsEngine {
    private var tts: OfflineTts? = null
    private var loadedModelId: String? = null
    private var player: MediaPlayer? = null
    private var tempWavFile: File? = null

    val isReady: Boolean
        get() = tts != null

    val loadedFor: String?
        get() = loadedModelId

    /** Load (or swap to) the voice at [modelPath]. Disposes any previously loaded voice first. */
    fun load(model: TtsModelDescriptor, modelPath: String) {
        if (loadedModelId == model.id && tts != null) return
        disposeNative()

        val dir = File(modelPath)
        val onnxFile = dir.listFiles()?.firstOrNull { it.name.endsWith(".onnx") }
            ?: throw IllegalStateException("No .onnx model file found in $modelPath")
        val tokensFile = File(dir, "tokens.txt")
        check(tokensFile.exists()) { "Missing tokens.txt in $modelPath" }
        val dataDir = File(dir, "espeak-ng-data")

        val vitsConfig = OfflineTtsVitsModelConfig(
            model = onnxFile.absolutePath,
            lexicon = "",
            tokens = tokensFile.absolutePath,
            dataDir = if (dataDir.exists()) dataDir.absolutePath else "",
        )
        val modelConfig = OfflineTtsModelConfig(
            vits = vitsConfig,
            numThreads = 2,
            debug = false,
            provider = "cpu",
        )
        tts = OfflineTts(config = OfflineTtsConfig(model = modelConfig))
        loadedModelId = model.id
    }

    /**
     * Synthesize [text] and play it via [cacheDir]-backed temp WAV. Returns
     * once playback has actually started (not finished) — callers that need
     * to know when speech ends should use [onFinished].
     */
    fun speak(
        text: String,
        cacheDir: File,
        onFinished: () -> Unit,
        onPlaybackError: (String) -> Unit,
    ): SpeakResult {
        val engine = checkNotNull(tts) { "No TTS voice loaded" }

        val synthStart = System.currentTimeMillis()
        val audio = engine.generate(text, sid = 0, speed = 1.0f)
        val synthesisMs = System.currentTimeMillis() - synthStart
        val audioDurationMs = if (audio.samples.isNotEmpty()) {
            (audio.samples.size.toDouble() / audio.sampleRate) * 1000.0
        } else {
            0.0
        }

        cleanupTempFile()
        val wavFile = File(cacheDir, "tts-playback-${System.currentTimeMillis()}.wav")
        val saved = GeneratedAudio(audio.samples, audio.sampleRate).save(wavFile.absolutePath)
        check(saved) { "Failed to save synthesized audio to ${wavFile.absolutePath}" }
        tempWavFile = wavFile

        player?.release()
        val mp = MediaPlayer()
        player = mp
        try {
            mp.setDataSource(wavFile.absolutePath)
            mp.setOnCompletionListener { onFinished() }
            mp.setOnErrorListener { _, what, extra ->
                onPlaybackError("MediaPlayer error (what=$what, extra=$extra)")
                true
            }
            mp.prepare()
            mp.start()
        } catch (e: Exception) {
            onPlaybackError(e.message ?: "Playback failed to start")
        }

        return SpeakResult(synthesisMs, audioDurationMs)
    }

    /** Stop playback immediately, without disposing the loaded voice. Used for critical interruption. */
    fun stopPlayback() {
        try {
            player?.pause()
        } catch (e: Exception) {
            // Best effort.
        }
    }

    fun dispose() {
        disposeNative()
        cleanupTempFile()
    }

    private fun disposeNative() {
        player?.release()
        player = null
        tts?.release()
        tts = null
        loadedModelId = null
    }

    private fun cleanupTempFile() {
        val file = tempWavFile ?: return
        tempWavFile = null
        try {
            if (file.exists()) file.delete()
        } catch (e: Exception) {
            // Non-fatal: a leftover temp WAV costs space, not correctness.
        }
    }
}
