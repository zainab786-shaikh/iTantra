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
 * generalisation beyond these exact sentences. Say a fifteenth thing and it
 * falls back to PACK7 like anything else.
 *
 * ## Ids are append-only
 *
 * An id is a wire contract. Never reuse or renumber one: a reused id does not
 * fail, it decodes into a confidently wrong message — which on this system
 * could mean the wrong emergency at the wrong gate. Retire an entry by
 * leaving its id unused, never by giving it to something else. Adding a new
 * *language* to an existing id is safe; it does not change what the id means.
 *
 * ## Every surface for an id must classify to the same priority band
 *
 * Priority is derived from the sender's plaintext before encoding (see
 * `packet.classifyPriority`), so a phrase whose English surface says "under
 * attack" and whose Hindi surface lacks any CRITICAL keyword would be sent
 * as CRITICAL by one operator and NORMAL by another — the same message,
 * banded differently depending on who spoke it. Each entry below is written
 * so that every language hits its intended band. `PhraseDictionaryTest`
 * enforces this; it is not a style rule.
 */
object PhraseDictionary {

    /**
     * phraseId -> languageCode -> surface text.
     *
     * Surfaces are lowercase and unpunctuated because that is what the CTC
     * decoders actually emit. A surface that does not match what STT produces
     * simply never gets selected — PHRASE is only used when re-decoding it
     * reproduces the operator's words exactly.
     */
    private val PHRASES: Map<Int, Map<String, String>> = mapOf(
        // ---------------------------------------------------------------
        // CRITICAL
        // ---------------------------------------------------------------
        1 to mapOf(
            "en-IN" to "fire at north gate",
            "hi-IN" to "उत्तर द्वार पर आग",
            "ta-IN" to "வடக்கு வாசலில் தீ",
            "mr-IN" to "उत्तर दरवाजाला आग",
            "bn-IN" to "উত্তর গেটে আগুন",
        ),
        5 to mapOf(
            "en-IN" to "emergency at checkpoint",
            "hi-IN" to "चौकी पर आपातकाल",
            "ta-IN" to "சோதனைச் சாவடியில் அவசரம்",
            "mr-IN" to "चौकीवर आणीबाणी",
            "bn-IN" to "চেকপোস্টে জরুরি অবস্থা",
        ),
        6 to mapOf(
            "en-IN" to "under attack",
            "hi-IN" to "हम पर हमला हुआ है",
            "ta-IN" to "தாக்குதல் நடக்கிறது",
            "mr-IN" to "आमच्यावर हल्ला झाला",
            "bn-IN" to "আমরা হামলার শিকার",
        ),

        // ---------------------------------------------------------------
        // HIGH
        // ---------------------------------------------------------------
        2 to mapOf(
            "en-IN" to "send help immediately",
            "hi-IN" to "तुरंत मदद भेजो",
            "ta-IN" to "உடனடியாக உதவி அனுப்பவும்",
            "mr-IN" to "त्वरित मदत पाठवा",
            "bn-IN" to "এখনই সাহায্য পাঠান",
        ),
        7 to mapOf(
            "en-IN" to "requesting backup",
            "hi-IN" to "सहायता भेजो",
            "ta-IN" to "உதவி தேவை",
            "mr-IN" to "मदत हवी आहे",
            "bn-IN" to "সাহায্য প্রয়োজন",
        ),
        8 to mapOf(
            "en-IN" to "medic required",
            "hi-IN" to "चिकित्सा सहायता चाहिए",
            "ta-IN" to "மருத்துவ உதவி தேவை",
            "mr-IN" to "वैद्यकीय मदत हवी",
            "bn-IN" to "চিকিৎসা সাহায্য দরকার",
        ),
        9 to mapOf(
            "en-IN" to "evacuate immediately",
            "hi-IN" to "तुरंत खाली करो",
            "ta-IN" to "உடனடியாக வெளியேறவும்",
            "mr-IN" to "त्वरित रिकामे करा",
            "bn-IN" to "তাড়াতাড়ি সরে যান",
        ),
        10 to mapOf(
            "en-IN" to "injured man down",
            "hi-IN" to "घायल सैनिक गिरा है",
            // "காயம் அடைந்தார்", not the compound "காயமடைந்தவர்": compounding
            // absorbs the virama on ம, so the HIGH keyword "காயம்" stops being
            // a substring and this phrase alone would have gone out as NORMAL
            // from a Tamil operator while every other language sent HIGH.
            "ta-IN" to "வீரர் காயம் அடைந்தார்",
            "mr-IN" to "जखमी सैनिक पडला आहे",
            "bn-IN" to "আহত সৈনিক পড়ে গেছে",
        ),

        // ---------------------------------------------------------------
        // MEDIUM
        // ---------------------------------------------------------------
        3 to mapOf(
            "en-IN" to "position secured",
            "hi-IN" to "स्थिति सुरक्षित है",
            "ta-IN" to "நிலை பாதுகாக்கப்பட்டது",
            "mr-IN" to "स्थिती सुरक्षित आहे",
            "bn-IN" to "অবস্থান সুরক্ষিত",
        ),
        11 to mapOf(
            "en-IN" to "report status",
            "hi-IN" to "स्थिति की रिपोर्ट दो",
            "ta-IN" to "நிலை அறிக்கை தேவை",
            "mr-IN" to "स्थिती अहवाल द्या",
            "bn-IN" to "অবস্থা রিপোর্ট করুন",
        ),
        12 to mapOf(
            "en-IN" to "hold position",
            "hi-IN" to "स्थिति बनाए रखो",
            "ta-IN" to "நிலையை தக்கவைக்கவும்",
            "mr-IN" to "स्थिती कायम ठेवा",
            "bn-IN" to "অবস্থান ধরে রাখুন",
        ),
        13 to mapOf(
            "en-IN" to "moving to secondary position",
            "hi-IN" to "दूसरी स्थिति की ओर बढ़ रहे हैं",
            "ta-IN" to "இரண்டாம் நிலைக்கு நகர்கிறோம்",
            "mr-IN" to "दुसऱ्या स्थितीकडे जात आहोत",
            "bn-IN" to "দ্বিতীয় অবস্থানে যাচ্ছি",
        ),

        // ---------------------------------------------------------------
        // NORMAL
        // ---------------------------------------------------------------
        4 to mapOf(
            "en-IN" to "area clear",
            "hi-IN" to "क्षेत्र सुरक्षित है",
            "ta-IN" to "பகுதி பாதுகாப்பானது",
            "mr-IN" to "परिसर मोकळा आहे",
            "bn-IN" to "এলাকা নিরাপদ",
        ),
        14 to mapOf(
            "en-IN" to "message received",
            "hi-IN" to "संदेश मिल गया",
            "ta-IN" to "செய்தி கிடைத்தது",
            "mr-IN" to "संदेश मिळाला",
            "bn-IN" to "বার্তা পেয়েছি",
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

    /** Every (id, language, surface) triple, for tests and diagnostics. */
    fun entries(): List<Triple<Int, String, String>> =
        PHRASES.flatMap { (id, surfaces) -> surfaces.map { (lang, text) -> Triple(id, lang, text) } }
}
