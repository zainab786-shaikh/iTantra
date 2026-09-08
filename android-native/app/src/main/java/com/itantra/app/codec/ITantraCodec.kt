package com.itantra.app.codec

import com.itantra.app.config.languageFromWireId
import com.itantra.app.config.languageWireId

/**
 * The demo codec: three modes, and a selector that always picks the smallest
 * one that reproduces the text exactly.
 *
 * ## Payload layouts
 *
 * ```
 * RAW     [ utf-8 bytes ]
 * PACK7   [ langId u8 ][ charCount u8 ][ 7 bits per character ... ]
 * PHRASE  [ langId u8 ][ phraseId u8 ]
 * ```
 *
 * The mode is **not** in any of them — it rides in the packet header, because
 * the decoder has to know it before it can start (see [CodecMode]).
 *
 * ## Why this is Kotlin and not C++
 *
 * The final design calls for a C++17 semantic core behind JNI, and that
 * remains right for the real system. It is wrong here: there is no native
 * build in this project at all (the `.so` files are extracted from prebuilt
 * AARs, nothing is compiled), and most of this file is throwaway — PACK7 is
 * replaced by subwords plus arithmetic coding, [PhraseDictionary] by intents
 * and slots. Only [BitWriter] and the [TextCodec] interface survive.
 * Performance is not a factor: one encode per sentence, against a 300-700 ms
 * endpointer.
 */
class ITantraCodec : TextCodec {

    /** PACK7's `charCount` is one byte, so this is the longest it can carry. */
    private val maxPack7Chars = 255

    override fun encode(text: String, languageCode: String): EncodedPayload {
        val originalBytes = text.toByteArray(Charsets.UTF_8).size

        // Encode every mode that applies and keep the smallest. This is a
        // correctness guard, not an optimisation: PACK7 carries a two-byte
        // header, and on already-compact Latin text that header can outweigh
        // the saving and produce a payload LARGER than the input. Selecting
        // by rule would risk showing the payload growing, on camera.
        //
        // Every candidate is also verified by decoding it back and requiring
        // an exact match, so "the text is reconstructed exactly" holds for
        // whichever mode wins, unconditionally.
        val candidates = ArrayList<EncodedPayload>(3)

        val raw = EncodedPayload(text.toByteArray(Charsets.UTF_8), CodecMode.RAW, originalBytes)
        candidates.add(raw)

        encodePack7(text, languageCode)
            ?.let { EncodedPayload(it, CodecMode.PACK7, originalBytes) }
            ?.takeIf { reproducesExactly(it, text, languageCode) }
            ?.let(candidates::add)

        encodePhrase(text, languageCode)
            ?.let { EncodedPayload(it, CodecMode.PHRASE, originalBytes) }
            ?.takeIf { reproducesExactly(it, text, languageCode) }
            ?.let(candidates::add)

        // Smallest wins; a tie goes to the lower mode number, so the simplest
        // representation is preferred when nothing is gained by cleverness.
        return candidates.minWith(
            compareBy({ it.bytes.size }, { it.mode.wire })
        )
    }

    override fun decode(
        mode: CodecMode,
        payload: ByteArray,
        senderLanguage: String,
        receiverLanguage: String,
    ): DecodedText = when (mode) {
        // Language-independent bytes; the header says whose language it is.
        CodecMode.RAW -> DecodedText(String(payload, Charsets.UTF_8), senderLanguage)

        // The payload names its own language, and that is authoritative:
        // decoding against the receiver's table would map every character to
        // the wrong index and yield nonsense in a plausible-looking script.
        CodecMode.PACK7 -> decodePack7(payload)

        // Only the id travelled, so the receiver renders it in its own
        // language. This is the cross-language path.
        CodecMode.PHRASE -> decodePhrase(payload, senderLanguage, receiverLanguage)
    }

    // -----------------------------------------------------------------------
    // PACK7
    // -----------------------------------------------------------------------

    /** @return the payload, or null if this text cannot be represented exactly. */
    private fun encodePack7(text: String, languageCode: String): ByteArray? {
        if (text.length > maxPack7Chars) return null
        val index = Alphabets.indexFor(languageCode) ?: return null

        val writer = BitWriter()
        for (ch in text) {
            // One character outside the table sends the whole message to RAW.
            // Always correct, just larger.
            val slot = index[ch] ?: return null
            writer.write(slot, 7)
        }
        val body = writer.toByteArray()

        val out = ByteArray(2 + body.size)
        out[0] = languageWireId(languageCode).toByte()
        out[1] = text.length.toByte()
        body.copyInto(out, 2)
        return out
    }

    private fun decodePack7(payload: ByteArray): DecodedText {
        check(payload.size >= 2) { "PACK7 payload too short" }
        val language = languageFromWireId(payload[0].toInt() and 0xFF)
            ?: throw IllegalStateException("PACK7 payload names an unknown language")
        val charCount = payload[1].toInt() and 0xFF
        val table = Alphabets.tableFor(language.code)
            ?: throw IllegalStateException("no alphabet for ${language.code}")

        val reader = BitReader(payload, offset = 2)
        val sb = StringBuilder(charCount)
        repeat(charCount) {
            // charCount is what distinguishes real trailing characters from
            // the final byte's zero padding - slot 0 is a space, so without
            // it a padded payload would decode with phantom trailing spaces.
            val slot = reader.read(7)
            check(slot < table.length) { "PACK7 slot $slot outside ${language.code} table" }
            sb.append(table[slot])
        }
        return DecodedText(sb.toString(), language.code)
    }

    // -----------------------------------------------------------------------
    // PHRASE
    // -----------------------------------------------------------------------

    private fun encodePhrase(text: String, languageCode: String): ByteArray? {
        val id = PhraseDictionary.idFor(text, languageCode) ?: return null
        if (id > PhraseDictionary.MAX_ID) return null
        return byteArrayOf(languageWireId(languageCode).toByte(), id.toByte())
    }

    private fun decodePhrase(
        payload: ByteArray,
        senderLanguage: String,
        receiverLanguage: String,
    ): DecodedText {
        check(payload.size >= 2) { "PHRASE payload too short" }
        val payloadLanguage = languageFromWireId(payload[0].toInt() and 0xFF)?.code
            ?: senderLanguage
        val id = payload[1].toInt() and 0xFF

        // The receiver's language is the point of this mode.
        PhraseDictionary.surfaceFor(id, receiverLanguage)?.let {
            return DecodedText(it, receiverLanguage)
        }
        // Not authored in the receiver's language yet: fall back to the
        // sender's, so a partly-populated table degrades to same-language
        // delivery rather than to nothing.
        PhraseDictionary.surfaceFor(id, payloadLanguage)?.let {
            return DecodedText(it, payloadLanguage)
        }
        throw IllegalStateException("phrase id $id is not in this build's table")
    }

    // -----------------------------------------------------------------------

    /**
     * Decode a candidate straight back and require it to match the source.
     *
     * The receiver's language is set to the sender's here, so PHRASE is
     * checked against the surface it would produce for a same-language peer.
     * That is the strict reading: a phrase whose canonical surface differs
     * from what was actually said (different punctuation, say) is rejected
     * rather than quietly replacing the operator's words.
     */
    private fun reproducesExactly(
        candidate: EncodedPayload,
        text: String,
        languageCode: String,
    ): Boolean = try {
        decode(candidate.mode, candidate.bytes, languageCode, languageCode).text == text
    } catch (e: Exception) {
        false
    }
}
