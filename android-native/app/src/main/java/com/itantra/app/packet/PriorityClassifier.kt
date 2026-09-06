package com.itantra.app.packet

/**
 * Direct port of src/core/packet/priority.ts.
 *
 * Keyword triggers that raise a packet's priority band, per language.
 * This is a deliberately transparent, auditable rule set rather than a
 * model: an operator needs to be able to predict exactly why a message
 * was escalated, and a misfire on a CRITICAL band is expensive. Every
 * keyword below is copied verbatim from the TS source, in the same
 * language groupings — none added, none removed, none reworded.
 */
private val TRIGGERS: Map<PacketPriority, List<String>> = mapOf(
    PacketPriority.CRITICAL to listOf(
        "mayday", "emergency", "sos", "critical", "casualty", "fire", "attack",
        // Hindi
        "आपातकाल", "खतरा", "आग", "हमला",
        // Tamil
        "அவசரம்", "ஆபத்து", "தீ", "தாக்குதல்",
        // Marathi
        "आणीबाणी", "धोका", "आग", "हल्ला",
        // Bengali. The two-word form is listed above the bare "জরুরি" in HIGH, and
        // CRITICAL is tested first, so "emergency" outranks plain "urgent".
        "জরুরি অবস্থা", "আগুন", "বিপদ", "হামলা",
        // Telugu
        "అత్యవసర", "మంటలు", "ప్రమాదం", "దాడి",
        // Kannada
        "ಬೆಂಕಿ", "ಅಪಾಯ", "ದಾಳಿ",
        // Gujarati
        "કટોકટી", "આગ", "જોખમ", "હુમલો",
        // Malayalam
        "അപകടം", "തീ", "ആക്രമണം", "അടിയന്തിരം",
        // Odia
        "ନିଆଁ", "ବିପଦ", "ଆକ୍ରମଣ",
    ),
    PacketPriority.HIGH to listOf(
        "urgent", "immediate", "backup", "medic", "injured", "help", "breach",
        // Hindi
        "तुरंत", "सहायता", "घायल", "मदद",
        // Tamil
        "உடனடி", "உதவி", "காயம்",
        // Marathi
        "त्वरित", "मदत", "जखमी",
        // Bengali
        "সাহায্য", "আহত", "জরুরি", "তাড়াতাড়ি",
        // Telugu
        "సహాయం", "గాయ", "తక్షణ",
        // Kannada
        "ಸಹಾಯ", "ಗಾಯ", "ತಕ್ಷಣ", "ತುರ್ತು",
        // Gujarati
        "મદદ", "ઘાયલ", "તાત્કાલિક",
        // Malayalam
        "സഹായം", "ഉടനടി", "പരിക്ക്",
        // Odia
        "ସାହାଯ୍ୟ", "ତୁରନ୍ତ", "ଆହତ",
    ),
    PacketPriority.MEDIUM to listOf(
        "request", "report", "status", "confirm", "move", "position",
        // Hindi
        "रिपोर्ट", "स्थिति", "पुष्टि",
        // Tamil
        "அறிக்கை", "நிலை",
        // Marathi
        "अहवाल", "स्थिती",
        // Bengali
        "রিপোর্ট", "অবস্থা", "অবস্থান",
        // Telugu
        "నివేదిక", "స్థితి",
        // Kannada
        "ವರದಿ", "ಸ್ಥಿತಿ",
        // Gujarati
        "અહેવાલ", "સ્થિતિ",
        // Malayalam
        "റിപ്പോർട്ട്", "സ്ഥിതി",
        // Odia
        "ରିପୋର୍ଟ", "ସ୍ଥିତି",
    ),
    PacketPriority.NORMAL to emptyList(),
)

/** Bands in descending order; the first match wins. */
private val ORDER: List<PacketPriority> = listOf(PacketPriority.CRITICAL, PacketPriority.HIGH, PacketPriority.MEDIUM)

/**
 * Classify an utterance into a priority band by keyword match.
 * Defaults to NORMAL when nothing matches.
 */
fun classifyPriority(text: String): PacketPriority {
    val haystack = text.lowercase()
    for (band in ORDER) {
        if (TRIGGERS.getValue(band).any { haystack.contains(it) }) return band
    }
    return PacketPriority.NORMAL
}

/**
 * Presentation colour per band, matching iTantra Design.md's restrained
 * palette (kept as literal hex here rather than importing the UI theme,
 * since core/ intentionally has no dependency on the UI layer, same
 * separation the TS source keeps) — textMuted / info / warning /
 * critical, an escalating ladder rather than four unrelated saturated
 * hues.
 */
val PRIORITY_COLORS: Map<PacketPriority, String> = mapOf(
    PacketPriority.NORMAL to "#B5B5B5",
    PacketPriority.MEDIUM to "#72A9D8",
    PacketPriority.HIGH to "#E5B85C",
    PacketPriority.CRITICAL to "#E66B67",
)
