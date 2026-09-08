package com.itantra.app.codec

/**
 * A small, hand-authored table mapping whole operational phrases to a single
 * id, with per-language surface text.
 *
 * A hit costs **two bytes regardless of sentence length**, and it is the one
 * mode that crosses languages: only the id travels, so Tamil spoken in
 * becomes Hindi spoken out with no translation anywhere in the system.
 *
 * ## This is not Tier 1, and must not be presented as Tier 1
 *
 * It is a deliberately minimal stand-in for the semantic frame and codebook
 * system in the full design. It demonstrates the same *mechanism* — meaning
 * becomes a number, the number crosses the link, the receiver reconstructs
 * language-specific text — at a scale that can be hand-authored rather than
 * derived from a corpus. There is no intent model, no slot filling, and no
 * generalisation beyond these exact sentences.
 *
 * ## Ids are append-only
 *
 * An id is a wire contract. Never reuse or renumber one: a reused id does not
 * fail, it decodes into a confidently wrong message — which on this system
 * could mean the wrong emergency at the wrong gate. Retire an entry by
 * leaving its id unused, never by giving it to something else.
 */
object PhraseDictionary {

    /** phraseId -> languageCode -> surface text. */
    private val PHRASES: Map<Int, Map<String, String>> = mapOf(
        1 to mapOf(
            "en-IN" to "fire at north gate",
            "hi-IN" to "उत्तर द्वार पर आग",
            "ta-IN" to "வடக்கு வாசலில் தீ",
        ),
        2 to mapOf(
            "en-IN" to "send help immediately",
            "hi-IN" to "तुरंत मदद भेजो",
            "ta-IN" to "உடனடியாக உதவி அனுப்பவும்",
        ),
        3 to mapOf(
            "en-IN" to "position secured",
            "hi-IN" to "स्थिति सुरक्षित है",
            "ta-IN" to "நிலை பாதுகாக்கப்பட்டது",
        ),
        4 to mapOf(
            "en-IN" to "area clear",
            "hi-IN" to "क्षेत्र सुरक्षित है",
            "ta-IN" to "பகுதி பாதுகாப்பானது",
        ),
    )

    /** Reverse lookup: languageCode -> normalized surface -> phraseId. */
    private val BY_SURFACE: Map<String, Map<String, Int>> = run {
        val byLanguage = mutableMapOf<String, MutableMap<String, Int>>()
        for ((id, surfaces) in PHRASES) {
            for ((language, text) in surfaces) {
                byLanguage.getOrPut(language) { mutableMapOf() }[normalize(text)] = id
            }
        }
        byLanguage
    }

    /** Largest id the 1-byte phraseId field can carry. */
    const val MAX_ID = 255

    /**
     * Compared on a normalized form so that incidental spacing or casing does
     * not miss an otherwise exact hit.
     *
     * This is only used to *find* a candidate id. Whether the candidate is
     * actually used is decided by re-decoding it and requiring an exact match
     * against the original text — see ITantraCodec — so normalisation can
     * never make the round trip lossy.
     */
    private fun normalize(text: String): String =
        text.trim().lowercase().replace(Regex("\\s+"), " ")

    /** The id whose [languageCode] surface matches [text], or null. */
    fun idFor(text: String, languageCode: String): Int? =
        BY_SURFACE[languageCode]?.get(normalize(text))

    /** The surface text for [id] in [languageCode], or null if not authored. */
    fun surfaceFor(id: Int, languageCode: String): String? = PHRASES[id]?.get(languageCode)

    /** Languages in which [id] can be spoken. */
    fun languagesFor(id: Int): Set<String> = PHRASES[id]?.keys ?: emptySet()

    /** Every authored id, for tests and diagnostics. */
    val ids: Set<Int> get() = PHRASES.keys
}
