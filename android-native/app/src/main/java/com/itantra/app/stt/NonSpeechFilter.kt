package com.itantra.app.stt

/**
 * Direct port of src/core/stt/hallucinations.ts.
 *
 * Filters non-speech output some decoders produce with high confidence
 * when handed audio containing no real speech (subtitle-trained models
 * answer silence with "[Music]", "[BLANK_AUDIO]", "Thanks for watching!",
 * etc.). Without this filter such output becomes a packet and gets
 * transmitted as if someone had spoken it.
 *
 * The VAD/segmenter already rejects most silence; this catches what
 * survives it, such as background noise loud enough to look like speech.
 * Ported and kept even though the model currently in use (NeMo CTC /
 * AI4Bharat IndicConformer) is not known to produce these specific
 * subtitle artifacts — the RN source runs this filter unconditionally on
 * every transcript, and this migration must not silently drop that
 * safety net.
 */
object NonSpeechFilter {

    /**
     * Literal phrases decoders can emit for non-speech audio. Compared
     * case-insensitively after stripping punctuation and whitespace.
     */
    private val ARTIFACT_PHRASES: Set<String> = setOf(
        "thank you",
        "thanks for watching",
        "thank you for watching",
        "thanks for watching!",
        "bye",
        "bye bye",
        "you",
        "so",
        "okay",
        "oh",
        "hmm",
        "mm",
        "uh",
        "the",
        "subtitles by the amara org community",
        "subs by www zeoranger co uk",
        "transcription by castingwords",
    )

    /**
     * Text made up entirely of a bracketed or parenthesised tag —
     * "[Music]", "(applause)", "♪♪♪" — is a subtitle annotation, never
     * speech.
     */
    private val FULLY_TAGGED = Regex("^[\\s]*[\\[({【♪*][^\\])】]*[\\])】♪*][\\s.!?]*$")

    /** Strip punctuation and collapse whitespace, for phrase comparison. */
    private fun normalize(text: String): String =
        text.lowercase()
            .replace(Regex("[.,!?¡¿;:\"'`´“”‘’\\-_/\\\\]"), " ")
            .replace(Regex("\\s+"), " ")
            .trim()

    /**
     * True when [text] is a subtitle artifact rather than a transcription.
     *
     * Deliberately conservative about short real words: a lone "so" or
     * "you" is discarded, which can drop a genuine one-word reply, but
     * transmitting a hallucinated packet is the worse failure for an
     * operational tool.
     */
    fun isNonSpeechArtifact(text: String): Boolean {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return true

        if (FULLY_TAGGED.matches(trimmed)) return true

        val normalized = normalize(trimmed)
        if (normalized.isEmpty()) return true
        if (ARTIFACT_PHRASES.contains(normalized)) return true

        // A single repeated token ("you you you you") is a decoder loop, not speech.
        val words = normalized.split(" ")
        if (words.size >= 4 && words.toSet().size == 1) return true

        return false
    }
}
