package com.itantra.app.codec

import com.itantra.app.config.LANGUAGES
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The demo says two things out loud: the payload is smaller, and the text
 * comes back exactly. These tests are what make those claims checkable
 * rather than asserted.
 */
class CodecTest {

    private val codec = ITantraCodec()

    /** Real operational sentences, in the script each language is written in. */
    private val corpus: Map<String, List<String>> = mapOf(
        "en-IN" to listOf(
            "fire at north gate",
            "send help immediately",
            "area clear",
            "requesting backup at checkpoint three",
            "casualty reported move to secondary position now",
        ),
        "hi-IN" to listOf(
            "उत्तर द्वार पर आग",
            "उत्तर द्वार पर आग लग गई है तुरंत मदद भेजो",
            "स्थिति सुरक्षित है",
        ),
        "mr-IN" to listOf("उत्तर दरवाजाला आग लागली आहे", "मदत पाठवा"),
        "gu-IN" to listOf("ઉત્તર દરવાજા પર આગ લાગી છે", "તાત્કાલિક મદદ મોકલો"),
        "kn-IN" to listOf("ಉತ್ತರ ದ್ವಾರದಲ್ಲಿ ಬೆಂಕಿ", "ತಕ್ಷಣ ಸಹಾಯ ಕಳುಹಿಸಿ"),
        "ml-IN" to listOf("വടക്കേ ഗേറ്റിൽ തീ", "ഉടനടി സഹായം അയയ്ക്കുക"),
        "ta-IN" to listOf("வடக்கு வாசலில் தீ", "உடனடியாக உதவி அனுப்பவும்"),
        "te-IN" to listOf("ఉత్తర ద్వారం వద్ద మంటలు", "వెంటనే సహాయం పంపండి"),
        "or-IN" to listOf("ଉତ୍ତର ଦ୍ୱାରରେ ନିଆଁ", "ତୁରନ୍ତ ସାହାଯ୍ୟ ପଠାନ୍ତୁ"),
        "bn-IN" to listOf("উত্তর গেটে আগুন", "এখনই সাহায্য পাঠান"),
    )

    private fun roundTrip(text: String, language: String): String {
        val encoded = codec.encode(text, language)
        return codec.decode(encoded.mode, encoded.bytes, language, language).text
    }

    // -----------------------------------------------------------------------
    // Lossless
    // -----------------------------------------------------------------------

    @Test
    fun `every language round trips every sentence exactly`() {
        assertEquals("every registered language needs a corpus", LANGUAGES.size, corpus.size)
        for ((language, sentences) in corpus) {
            for (text in sentences) {
                assertEquals("round trip for $language", text, roundTrip(text, language))
            }
        }
    }

    @Test
    fun `empty text round trips`() {
        assertEquals("", roundTrip("", "en-IN"))
        assertEquals("", roundTrip("", "hi-IN"))
    }

    @Test
    fun `trailing spaces survive, so padding is never mistaken for content`() {
        // PACK7 zero-pads its final byte and slot 0 is the space character,
        // so without an explicit character count this is exactly the input
        // that would come back with phantom trailing spaces.
        for (text in listOf(" ", "  ", "fire ", "fire  ", " fire ")) {
            assertEquals("[$text]", text, roundTrip(text, "en-IN"))
        }
    }

    @Test
    fun `text at every length round trips`() {
        // Walks every possible bit-alignment of the final byte.
        for (n in 0..40) {
            val text = "a".repeat(n)
            assertEquals("length $n", text, roundTrip(text, "en-IN"))
        }
    }

    // -----------------------------------------------------------------------
    // Never larger than RAW
    // -----------------------------------------------------------------------

    @Test
    fun `the chosen payload is never larger than plain utf-8`() {
        // This is the guard that stops a NEGATIVE compression ratio appearing
        // on camera. Short Latin text is the dangerous case: PACK7's 2-byte
        // header can outweigh its 1-bit-per-character saving.
        for ((language, sentences) in corpus) {
            for (text in sentences) {
                val raw = text.toByteArray(Charsets.UTF_8).size
                val chosen = codec.encode(text, language).bytes.size
                assertTrue(
                    "$language \"$text\": chose $chosen B against RAW's $raw B",
                    chosen <= raw,
                )
            }
        }
    }

    @Test
    fun `short english prefers RAW rather than growing`() {
        for (text in listOf("a", "ok", "go now", "move up")) {
            val encoded = codec.encode(text, "en-IN")
            assertEquals(
                "\"$text\" should stay RAW",
                CodecMode.RAW,
                encoded.mode,
            )
        }
    }

    // -----------------------------------------------------------------------
    // Mode selection
    // -----------------------------------------------------------------------

