package com.itantra.app.codec

/**
 * How a payload was encoded.
 *
 * The mode travels in the packet header ([com.itantra.app.packet.ITantraPacket.mode]),
 * **not** inside the payload. The decoder cannot begin until it knows which
 * mode was used, so the mode has to be readable without first decoding;
 * carrying it in both places would transmit the same fact twice, on a link
 * where the entire payload can be two bytes.
 *
 * [wire] values are a permanent contract. Append new modes; never renumber.
 */
enum class CodecMode(val wire: Byte) {
    /** UTF-8, unmodified. Always correct, never smaller. */
    RAW(0),

    /** Per-language character table, 7 bits per character. */
    PACK7(1),

    /** A single id from the operational phrase table. Two bytes, any length. */
    PHRASE(2);

    companion object {
        fun fromWire(value: Byte): CodecMode? = entries.firstOrNull { it.wire == value }
    }
}

/**
 * The result of encoding one utterance.
 *
 * [bytes] is the payload alone — it does not contain the mode, and it does
 * not contain the packet header.
 */
class EncodedPayload(
    val bytes: ByteArray,
    val mode: CodecMode,
    /** UTF-8 size of the source text, so the UI can show what was saved. */
    val originalBytes: Int,
)

/**
 * Decoded text, together with **the language that text is actually in**.
 *
 * The language is returned rather than left to the caller because it differs
 * by mode, and getting it wrong produces garbage rather than a degraded
 * result:
 *
 * | mode   | text comes back in    |
 * |--------|-----------------------|
 * | PHRASE | the receiver's language |
 * | PACK7  | the sender's language, from the payload's own langId |
 * | RAW    | the sender's language, from the packet header |
 *
 * Only PHRASE crosses languages, and it does so because only an id travels:
 * Tamil spoken in, Hindi spoken out, with no translation anywhere in the
 * system. That also makes this value what selects the TTS voice — speaking
 * PACK7 Tamil text with the receiver's Hindi voice would be wrong.
 */
data class DecodedText(
    val text: String,
    val languageCode: String,
)

/**
 * Lossless text ⇄ bytes.
 *
 * `decode(mode, encode(x).bytes, …) == x` for every input this codec accepts,
 * for every mode it selects. There are no lossy paths: a text the compact
 * modes cannot represent exactly falls back to RAW rather than being
 * approximated.
 *
 * This is the seam the real tier system replaces. When it arrives,
 * [ITantraCodec] becomes a thin JNI wrapper and everything above this
 * interface is untouched.
 */
interface TextCodec {
    /**
     * Encode [text], spoken in [languageCode], choosing the smallest mode
     * that reproduces it exactly.
     */
    fun encode(text: String, languageCode: String): EncodedPayload

    /**
     * Decode [payload], which was encoded with [mode].
     *
     * [senderLanguage] is the packet header's language field; it is what RAW
     * decodes as. [receiverLanguage] is this device's configured language;
     * it is what PHRASE decodes into. PACK7 uses neither — its payload names
     * its own language.
     */
    fun decode(
        mode: CodecMode,
        payload: ByteArray,
        senderLanguage: String,
        receiverLanguage: String,
    ): DecodedText
}
