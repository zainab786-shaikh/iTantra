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
}