    @Test
    fun `indic text is packed, roughly threefold`() {
        val text = "उत्तर द्वार पर आग लग गई है तुरंत मदद भेजो"
        val encoded = codec.encode(text, "hi-IN")
        assertEquals(CodecMode.PACK7, encoded.mode)
        val raw = text.toByteArray(Charsets.UTF_8).size
        assertTrue(
            "expected a real saving, got $raw B -> ${encoded.bytes.size} B",
            encoded.bytes.size * 2 < raw,
        )
    }

    @Test
    fun `a known phrase costs two bytes whatever its length`() {
        for ((text, language) in listOf(
            PhraseDictionary.surfaceFor(1, "en-IN")!! to "en-IN",
            PhraseDictionary.surfaceFor(1, "hi-IN")!! to "hi-IN",
            PhraseDictionary.surfaceFor(1, "mr-IN")!! to "mr-IN",
        )) {
            val encoded = codec.encode(text, language)
            assertEquals("$language \"$text\"", CodecMode.PHRASE, encoded.mode)
            assertEquals(2, encoded.bytes.size)
        }
    }

    @Test
    fun `text with an out-of-alphabet character falls back to RAW`() {
        // An emoji is in no table; the message must still transmit.
        val text = "fire ✈ north"
        val encoded = codec.encode(text, "en-IN")
        assertEquals(CodecMode.RAW, encoded.mode)
        assertEquals(text, roundTrip(text, "en-IN"))
    }

    @Test
    fun `text longer than PACK7's character count falls back to RAW`() {
        val text = "a".repeat(256)
        assertEquals(CodecMode.RAW, codec.encode(text, "en-IN").mode)
        assertEquals(text, roundTrip(text, "en-IN"))

        // 255 is still packable.
        assertEquals(CodecMode.PACK7, codec.encode("a".repeat(255), "en-IN").mode)
    }

    @Test
    fun `originalBytes reports the source size, not the payload size`() {
        val text = "उत्तर द्वार पर आग लग गई है"
        val encoded = codec.encode(text, "hi-IN")
        assertEquals(text.toByteArray(Charsets.UTF_8).size, encoded.originalBytes)
        assertNotEquals(encoded.originalBytes, encoded.bytes.size)
    }

    // -----------------------------------------------------------------------
    // Language semantics (the rule that produces garbage if broken)
    // -----------------------------------------------------------------------

    @Test
    fun `PACK7 decodes in the sender's language regardless of the receiver's`() {
        val text = "வடக்கு வாசலில் தீ ஆபத்து உடனடி உதவி"
        val encoded = codec.encode(text, "ta-IN")
        assertEquals(CodecMode.PACK7, encoded.mode)

        // A Hindi-configured receiver must still get Tamil, in Tamil script.
        val decoded = codec.decode(encoded.mode, encoded.bytes, "ta-IN", "hi-IN")
        assertEquals(text, decoded.text)
        assertEquals("ta-IN", decoded.languageCode)
    }

    @Test
    fun `RAW decodes in the sender's language from the packet header`() {
        val text = "fire ✈ north"
        val encoded = codec.encode(text, "en-IN")
        val decoded = codec.decode(encoded.mode, encoded.bytes, "en-IN", "hi-IN")
        assertEquals(text, decoded.text)
        assertEquals("en-IN", decoded.languageCode)
    }

    @Test
    fun `PHRASE crosses languages - Marathi in, Hindi out, no translation`() {
        val spoken = PhraseDictionary.surfaceFor(1, "mr-IN")!!
        val encoded = codec.encode(spoken, "mr-IN")
        assertEquals(CodecMode.PHRASE, encoded.mode)
        assertEquals(2, encoded.bytes.size)

        val decoded = codec.decode(encoded.mode, encoded.bytes, "mr-IN", "hi-IN")
        assertEquals(PhraseDictionary.surfaceFor(1, "hi-IN"), decoded.text)
        assertEquals("hi-IN", decoded.languageCode)
    }

    @Test
    fun `PHRASE falls back to the sender's language when the receiver has no surface`() {
        val english = PhraseDictionary.surfaceFor(1, "en-IN")!!
        val encoded = codec.encode(english, "en-IN")
        // Gujarati has no surfaces in the phrase table, so a Gujarati-
        // configured receiver cannot render this id in its own language. It
        // must still hear the message, in the sender's.
        val receiver = "gu-IN"
        assertNull(
            "this test needs a language the phrase table does not cover",
            PhraseDictionary.surfaceFor(1, receiver),
        )
        val decoded = codec.decode(encoded.mode, encoded.bytes, "en-IN", receiver)
        assertEquals(english, decoded.text)
        assertEquals("en-IN", decoded.languageCode)
    }

    @Test
    fun `a phrase whose surface differs from what was said is not sent as PHRASE`() {
        // Punctuation the table does not carry: encoding as PHRASE would put
        // the operator's words back slightly changed, so it must not be used.
        val text = PhraseDictionary.surfaceFor(1, "en-IN")!! + "."
        assertNotEquals(CodecMode.PHRASE, codec.encode(text, "en-IN").mode)
        assertEquals(text, roundTrip(text, "en-IN"))
    }
}
