package com.itantra.app.codec

import com.itantra.app.packet.classifyPriority
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The phrase table is a wire contract carrying operational meaning, so its
 * invariants are worth enforcing rather than trusting to careful typing.
 */
class PhraseDictionaryTest {

    private val codec = ITantraCodec()
    private val languages = listOf("en-IN", "hi-IN", "ta-IN", "mr-IN", "bn-IN")

    @Test
    fun `every phrase is authored in every supported language`() {
        for (id in PhraseDictionary.ids) {
            for (language in languages) {
                assertNotNull(
                    "phrase $id has no $language surface",
                    PhraseDictionary.surfaceFor(id, language),
                )
            }
        }
    }

    @Test
    fun `ids fit the one-byte phraseId field`() {
        for (id in PhraseDictionary.ids) {
            assertTrue("id $id exceeds ${PhraseDictionary.MAX_ID}", id in 1..PhraseDictionary.MAX_ID)
        }
    }

    @Test
    fun `no two phrases share a surface within a language`() {
        // A collision is silent: the reverse map keeps whichever id was built
        // last, so one phrase becomes unreachable and the other answers for
        // both.
        for (language in languages) {
            val seen = mutableMapOf<String, Int>()
            for ((id, lang, text) in PhraseDictionary.entries()) {
                if (lang != language) continue
                val key = text.trim().lowercase()
                val previous = seen.put(key, id)
                assertEquals(
                    "\"$text\" is used by both phrase $previous and phrase $id in $language",
                    null,
                    previous,
                )
            }
        }
    }

    @Test
    fun `every surface of a phrase classifies to the same priority band`() {
        // Priority is derived from the SENDER's plaintext, so if the English
        // surface trips a CRITICAL keyword and the Hindi one does not, the
        // same operational message is banded differently depending on who
        // spoke it - and on a critical message that decides whether it
        // interrupts playback at the far end.
        for (id in PhraseDictionary.ids) {
            val bands = languages.associateWith { language ->
                classifyPriority(PhraseDictionary.surfaceFor(id, language)!!)
            }
            val distinct = bands.values.toSet()
            assertEquals(
                "phrase $id classifies inconsistently: " +
                    bands.entries.joinToString { "${it.key}=${it.value.value}" },
                1,
                distinct.size,
            )
        }
    }

    @Test
    fun `the table covers every priority band`() {
        // A demo that cannot produce a CRITICAL cannot show the priority
        // interrupt, and one that is all CRITICAL shows nothing either.
        val bands = PhraseDictionary.ids
            .map { classifyPriority(PhraseDictionary.surfaceFor(it, "en-IN")!!) }
            .toSet()
        assertEquals("expected all four bands represented", 4, bands.size)
    }

    @Test
    fun `every authored surface actually encodes as a two-byte PHRASE`() {
        // An entry that does not round-trip exactly is silently never
        // selected, so it would look authored while doing nothing.
        for ((id, language, text) in PhraseDictionary.entries()) {
            val encoded = codec.encode(text, language)
            assertEquals(
                "phrase $id ($language) \"$text\" encoded as ${encoded.mode}",
                CodecMode.PHRASE,
                encoded.mode,
            )
            assertEquals(2, encoded.bytes.size)

            val decoded = codec.decode(encoded.mode, encoded.bytes, language, language)
            assertEquals(text, decoded.text)
        }
    }

    @Test
    fun `a phrase spoken in one language is delivered in the receiver's`() {
        for (id in PhraseDictionary.ids) {
            val sent = PhraseDictionary.surfaceFor(id, "ta-IN")!!
            val encoded = codec.encode(sent, "ta-IN")
            for (receiver in languages) {
                val decoded = codec.decode(encoded.mode, encoded.bytes, "ta-IN", receiver)
                assertEquals(
                    "phrase $id delivered to $receiver",
                    PhraseDictionary.surfaceFor(id, receiver),
                    decoded.text,
                )
                assertEquals(receiver, decoded.languageCode)
            }
        }
    }
}
