package com.itantra.app.audio

import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Direct port of src/core/audio/pcm.ts. Same formulas, same constants —
 * nothing tuned or optimized. All hot-path, no allocations beyond the
 * result, same as the source.
 */
object PcmMath {

    /**
     * Reinterpret int16 samples as normalized floats in [-1, 1].
     *
     * Same asymmetric scaling as the TS source (int16 range is
     * [-32768, 32767], so positive and negative values are scaled by
     * different denominators to land exactly on +/-1.0 without clipping).
     *
     * Takes a ShortArray directly rather than raw bytes: Android's
     * AudioRecord.read(ShortArray, ...) already decodes PCM16LE for us, so
     * there is no byte-parsing step to port — int16ToFloat32(ArrayBuffer) in
     * the TS source exists only because expo-audio hands back raw bytes.
     * The scaling math itself is unchanged.
     */
    fun int16ToFloat32(shorts: ShortArray, length: Int = shorts.size): FloatArray {
        val out = FloatArray(length)
        for (i in 0 until length) {
            val s = shorts[i]
            out[i] = if (s < 0) s / 32768f else s / 32767f
        }
        return out
    }

    /** Root-mean-square amplitude of a frame, 0..1. */
    fun rms(samples: FloatArray): Float {
        if (samples.isEmpty()) return 0f
        var sum = 0.0
        for (v in samples) sum += v.toDouble() * v.toDouble()
        return sqrt(sum / samples.size).toFloat()
    }

    /** Zero-crossing rate, 0..1. Separates voiced speech from broadband hiss. */
    fun zeroCrossingRate(samples: FloatArray): Float {
        if (samples.size < 2) return 0f
        var crossings = 0
        for (i in 1 until samples.size) {
            if ((samples[i - 1] < 0f) != (samples[i] < 0f)) crossings++
        }
        return crossings.toFloat() / (samples.size - 1)
    }

    /**
     * Map an RMS value to a perceptually even 0..1 meter reading.
     * Speech RMS lives around 0.02-0.3, so a linear meter barely moves. This
     * maps a -60..0 dBFS window onto 0..1.
     */
    fun rmsToLevel(value: Float): Float {
        if (value <= 0f) return 0f
        val db = 20 * log10(value.toDouble())
        val minDb = -60.0
        return min(1.0, max(0.0, (db - minDb) / -minDb)).toFloat()
    }

    /**
     * Linear-interpolating resampler. Only exercised if the hardware
     * delivers a different rate than requested; on Android's AudioRecord
     * (unlike expo-audio) the platform already resamples internally to the
     * rate passed to the constructor, so in practice this path is not hit
     * today — kept for parity with the source and as a defensive fallback,
     * not removed.
     */
    fun resampleLinear(samples: FloatArray, fromRate: Int, toRate: Int): FloatArray {
        if (fromRate == toRate || samples.isEmpty()) return samples
        val ratio = fromRate.toDouble() / toRate.toDouble()
        val outLength = (samples.size / ratio).toInt()
        val out = FloatArray(outLength)
        for (i in 0 until outLength) {
            val pos = i * ratio
            val idx = pos.toInt()
            val frac = (pos - idx).toFloat()
            val a = samples.getOrElse(idx) { 0f }
            val b = samples.getOrElse(idx + 1) { a }
            out[i] = a + (b - a) * frac
        }
        return out
    }

    /** Concatenate frames into one contiguous buffer. */
    fun concatFloat32(chunks: List<FloatArray>): FloatArray {
        var total = 0
        for (c in chunks) total += c.size
        val out = FloatArray(total)
        var offset = 0
        for (c in chunks) {
            c.copyInto(out, offset)
            offset += c.size
        }
        return out
    }
}
