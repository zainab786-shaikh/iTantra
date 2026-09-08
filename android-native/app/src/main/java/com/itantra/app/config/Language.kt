package com.itantra.app.config

/**
 * Direct port of src/config/languages.ts — one entry per language the
 * transmitter can decode. Every value (code/label/native/short/accent) is
 * copied verbatim from the TypeScript source, in the same order; nothing
 * invented, reordered, or renamed.
 */
data class Language(
    /** BCP-47-ish code carried in iTantraPacket.language. */
    val code: String,
    /** English label. */
    val label: String,
    /** Native-script label. */
    val native: String,
    /** Language token understood by multilingual sherpa-onnx models. */
    val sherpaLang: String,
    /** Two-letter tag. */
    val short: String,
    /** Accent colour used by the UI for this language. */
    val accent: String,
)

val LANGUAGES: List<Language> = listOf(
    Language(code = "en-IN", label = "English", native = "English", sherpaLang = "en", short = "EN", accent = "#5EEAD4"),
    Language(code = "hi-IN", label = "Hindi", native = "हिन्दी", sherpaLang = "hi", short = "HI", accent = "#FDBA74"),
    Language(code = "mr-IN", label = "Marathi", native = "मराठी", sherpaLang = "mr", short = "MR", accent = "#C4B5FD"),
    Language(code = "gu-IN", label = "Gujarati", native = "ગુજરાતી", sherpaLang = "gu", short = "GU", accent = "#FDE68A"),
    Language(code = "kn-IN", label = "Kannada", native = "ಕನ್ನಡ", sherpaLang = "kn", short = "KN", accent = "#FCA5A5"),
    Language(code = "ml-IN", label = "Malayalam", native = "മലയാളം", sherpaLang = "ml", short = "ML", accent = "#A5B4FC"),
    Language(code = "ta-IN", label = "Tamil", native = "தமிழ்", sherpaLang = "ta", short = "TA", accent = "#F9A8D4"),
    Language(code = "te-IN", label = "Telugu", native = "తెలుగు", sherpaLang = "te", short = "TE", accent = "#BEF264"),
    Language(code = "or-IN", label = "Odia", native = "ଓଡ଼ିଆ", sherpaLang = "or", short = "OD", accent = "#6EE7B7"),
    Language(code = "bn-IN", label = "Bengali", native = "বাংলা", sherpaLang = "bn", short = "BN", accent = "#7DD3FC"),
)

val DEFAULT_LANGUAGE: Language = LANGUAGES.first()

fun findLanguage(code: String): Language =
    LANGUAGES.find { it.code == code } ?: DEFAULT_LANGUAGE

// ---------------------------------------------------------------------------
// Wire contract.
//
// A language travels over the air as its *index into LANGUAGES* (6 bits, see
// transport/PacketCodec.kt), not as its BCP-47 string — a 1-byte field instead
// of a 5-byte one, on a link where the whole payload can be 2 bytes.
//
// That makes the ORDER OF THIS LIST part of the wire format. Appending a new
// language is safe. Reordering, inserting, or removing an entry is not: two
// devices on different builds would then disagree about what "3" means, and a
// packet would decode into confidently wrong text in the wrong script.
// ---------------------------------------------------------------------------

/** Wire id for [code] — its index in [LANGUAGES]. Falls back to the default language's id. */
fun languageWireId(code: String): Int {
    val index = LANGUAGES.indexOfFirst { it.code == code }
    return if (index >= 0) index else LANGUAGES.indexOf(DEFAULT_LANGUAGE)
}

/** Inverse of [languageWireId]. Returns null for an id this build does not know. */
fun languageFromWireId(id: Int): Language? = LANGUAGES.getOrNull(id)
