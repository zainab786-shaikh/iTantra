package com.itantra.app.stt

/** Direct port of SttEngineKind in src/core/types.ts. Which concrete STT implementation is live. */
enum class SttEngineKind {
    SHERPA_ONNX,
    SIMULATED,
    NONE,
}

/** Direct port of SttTranscription in src/core/stt/SttBackend.ts. */
data class SttTranscription(
    val text: String,
    /** Language the decoder actually reported, when it is multilingual. */
    val detectedLanguage: String? = null,
    /**
     * Text exactly as the engine returned it, before repairScript() ran.
     * Diagnostic-only, same as the source.
     */
    val rawText: String? = null,
)

/**
 * Direct port of the SttBackend interface in src/core/stt/SttBackend.ts.
 *
 * A speech recogniser bound to one language. load() is separated from
 * construction because model loading is slow (hundreds of ms to
 * seconds).
 */
interface SttBackend {
    val kind: SttEngineKind

    /** True once a decoder is resident and transcribe() can be called. */
    val isReady: Boolean

    /** Load (or swap to) the decoder for [languageCode]. */
    fun load(languageCode: String)

    /** Decode one utterance of 16 kHz mono audio. */
    fun transcribe(samples: FloatArray, languageCode: String): SttTranscription

    fun dispose()
}
