package com.itantra.app.codec

import com.itantra.app.config.LANGUAGES
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The alphabet tables are a wire contract; these are their invariants. */
class AlphabetsTest {

    @Test
    fun `every registered language has a table`() {
        for (language in LANGUAGES) {
            assertNotNull("no alphabet for ${language.code}", Alphabets.tableFor(language.code))
        }
        assertEquals(LANGUAGES.size, Alphabets.languages.size)
    }

    @Test
    fun `no table exceeds what a 7-bit index can address`() {
        for (code in Alphabets.languages) {
            val table = Alphabets.tableFor(code)!!
            assertTrue(
                "$code has ${table.length} entries, over ${Alphabets.MAX_ENTRIES}",
                table.length <= Alphabets.MAX_ENTRIES,
            )
        }
    }

    @Test
    fun `no table contains a duplicate character`() {
        // A duplicate would make the character-to-index map lossy: two slots
        // encode the same character but only one decodes back to it.
        for (code in Alphabets.languages) {
            val table = Alphabets.tableFor(code)!!
            val distinct = table.toSet().size
            assertEquals("$code has ${table.length - distinct} duplicate(s)", table.length, distinct)
        }
    }

    @Test
    fun `slot zero is the space character in every table`() {
        for (code in Alphabets.languages) {
            assertEquals("$code slot 0", ' ', Alphabets.tableFor(code)!![0])
        }
    }

    @Test
    fun `index and table agree in both directions`() {
        for (code in Alphabets.languages) {
            val table = Alphabets.tableFor(code)!!
            val index = Alphabets.indexFor(code)!!
            assertEquals(table.length, index.size)
            for ((slot, ch) in table.withIndex()) {
                assertEquals("$code '$ch'", slot, index[ch])
            }
        }
    }
}

/** The bit layer under PACK7. */
class BitPackerTest {

    @Test
    fun `values survive at every width`() {
        for (width in 1..16) {
            val values = listOf(0, 1, (1 shl width) - 1, (1 shl width) / 2)
            val writer = BitWriter()
            for (v in values) writer.write(v, width)
            val reader = BitReader(writer.toByteArray())
            for (v in values) assertEquals("width $width", v, reader.read(width))
        }
    }

    @Test
    fun `non byte aligned streams survive`() {
        for (count in 1..24) {
            val values = List(count) { it % 128 }
            val writer = BitWriter()
            for (v in values) writer.write(v, 7)
            val reader = BitReader(writer.toByteArray())
            for (v in values) assertEquals("count $count", v, reader.read(7))
        }
    }

    @Test
    fun `seven bit symbols pack eight to every seven bytes`() {
        val writer = BitWriter()
        repeat(8) { writer.write(0x7F, 7) }
        assertEquals(7, writer.toByteArray().size)
    }

    @Test
    fun `writing is most significant bit first`() {
        val writer = BitWriter()
        writer.write(0b1, 1)
        writer.write(0b0000000, 7)
        assertEquals(0x80.toByte(), writer.toByteArray()[0])
    }

    @Test
    fun `an offset skips whole bytes`() {
        val writer = BitWriter()
        writer.write(0x41, 8)
        writer.write(0x2A, 7)
        val reader = BitReader(writer.toByteArray(), offset = 1)
        assertEquals(0x2A, reader.read(7))
    }

    @Test(expected = IllegalStateException::class)
    fun `reading past the end fails loudly rather than returning zeroes`() {
        // These bytes can come off the wire, so exhaustion must be an error,
        // not a silent stream of zero symbols.
        BitReader(ByteArray(1)).run {
            read(8)
            read(1)
        }
    }
}
