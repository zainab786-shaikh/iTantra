package com.itantra.app.codec

import java.io.ByteArrayOutputStream

/**
 * MSB-first bit writer.
 *
 * MSB-first so that a hex dump of the payload on screen reads in the same
 * order as the bit stream — when a demo shows raw bytes, the bytes should be
 * followable by eye.
 *
 * The final byte is zero-padded. Padding is indistinguishable from real data
 * at this level, which is why every format built on top of this carries its
 * own count of how many symbols to read back (see PACK7's `charCount`).
 */
class BitWriter {
    private val out = ByteArrayOutputStream()
    private var accumulator = 0
    private var bitsHeld = 0

    /** Append the low [width] bits of [value], most significant first. */
    fun write(value: Int, width: Int) {
        require(width in 1..32) { "width $width out of range" }
        for (shift in width - 1 downTo 0) {
            val bit = (value ushr shift) and 1
            accumulator = (accumulator shl 1) or bit
            bitsHeld++
            if (bitsHeld == 8) {
                out.write(accumulator and 0xFF)
                accumulator = 0
                bitsHeld = 0
            }
        }
    }

    /** Flush any partial byte (zero-padded) and return the buffer. */
    fun toByteArray(): ByteArray {
        if (bitsHeld > 0) {
            val padded = accumulator shl (8 - bitsHeld)
            out.write(padded and 0xFF)
            accumulator = 0
            bitsHeld = 0
        }
        return out.toByteArray()
    }
}

/** MSB-first bit reader. The exact inverse of [BitWriter]. */
class BitReader(private val source: ByteArray, private val offset: Int = 0) {
    private var bitPosition = 0

    /** Bits not yet consumed. */
    val bitsRemaining: Int
        get() = (source.size - offset) * 8 - bitPosition

    /**
     * Read [width] bits, most significant first.
     *
     * @throws IllegalStateException if the buffer is exhausted. Callers decode
     *   attacker- or noise-supplied bytes, so running off the end must be an
     *   explicit failure rather than silently returning zeroes.
     */
    fun read(width: Int): Int {
        require(width in 1..32) { "width $width out of range" }
        check(bitsRemaining >= width) { "need $width bits, $bitsRemaining left" }
        var value = 0
        repeat(width) {
            val index = offset + (bitPosition ushr 3)
            val bit = (source[index].toInt() ushr (7 - (bitPosition and 7))) and 1
            value = (value shl 1) or bit
            bitPosition++
        }
        return value
    }
}
