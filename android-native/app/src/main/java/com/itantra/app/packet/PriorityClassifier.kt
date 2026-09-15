package com.itantra.app.packet

/**
 * Keyword triggers that raise a message to [PacketPriority.CRITICAL].
 *
 * A deliberately transparent, auditable rule set rather than a model: an
 * operator needs to be able to predict exactly why a message was escalated,
 * and a misfire on CRITICAL is expensive.
 *
 * ## This is an interim classifier
 *
 * The specifications define priority as coming from the semantic layer, not
 * from a keyword scan (`packet §11.1`, `language §11.2`):
 *
 * ```
 * CRITICAL  =  intents.bin[intent].is_alert          (automatic)
 *              OR the operator's "Send as Critical"  (manual override)
 * ```
 *
 * `intents.bin` arrives in Phase 6 and is wired through in Phase 11. Until
 * then this table stands in for the `is_alert` flag. Both paths raise
 * priority; **neither can lower it, and the system never lowers it** (§11.1).
 *
 * ## The MEDIUM and HIGH tables are gone
 *
 * They were dropped with the bands themselves — `HIGH` does not exist on the
 * wire, in the API or in the UI (`packet §11`, `receiver §7.4`,
 * `language §11.2`). Their keywords ("urgent", "help", "report", "status",
 * and the per-language equivalents) now classify as [PacketPriority.NORMAL],
 * because NORMAL is the only other state there is. Any of them that genuinely
 * warrants escalation belongs in `intents.bin` with `is_alert` set, which is
 * the decision Phase 6 makes with the codebook in hand — not one to guess at
 * here by promoting a word list.
 *
 * The CRITICAL list below is unchanged: same keywords, same languages, none
 * added, none removed, none reworded.
 */
private val CRITICAL_TRIGGERS: List<String> = listOf(
    "mayday", "emergency", "sos", "critical", "casualty", "fire", "attack",
    // Hindi
    "आपातकाल", "खतरा", "आग", "हमला",
    // Tamil
    "அவசரம்", "ஆபத்து", "தீ", "தாக்குதல்",
    // Marathi
    "आणीबाणी", "धोका", "आग", "हल्ला",
    // Bengali
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
)

/**
 * Classify an utterance. [PacketPriority.CRITICAL] on a keyword hit,
 * [PacketPriority.NORMAL] otherwise — there is no third answer.
 */
fun classifyPriority(text: String): PacketPriority {
    val haystack = text.lowercase()
    return if (CRITICAL_TRIGGERS.any { haystack.contains(it) }) {
        PacketPriority.CRITICAL
    } else {
        PacketPriority.NORMAL
    }
}

/**
 * Presentation colour per band, matching iTantra Design.md's restrained
 * palette (kept as literal hex here rather than importing the UI theme,
 * since core/ intentionally has no dependency on the UI layer, same
 * separation the TS source keeps) — textMuted for NORMAL, critical for
 * CRITICAL. The two intermediate hues went with the two intermediate bands.
 */
val PRIORITY_COLORS: Map<PacketPriority, String> = mapOf(
    PacketPriority.NORMAL to "#B5B5B5",
    PacketPriority.CRITICAL to "#E66B67",
)
