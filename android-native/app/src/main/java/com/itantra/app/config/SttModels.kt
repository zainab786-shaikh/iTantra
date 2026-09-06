package com.itantra.app.config

/**
 * Direct port of the 10 active decoders in src/config/models.ts — English
 * NeMo CTC Medium plus the nine AI4Bharat IndicConformer languages — i.e.
 * exactly the model mapping the existing RN application uses today.
 *
 * This intentionally omits the Dolphin/Whisper fallback entries that TS
 * file also declares. In src/config/models.ts, resolveModelForLanguage()
 * never matches them for any of these 10 languages (the Indic/English
 * entries are listed first in STT_MODELS and always win the first-match
 * lookup), so those fallbacks carry no behavior for this app. Per the
 * migration's explicit instruction that Dolphin must not be reintroduced,
 * and per MIGRATION_AUDIT.md §D documenting them as dead code for the
 * current mapping, they are not ported here at all — not even as inert
 * config.
 */
data class SttModelDescriptor(
    /** Release asset name minus ".tar.bz2", or on-disk directory name — matches src/core/stt/ModelManager.ts's SIDELOAD_DIR/<id> convention. */
    val id: String,
    val label: String,
    /** sherpa-onnx model family. Always "nemo_ctc" for every language this app currently routes — see SttEngine.kt. */
    val modelType: String,
    /** Language codes this decoder can serve. */
    val languages: List<String>,
    /** Download size, verbatim from models.ts, for parity/reference only — not used for any behavior here. */
    val approxMb: Int,
    val preferInt8: Boolean,
    val numThreads: Int,
)

private const val HF_INDIC_BASE =
    "https://huggingface.co/parismitaglobalsolutions/indicconformer-sherpa-onnx/resolve/main"

/** Mirrors createIndicConformer() in src/config/models.ts. */
private fun indicConformer(langCode: String, langTag: String, name: String) = SttModelDescriptor(
    id = "indicconformer-$langCode",
    label = "IndicConformer ($name)",
    modelType = "nemo_ctc",
    languages = listOf(langTag),
    approxMb = 188,
    preferInt8 = true,
    numThreads = 2,
)

val NEMO_CTC_ENGLISH = SttModelDescriptor(
    id = "sherpa-onnx-nemo-ctc-en-conformer-medium",
    label = "NeMo CTC (English)",
    modelType = "nemo_ctc",
    languages = listOf("en-IN"),
    approxMb = 158,
    preferInt8 = true,
    numThreads = 2,
)

val INDIC_CONFORMER_HINDI = indicConformer("hi", "hi-IN", "Hindi")
val INDIC_CONFORMER_MARATHI = indicConformer("mr", "mr-IN", "Marathi")
val INDIC_CONFORMER_BENGALI = indicConformer("bn", "bn-IN", "Bengali")
val INDIC_CONFORMER_TAMIL = indicConformer("ta", "ta-IN", "Tamil")
val INDIC_CONFORMER_TELUGU = indicConformer("te", "te-IN", "Telugu")
val INDIC_CONFORMER_KANNADA = indicConformer("kn", "kn-IN", "Kannada")
val INDIC_CONFORMER_GUJARATI = indicConformer("gu", "gu-IN", "Gujarati")
val INDIC_CONFORMER_MALAYALAM = indicConformer("ml", "ml-IN", "Malayalam")

// Odia's real config (src/config/models.ts, INDIC_CONFORMER_ODIA) sources
// tokens.txt/model.int8.onnx from a different HF repo
// (OpenVoiceOS/ai4bharat-indicconformer-or-onnx) than the other eight
// languages, but the on-disk id/shape is identical once installed, so the
// descriptor itself is the same shape as the others.
val INDIC_CONFORMER_ODIA = SttModelDescriptor(
    id = "indicconformer-or",
    label = "IndicConformer (Odia)",
    modelType = "nemo_ctc",
    languages = listOf("or-IN"),
    approxMb = 188,
    preferInt8 = true,
    numThreads = 2,
)

/** Same order as STT_MODELS' active (non-Dolphin/Whisper) entries in src/config/models.ts. */
val STT_MODELS: List<SttModelDescriptor> = listOf(
    NEMO_CTC_ENGLISH,
    INDIC_CONFORMER_HINDI,
    INDIC_CONFORMER_MARATHI,
    INDIC_CONFORMER_BENGALI,
    INDIC_CONFORMER_TAMIL,
    INDIC_CONFORMER_TELUGU,
    INDIC_CONFORMER_KANNADA,
    INDIC_CONFORMER_GUJARATI,
    INDIC_CONFORMER_MALAYALAM,
    INDIC_CONFORMER_ODIA,
)

/** The decoder that serves [languageCode], or null if none claims it — same first-match semantics as resolveModelForLanguage() in src/config/models.ts. */
fun resolveModelForLanguage(languageCode: String): SttModelDescriptor? =
    STT_MODELS.find { languageCode in it.languages }
