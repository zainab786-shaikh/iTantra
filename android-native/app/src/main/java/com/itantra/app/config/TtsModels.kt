package com.itantra.app.config

/**
 * Direct port of src/config/ttsModelTypes.ts + src/config/ttsModels.ts.
 *
 * Only the "vits" model type is represented as a real branch — it is the
 * only TTSModelType this app's registry actually uses (both Piper and the
 * MMS conversions are VITS models). The TS union also names matcha/kokoro/
 * kitten/pocket/zipvoice/supertonic/auto, none of which any entry below
 * uses; they are omitted the same way Phase 2 omitted Dolphin/Whisper from
 * the STT model registry — inert config with no active behavior.
 */
enum class TtsModelType(val value: String) {
    VITS("vits"),
}

/** How a TTS model's files are fetched. Mirrors the TS `TtsModelSource` union. */
sealed class TtsModelSource {
    data class Archive(val url: String) : TtsModelSource()
    data class Files(val baseUrl: String, val files: List<String>) : TtsModelSource()
}

/**
 * A TTS voice this app knows how to run. `id` is the on-disk directory name
 * under the TTS sideload root (see TtsModelManager) — same separation from
 * STT model ids/directories as the source keeps.
 */
data class TtsModelDescriptor(
    val id: String,
    val label: String,
    val modelType: TtsModelType,
    /** Language codes (iTantraPacket.language values) this voice serves. */
    val languages: List<String>,
    /** Approximate installed size, for the UI to warn before a large transfer. */
    val approxMb: Double,
    val source: TtsModelSource,
)

private const val TTS_RELEASE_BASE =
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/"
private const val MMS_HF_BASE =
    "https://huggingface.co/willwade/mms-tts-multilingual-models-onnx/resolve/main/"

/** Piper voices, one per language — packaged sherpa-onnx release archives. */
val PIPER_ENGLISH = TtsModelDescriptor(
    id = "vits-piper-en_US-lessac-medium",
    label = "English voice",
    modelType = TtsModelType.VITS,
    languages = listOf("en-IN"),
    approxMb = 64.1,
    source = TtsModelSource.Archive("${TTS_RELEASE_BASE}vits-piper-en_US-lessac-medium.tar.bz2"),
)

val PIPER_HINDI = TtsModelDescriptor(
    id = "vits-piper-hi_IN-pratham-medium",
    label = "Hindi voice",
    modelType = TtsModelType.VITS,
    languages = listOf("hi-IN"),
    approxMb = 64.1,
    source = TtsModelSource.Archive("${TTS_RELEASE_BASE}vits-piper-hi_IN-pratham-medium.tar.bz2"),
)

val PIPER_MALAYALAM = TtsModelDescriptor(
    id = "vits-piper-ml_IN-arjun-medium",
    label = "Malayalam voice",
    modelType = TtsModelType.VITS,
    languages = listOf("ml-IN"),
    approxMb = 64.1,
    source = TtsModelSource.Archive("${TTS_RELEASE_BASE}vits-piper-ml_IN-arjun-medium.tar.bz2"),
)

/** MMS-VITS voices for the 7 languages Piper does not cover — loose files, no archive. */
val MMS_GUJARATI = TtsModelDescriptor(
    id = "mms-guj",
    label = "Gujarati voice",
    modelType = TtsModelType.VITS,
    languages = listOf("gu-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}guj/", listOf("model.onnx", "tokens.txt")),
)

val MMS_MARATHI = TtsModelDescriptor(
    id = "mms-mar",
    label = "Marathi voice",
    modelType = TtsModelType.VITS,
    languages = listOf("mr-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}mar/", listOf("model.onnx", "tokens.txt")),
)

val MMS_KANNADA = TtsModelDescriptor(
    id = "mms-kan",
    label = "Kannada voice",
    modelType = TtsModelType.VITS,
    languages = listOf("kn-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}kan/", listOf("model.onnx", "tokens.txt")),
)

val MMS_TAMIL = TtsModelDescriptor(
    id = "mms-tam",
    label = "Tamil voice",
    modelType = TtsModelType.VITS,
    languages = listOf("ta-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}tam/", listOf("model.onnx", "tokens.txt")),
)

val MMS_TELUGU = TtsModelDescriptor(
    id = "mms-tel",
    label = "Telugu voice",
    modelType = TtsModelType.VITS,
    languages = listOf("te-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}tel/", listOf("model.onnx", "tokens.txt")),
)

val MMS_ODIA = TtsModelDescriptor(
    id = "mms-ory",
    label = "Odia voice",
    modelType = TtsModelType.VITS,
    languages = listOf("or-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}ory/", listOf("model.onnx", "tokens.txt")),
)

val MMS_BENGALI = TtsModelDescriptor(
    id = "mms-ben",
    label = "Bengali voice",
    modelType = TtsModelType.VITS,
    languages = listOf("bn-IN"),
    approxMb = 114.0,
    source = TtsModelSource.Files("${MMS_HF_BASE}ben/", listOf("model.onnx", "tokens.txt")),
)

/** Registry of every TTS voice this app can run, one per language. Same order as the source. */
val TTS_MODELS: List<TtsModelDescriptor> = listOf(
    PIPER_ENGLISH,
    PIPER_HINDI,
    PIPER_MALAYALAM,
    MMS_GUJARATI,
    MMS_MARATHI,
    MMS_KANNADA,
    MMS_TAMIL,
    MMS_TELUGU,
    MMS_ODIA,
    MMS_BENGALI,
)

/** The single authoritative language -> TTS voice mapping. Same first-match semantics as the source. */
fun resolveTtsModelForLanguage(languageCode: String): TtsModelDescriptor? =
    TTS_MODELS.find { it.languages.contains(languageCode) }
