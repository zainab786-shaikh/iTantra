package com.itantra.app.stt

import com.itantra.app.config.resolveModelForLanguage
import java.io.File

/**
 * Direct port of src/core/stt/SherpaSttBackend.ts, adapted to call the
 * direct sherpa-onnx integration (SttEngine, Phase 2) instead of the RN
 * TurboModule bridge.
 *
 * Responsibilities preserved from the source:
 *  - resolve languageCode -> model descriptor -> on-disk model directory
 *    (side-load layout only: <filesDir>/itantra-models/<modelId>/,
 *    exactly what ModelManager.ts's findSideloaded() checks — the actual
 *    HTTP download path in ModelManager.ts is out of scope for this
 *    phase, which ports STT pipeline behavior, not networking);
 *  - cache the loaded engine by model id so repeated utterances in the
 *    same language do not pay a reload; rebuild only when the language
 *    (and therefore the model) actually changes;
 *  - call repairScript() (IndicScriptRepair) on every result, unconditionally,
 *    same as the source.
 *
 * Not ported from the source: the Whisper-specific modelOptions branch
 * (this app's Kotlin SttEngine only implements nemo_ctc, matching what
 * every one of the 10 active languages in src/config/models.ts actually
 * uses) and the __DEV__-only self-test method (a development convenience,
 * not pipeline behavior — this migration's on-device diagnostic probes
 * already serve that verification purpose).
 */
class SherpaSttBackend(private val modelsRootDir: File) : SttBackend {
    override val kind = SttEngineKind.SHERPA_ONNX

    private val engine = SttEngine()
    private var loadedModelId: String? = null

    override val isReady: Boolean
        get() = engine.isReady

    @Synchronized
    override fun load(languageCode: String) {
        val descriptor = resolveModelForLanguage(languageCode)
            ?: throw IllegalStateException("No STT model registered for language \"$languageCode\"")

        if (loadedModelId == descriptor.id && engine.isReady) return

        val modelDir = File(modelsRootDir, descriptor.id)
        if (!modelDir.exists() || !hasOnnxFile(modelDir)) {
            throw IllegalStateException(
                "The \"${descriptor.label}\" model is not installed on this device yet " +
                    "(expected at ${modelDir.absolutePath})."
            )
        }

        engine.release()
        loadedModelId = null

        engine.load(modelDir.absolutePath, numThreads = descriptor.numThreads)
        loadedModelId = descriptor.id
    }

    @Synchronized
    override fun transcribe(samples: FloatArray, languageCode: String): SttTranscription {
        load(languageCode)
        check(engine.isReady) { "STT engine unavailable" }

        val raw = engine.transcribe(samples).trim()
        val repair = IndicScriptRepair.repairScript(raw, languageCode)

        return SttTranscription(
            text = repair.text,
            rawText = raw,
        )
    }

    @Synchronized
    override fun dispose() {
        engine.release()
        loadedModelId = null
    }

    private fun hasOnnxFile(dir: File): Boolean =
        dir.listFiles()?.any { it.name.endsWith(".onnx") } == true
}
