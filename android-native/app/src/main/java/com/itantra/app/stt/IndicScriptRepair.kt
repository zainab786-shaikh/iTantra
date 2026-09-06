package com.itantra.app.stt

/**
 * Direct port of src/core/stt/indicScript.ts.
 *
 * Repairs script confusion between Indian languages: a multilingual
 * decoder that identifies language itself (Dolphin, historically) can
 * regularly decode the right *sounds* into the wrong *script* — Bengali
 * speech came back on device as "अपनार ओबस्थान जान", phonetically
 * "apnar obosthan janan", correct word for word, written in Devanagari
 * instead of Bengali.
 *
 * That is recoverable. The Unicode Indic blocks are deliberately
 * parallel: all of them descend from ISCII and place the same phoneme at
 * the same offset within their block, so converting between them is an
 * arithmetic shift of the code point. Transliterating is therefore
 * lossless for the shared letters and far better than discarding the
 * text.
 *
 * Output in a script with no such correspondence — Arabic, Cyrillic,
 * Han — is not repairable and is rejected instead, so the operator sees
 * nothing rather than a fragment.
 *
 * Kept and ported even though the models this app currently routes to
 * (NeMo CTC, AI4Bharat IndicConformer) do not exhibit this specific
 * failure mode — the RN source runs this unconditionally on every
 * result regardless of which model produced it, and this migration must
 * not silently drop a safety net just because the currently-selected
 * models don't currently need it.
 */
object IndicScriptRepair {

    /** Start of each Indic block, which is also its transliteration origin. */
    private val BLOCKS: Map<String, Int> = mapOf(
        "devanagari" to 0x0900,
        "bengali" to 0x0980,
        "gujarati" to 0x0a80,
        "oriya" to 0x0b00,
        "tamil" to 0x0b80,
        "telugu" to 0x0c00,
        "kannada" to 0x0c80,
        "malayalam" to 0x0d00,
    )

    private const val BLOCK_SIZE = 0x80

    /** The script each language is written in. */
    private val LANGUAGE_SCRIPT: Map<String, String> = mapOf(
        "hi-IN" to "devanagari",
        "mr-IN" to "devanagari",
        "bn-IN" to "bengali",
        "gu-IN" to "gujarati",
        "ta-IN" to "tamil",
        "te-IN" to "telugu",
        "kn-IN" to "kannada",
        "ml-IN" to "malayalam",
        "or-IN" to "oriya",
    )

    /** Which Indic block a code point belongs to, or null. */
    private fun blockOf(code: Int): String? {
        for ((name, start) in BLOCKS) {
            if (code >= start && code < start + BLOCK_SIZE) return name
        }
        return null
    }

    private fun isLatinOrPunctuation(code: Int): Boolean =
        code <= 0x007f ||
            (code in 0x00a0..0x024f) ||
            (code in 0x2010..0x2027)

    data class ScriptRepair(
        val text: String,
        /** True when the output could not be salvaged and should be discarded. */
        val rejected: Boolean,
        /** Set when characters were shifted from another Indic script. */
        val transliteratedFrom: String? = null,
    )

    /**
     * Coerce [text] into the script [languageCode] is written in.
     *
     * Latin is always preserved: code-switching into English ("sector
     * four", "over") is normal in Indian radio traffic.
     */
    fun repairScript(text: String, languageCode: String): ScriptRepair {
        val target = LANGUAGE_SCRIPT[languageCode] ?: return ScriptRepair(text, rejected = false)
        val targetStart = BLOCKS.getValue(target)

        // Tally which scripts the text is actually written in.
        val counts = mutableMapOf<String, Int>()
        var foreign = 0
        var letters = 0

        for (codePoint in text.codePoints().toArray()) {
            if (isLatinOrPunctuation(codePoint)) continue
            letters++
            val block = blockOf(codePoint)
            if (block != null) counts[block] = (counts[block] ?: 0) + 1 else foreign++
        }

        // Mostly Arabic/Cyrillic/Han: the language ID was badly wrong and
        // there is no correspondence to exploit. Emitting a stripped
        // fragment would be worse than admitting the failure.
        if (letters > 0 && foreign.toDouble() / letters > 0.4) {
            return ScriptRepair(text = "", rejected = true)
        }

        // Pick the dominant Indic block actually present.
        var dominant: String? = null
        var best = 0
        for ((block, n) in counts) {
            if (n > best) {
                best = n
                dominant = block
            }
        }

        val out = StringBuilder()
        for (codePoint in text.codePoints().toArray()) {
            if (isLatinOrPunctuation(codePoint)) {
                out.appendCodePoint(codePoint)
                continue
            }

            val block = blockOf(codePoint) ?: continue // drop the residual foreign characters

            if (block == target) {
                out.appendCodePoint(codePoint)
                continue
            }

            // Shift by the difference between block origins: the same
            // offset within each block denotes the same phoneme.
            val shifted = codePoint - BLOCKS.getValue(block) + targetStart
            out.appendCodePoint(shifted)
        }

        val repaired = out.toString().replace(Regex("\\s{2,}"), " ").trim()

        return ScriptRepair(
            text = repaired,
            rejected = repaired.isEmpty() && text.trim().isNotEmpty(),
            transliteratedFrom = if (dominant != null && dominant != target) dominant else null,
        )
    }
}
