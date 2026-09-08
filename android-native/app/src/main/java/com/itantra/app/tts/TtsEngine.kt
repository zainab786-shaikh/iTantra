package com.itantra.app.tts

import android.media.AudioAttributes
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
        /**
         * A CRITICAL message plays at the player's full scale.
         *
         * This is only half of "as loud as Android permits" — it removes any
         * per-player attenuation, while [TtsManager] separately raises the
         * media stream itself. Both are needed: a player at 1.0 on a stream
         * turned down to 2/15 is still barely audible.
         */
        critical: Boolean = false,
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
        sweepOrphanedWavs(cacheDir)
        val wavFile = File(cacheDir, "tts-playback-${System.currentTimeMillis()}.wav")
        val saved = GeneratedAudio(audio.samples, audio.sampleRate).save(wavFile.absolutePath)
        check(saved) { "Failed to save synthesized audio to ${wavFile.absolutePath}" }
        tempWavFile = wavFile

        player?.release()
        val mp = MediaPlayer()
        player = mp
        try {
            // Declared explicitly rather than left to the default. SPEECH
            // content lets the platform apply speech-appropriate processing,
            // and naming MEDIA usage keeps playback on the same stream the
            // manager requests audio focus for - they have to agree, or the
            // focus request governs a stream the audio is not on.
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            mp.setVolume(1f, 1f)
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

    /**
     * Stop playback immediately, without disposing the loaded voice. Used for
     * critical interruption.
     *
     * `stop()` rather than `pause()`: this is invoked when a CRITICAL message
     * pre-empts whatever is speaking, and the intent is that the interrupted
     * audio is finished with, not merely suspended mid-word with a decoder
     * still holding the stream. A paused player also stays holding its output
     * until something else releases it, which on the interrupt path is
     * exactly the moment the next voice wants it.
     */
    fun stopPlayback() {
        try {
            player?.stop()
        } catch (e: Exception) {
            // A player already stopped, released, or never started throws
            // rather than no-opping. Nothing to recover here.
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

    /**
     * Delete playback WAVs left behind by earlier app sessions.
     *
     * [cleanupTempFile] only knows about the one file this instance is
     * currently tracking, so a process death mid-playback - or simply
     * closing the app - strands its WAV forever. Each is a megabyte or two
     * and they accumulate silently in the cache; four had built up on the
     * test device inside a day.
     */
    private fun sweepOrphanedWavs(cacheDir: File) {
        try {
            val keep = tempWavFile?.name
            cacheDir.listFiles { file ->
                file.isFile && file.name.startsWith("tts-playback-") &&
                    file.name.endsWith(".wav") && file.name != keep
            }?.forEach { it.delete() }
        } catch (e: Exception) {
            // Housekeeping only - never fail a transmission over disk tidiness.
        }
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
