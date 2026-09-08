package com.itantra.app.codec

/**
 * Per-language character tables — the basis of PACK7.
 *
 * UTF-8 spends three bytes on every Devanagari, Tamil, Bengali or Odia
 * character, because those blocks sit high in the code space. But any one
 * language uses well under 128 distinct characters in practice. Index into a
 * per-language table instead and each character costs **seven bits**:
 *
 * ```
 * 24 bits per character  ->  7 bits per character
 * ```
 *
 * ## These tables are a wire contract
 *
 * A character's index *is* its encoding. Two devices with different tables
 * decode the same payload into different text — silently, and in the right
 * script, which makes it worse than a failure. So:
 *
 * - **Never reorder a table.** Append only, and only while the total stays
 *   within 128.
 * - **Never derive a table from the platform's Unicode data.** They are
 *   written out as explicit ranges here precisely so that two devices on
 *   different Android versions cannot disagree.
 *
 * ## Coverage
 *
 * Each table holds the letters, vowel signs, virama, digits and marks a
 * language actually uses, plus space, danda, and the zero-width joiners that
 * Indic rendering depends on. Anything outside the table causes that whole
 * message to fall back to RAW — always correct, just larger — so a
 * conservative table costs bytes, never correctness.
 *
 * Slot 0 is the space character in every table.
 */
object Alphabets {

    /** Largest table PACK7's 7-bit index can address. */
    const val MAX_ENTRIES = 128

    /** Danda and double danda: sentence punctuation shared across Indic scripts. */
    private val DANDA = 0x0964..0x0965

    /** ZWNJ and ZWJ — significant in Indic clusters, not decoration. */
    private val JOINERS = 0x200C..0x200D

    private fun build(vararg specs: Any): String {
        val sb = StringBuilder()
        sb.append(' ') // slot 0, in every table
        for (spec in specs) {
            when (spec) {
                is IntRange -> for (cp in spec) sb.append(cp.toChar())
                is Int -> sb.append(spec.toChar())
                is String -> sb.append(spec)
                else -> throw IllegalArgumentException("unsupported spec $spec")
            }
        }
        return sb.toString()
    }

    private val DEVANAGARI = build(
        0x0901..0x0903,   // candrabindu, anusvara, visarga
        0x0905..0x0914,   // independent vowels
        0x0915..0x0939,   // consonants
        0x093C..0x094D,   // nukta, avagraha, vowel signs, virama
        0x0958..0x0963,   // nukta consonants, vocalic RR/LL and their signs
        0x0966..0x096F,   // digits
        DANDA, JOINERS,
    )

    private val BENGALI = build(
        0x0981..0x0983,
        0x0985..0x098C, 0x098F..0x0990, 0x0993..0x09A8,
        0x09AA..0x09B0, 0x09B2, 0x09B6..0x09B9,
        0x09BC..0x09CE,   // nukta, avagraha, vowel signs, virama, khanda ta
        0x09DC..0x09DD, 0x09DF..0x09E3,
        0x09E6..0x09EF,
        DANDA, JOINERS,
    )

    private val GUJARATI = build(
        0x0A81..0x0A83,
        0x0A85..0x0A8D, 0x0A8F..0x0A91, 0x0A93..0x0AA8,
        0x0AAA..0x0AB0, 0x0AB2..0x0AB3, 0x0AB5..0x0AB9,
        0x0ABC..0x0ACD,
        0x0AE0..0x0AE3, 0x0AE6..0x0AEF,
        DANDA, JOINERS,
    )

    private val KANNADA = build(
        0x0C82..0x0C83,
        0x0C85..0x0C8C, 0x0C8E..0x0C90, 0x0C92..0x0CA8,
        0x0CAA..0x0CB3, 0x0CB5..0x0CB9,
        0x0CBC..0x0CCD,
        0x0CD5..0x0CD6, 0x0CE0..0x0CE3, 0x0CE6..0x0CEF,
        DANDA, JOINERS,
    )

    private val MALAYALAM = build(
        0x0D02..0x0D03,
        0x0D05..0x0D0C, 0x0D0E..0x0D10, 0x0D12..0x0D3A,
        0x0D3D..0x0D4E,
        0x0D57, 0x0D60..0x0D63, 0x0D66..0x0D6F,
        DANDA, JOINERS,
    )

    private val TAMIL = build(
        0x0B82..0x0B83,
        0x0B85..0x0B8A, 0x0B8E..0x0B90, 0x0B92..0x0B95,
        0x0B99..0x0B9A, 0x0B9C, 0x0B9E..0x0B9F,
        0x0BA3..0x0BA4, 0x0BA8..0x0BAA, 0x0BAE..0x0BB9,
        0x0BBE..0x0BCD,
        0x0BD0, 0x0BD7, 0x0BE6..0x0BEF,
        DANDA, JOINERS,
    )

    private val TELUGU = build(
        0x0C01..0x0C03,
        0x0C05..0x0C0C, 0x0C0E..0x0C10, 0x0C12..0x0C28,
        0x0C2A..0x0C39,
        0x0C3D..0x0C4D,
        0x0C55..0x0C56, 0x0C60..0x0C63, 0x0C66..0x0C6F,
        DANDA, JOINERS,
    )

    private val ODIA = build(
        0x0B01..0x0B03,
        0x0B05..0x0B0C, 0x0B0F..0x0B10, 0x0B13..0x0B28,
        0x0B2A..0x0B30, 0x0B32..0x0B33, 0x0B35..0x0B39,
        0x0B3C..0x0B4D,
        0x0B5C..0x0B5D, 0x0B5F..0x0B63, 0x0B66..0x0B6F,
        DANDA, JOINERS,
    )

    /**
     * English is here for completeness, not for compression: at 7 bits
     * against UTF-8's 8, PACK7 saves an eighth of the text and spends a
     * 2-byte header doing it, so mode selection will usually prefer RAW on
     * short English. That is the correct outcome, not a shortcoming — see
     * ITantraCodec's mode selection.
     */
    private val ENGLISH = build(
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789",
        ".,'?-",
    )

    private val TABLES: Map<String, String> = mapOf(
        "en-IN" to ENGLISH,
        "hi-IN" to DEVANAGARI,
        "mr-IN" to DEVANAGARI,
        "gu-IN" to GUJARATI,
        "kn-IN" to KANNADA,
        "ml-IN" to MALAYALAM,
        "ta-IN" to TAMIL,
        "te-IN" to TELUGU,
        "or-IN" to ODIA,
        "bn-IN" to BENGALI,
    )

    private val INDEXES: Map<String, Map<Char, Int>> =
        TABLES.mapValues { (_, table) ->
            table.withIndex().associate { (index, ch) -> ch to index }
        }

    /** The table for [languageCode], or null if PACK7 does not serve it. */
    fun tableFor(languageCode: String): String? = TABLES[languageCode]

    /** Character → index for [languageCode]. */
    fun indexFor(languageCode: String): Map<Char, Int>? = INDEXES[languageCode]

    /** Every language with a table, for tests. */
    val languages: Set<String> get() = TABLES.keys
}
