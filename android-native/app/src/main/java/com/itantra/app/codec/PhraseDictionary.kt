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
        // CRITICAL — these pre-empt whatever the receiver is speaking.
        // ---------------------------------------------------------------
        1 to mapOf(
            "en-IN" to "there is a fire at the north gate send immediate assistance",
            "hi-IN" to "उत्तर द्वार पर आग लग गई है तुरंत सहायता भेजें",
            "mr-IN" to "उत्तर दरवाजाजवळ आग लागली आहे त्वरित मदत पाठवा",
            "bn-IN" to "উত্তর গেটে আগুন লেগেছে এখনই সাহায্য পাঠান",
        ),
        5 to mapOf(
            "en-IN" to "we have an emergency at the main checkpoint respond immediately",
            "hi-IN" to "मुख्य चौकी पर आपातकाल है तुरंत प्रतिक्रिया दें",
            "mr-IN" to "मुख्य चौकीवर आणीबाणी आहे त्वरित प्रतिसाद द्या",
            "bn-IN" to "প্রধান চেকপোস্টে জরুরি অবস্থা এখনই সাড়া দিন",
        ),
        6 to mapOf(
            "en-IN" to "our position is under attack request immediate support",
            "hi-IN" to "हमारी चौकी पर हमला हो रहा है तुरंत सहायता चाहिए",
            "mr-IN" to "आमच्या ठिकाणावर हल्ला होत आहे त्वरित मदत हवी",
            "bn-IN" to "আমাদের অবস্থানে হামলা হচ্ছে এখনই সহায়তা দরকার",
        ),

        // ---------------------------------------------------------------
        // HIGH
        // ---------------------------------------------------------------
        2 to mapOf(
            "en-IN" to "send help immediately to our current location",
            "hi-IN" to "हमारे वर्तमान स्थान पर तुरंत मदद भेजें",
            "mr-IN" to "आमच्या सध्याच्या ठिकाणी त्वरित मदत पाठवा",
            "bn-IN" to "আমাদের বর্তমান অবস্থানে এখনই সাহায্য পাঠান",
        ),
        7 to mapOf(
            "en-IN" to "requesting backup at our location as soon as possible",
            "hi-IN" to "हमारे स्थान पर जल्द से जल्द सहायता भेजी जाए",
            "mr-IN" to "आमच्या ठिकाणी लवकरात लवकर मदत पाठवावी",
            "bn-IN" to "আমাদের অবস্থানে যত দ্রুত সম্ভব সাহায্য পাঠান",
        ),
        8 to mapOf(
            "en-IN" to "we need a medic at this location right away",
            "hi-IN" to "इस स्थान पर तुरंत चिकित्सा सहायता की आवश्यकता है",
            "mr-IN" to "या ठिकाणी त्वरित वैद्यकीय मदत आवश्यक आहे",
            "bn-IN" to "এই অবস্থানে এখনই চিকিৎসা সাহায্য প্রয়োজন",
        ),
        9 to mapOf(
            "en-IN" to "evacuate the area immediately and move to safety",
            "hi-IN" to "इस क्षेत्र को तुरंत खाली करें और सुरक्षित स्थान पर जाएं",
            "mr-IN" to "हा परिसर त्वरित रिकामा करा आणि सुरक्षित ठिकाणी जा",
            "bn-IN" to "এই এলাকা তাড়াতাড়ি খালি করুন এবং নিরাপদ স্থানে যান",
        ),
        10 to mapOf(
            "en-IN" to "we have an injured man down and need assistance",
            "hi-IN" to "एक सैनिक घायल हो गया है सहायता की आवश्यकता है",
            "mr-IN" to "एक सैनिक जखमी झाला आहे मदत आवश्यक आहे",
            "bn-IN" to "একজন সৈনিক আহত হয়েছে সহায়তা প্রয়োজন",
        ),

        // ---------------------------------------------------------------
        // MEDIUM
        // ---------------------------------------------------------------
        3 to mapOf(
            "en-IN" to "our position is secured and we are holding steady",
            "hi-IN" to "हमारी स्थिति सुरक्षित है और हम डटे हुए हैं",
            "mr-IN" to "आमची स्थिती सुरक्षित आहे आणि आम्ही तैनात आहोत",
            "bn-IN" to "আমাদের অবস্থান সুরক্ষিত এবং আমরা প্রস্তুত আছি",
        ),
        11 to mapOf(
            "en-IN" to "all units report your current status over",
            "hi-IN" to "सभी इकाइयां अपनी वर्तमान स्थिति की रिपोर्ट दें",
            "mr-IN" to "सर्व पथकांनी आपली सध्याची स्थिती कळवावी",
            "bn-IN" to "সকল ইউনিট আপনাদের বর্তমান অবস্থা রিপোর্ট করুন",
        ),
        12 to mapOf(
            "en-IN" to "hold your position and await further instructions",
            "hi-IN" to "अपनी स्थिति बनाए रखें और अगले निर्देश की प्रतीक्षा करें",
            "mr-IN" to "आपली स्थिती कायम ठेवा आणि पुढील सूचनांची वाट पहा",
            "bn-IN" to "আপনার অবস্থান ধরে রাখুন এবং পরবর্তী নির্দেশের অপেক্ষা করুন",
        ),
        13 to mapOf(
            "en-IN" to "we are moving to the secondary position now",
            "hi-IN" to "हम दूसरी स्थिति की ओर बढ़ रहे हैं",
            "mr-IN" to "आम्ही दुसऱ्या स्थितीकडे जात आहोत",
            "bn-IN" to "আমরা দ্বিতীয় অবস্থানে সরে যাচ্ছি",
        ),

        // ---------------------------------------------------------------
        // NORMAL
        // ---------------------------------------------------------------
        4 to mapOf(
            "en-IN" to "the area is clear and all is quiet here",
            "hi-IN" to "क्षेत्र पूरी तरह सुरक्षित है और सब कुछ शांत है",
            "mr-IN" to "परिसर पूर्णपणे मोकळा आहे आणि सर्व काही शांत आहे",
            "bn-IN" to "এলাকা সম্পূর্ণ নিরাপদ এবং সব কিছু শান্ত",
        ),
        14 to mapOf(
            "en-IN" to "your message has been received loud and clear",
            "hi-IN" to "आपका संदेश स्पष्ट रूप से प्राप्त हो गया है",
            "mr-IN" to "तुमचा संदेश स्पष्टपणे मिळाला आहे",
            "bn-IN" to "আপনার বার্তা স্পষ্টভাবে পৌঁছেছে",
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
