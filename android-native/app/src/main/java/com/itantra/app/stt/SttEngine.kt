package com.itantra.app.stt

import com.k2fsa.sherpa.onnx.FeatureConfig
import com.k2fsa.sherpa.onnx.OfflineModelConfig
import com.k2fsa.sherpa.onnx.OfflineNemoEncDecCtcModelConfig
import com.k2fsa.sherpa.onnx.OfflineRecognizer
import com.k2fsa.sherpa.onnx.OfflineRecognizerConfig
import com.k2fsa.sherpa.onnx.OfflineStream
import java.io.File

/**
 * Direct sherpa-onnx nemo_ctc engine — Phase 2 proof-of-life.
 *
 * Calls com.k2fsa.sherpa.onnx.* directly (bundled from the same
 * com.xdcobra.sherpa:sherpa-onnx AAR the RN app's react-native-sherpa-onnx
 * dependency already resolves — see app/build.gradle.kts), with no React
 * Native TurboModule bridge in between.
 *
 * Modeled on react-native-sherpa-onnx's SherpaOnnxSttHelper.kt "nemo_ctc"
 * branch (node_modules/react-native-sherpa-onnx/android/src/main/java/com/
 * sherpaonnx/SherpaOnnxSttHelper.kt). Only nemo_ctc is implemented: per
 * src/config/models.ts, that is the only modelType this app's STT config
 * actually uses, for both English (NeMo CTC Medium) and all nine Indic
 * languages (AI4Bharat IndicConformer).
 */
class SttEngine {
    private var recognizer: OfflineRecognizer? = null

    val isReady: Boolean
        get() = recognizer != null

    /**
     * Load a nemo_ctc model from [modelDir], which must contain
     * "model.int8.onnx" and "tokens.txt" — the exact file names this
     * project's ModelManager (src/core/stt/ModelManager.ts) already
     * downloads/side-loads under those names.
     */
    fun load(modelDir: String, numThreads: Int = 2) {
        release()
        val modelFile = File(modelDir, "model.int8.onnx")
        val tokensFile = File(modelDir, "tokens.txt")
        check(modelFile.exists()) { "Missing model.int8.onnx in $modelDir" }
        check(tokensFile.exists()) { "Missing tokens.txt in $modelDir" }
        check(hasVocabSizeMetadata(modelFile)) {
            // sherpa-onnx's native nemo_ctc loader (offline-nemo-enc-dec-ctc-model.cc)
            // calls a fatal, unrecoverable native abort (SIGABRT) when this ONNX
            // metadata key is absent from the model - it never throws a catchable
            // Java exception. Verified on device: selecting the Odia decoder, whose
            // model.int8.onnx (from OpenVoiceOS/ai4bharat-indicconformer-or-onnx)
            // lacks this key, killed the whole app with "FORTIFY: pthread_mutex_lock
            // called on a destroyed mutex" mid-construction, not a normal exception.
            // Checked here by scanning the raw file bytes for the metadata key's
            // literal name before ever reaching that constructor - an onnxruntime
            // session-based check was tried first but a full graph load of a
            // ~137MB model hung indefinitely on-device, which is worse than the
            // crash it was meant to prevent - so a bad model surfaces as the same
            // "model unavailable" fallback every other missing/broken model already
            // produces, instead of terminating (or hanging) the app.
            "Missing 'vocab_size' metadata in model.int8.onnx in $modelDir"
        }

        val modelConfig = OfflineModelConfig(
            nemo = OfflineNemoEncDecCtcModelConfig(model = modelFile.absolutePath),
            tokens = tokensFile.absolutePath,
            modelType = "nemo_ctc",
        ).copy(numThreads = numThreads, provider = "cpu")

        val config = OfflineRecognizerConfig(
            featConfig = FeatureConfig(sampleRate = 16000, featureDim = 80),
            modelConfig = modelConfig,
        )
        recognizer = OfflineRecognizer(config = config)
    }

    /** Decode one utterance of mono PCM, normalized to [-1, 1]. */
    fun transcribe(samples: FloatArray, sampleRate: Int = 16000): String {
        val rec = checkNotNull(recognizer) { "SttEngine.load() must succeed before transcribe()" }
        val stream: OfflineStream = rec.createStream()
        try {
            stream.acceptWaveform(samples, sampleRate)
            rec.decode(stream)
            return rec.getResult(stream).text
        } finally {
            stream.release()
        }
    }

    fun release() {
        recognizer?.release()
        recognizer = null
    }

    /**
     * True if [modelFile]'s embedded ONNX metadata has the key sherpa-onnx's
     * nemo_ctc loader requires. ONNX metadata_props store each key as its
     * literal UTF-8 bytes in the file's protobuf encoding, so a streaming
     * byte-scan for "vocab_size" finds it (or proves its absence) without
     * parsing the protobuf structure or loading the model's graph/weights -
     * cheap and fast even for a 100+ MB file, unlike constructing a real
     * inference session just to read metadata.
     */
    private fun hasVocabSizeMetadata(modelFile: File): Boolean {
        val needle = "vocab_size".toByteArray(Charsets.US_ASCII)
        val chunkSize = 1 shl 20
        val buffer = ByteArray(chunkSize + needle.size - 1)
        return try {
            modelFile.inputStream().buffered().use { input ->
                var carry = 0
                while (true) {
                    val read = input.read(buffer, carry, chunkSize)
                    if (read <= 0) break
                    val end = carry + read
                    if (containsSequence(buffer, end, needle)) return@use true
                    val keep = minOf(needle.size - 1, end)
                    System.arraycopy(buffer, end - keep, buffer, 0, keep)
                    carry = keep
                }
                false
            }
        } catch (e: Exception) {
            false
        }
    }

    private fun containsSequence(haystack: ByteArray, length: Int, needle: ByteArray): Boolean {
        for (i in 0..length - needle.size) {
            var matched = true
            for (j in needle.indices) {
                if (haystack[i + j] != needle[j]) {
                    matched = false
                    break
                }
            }
            if (matched) return true
        }
        return false
    }
}
