package com.itantra.app.audio

import com.itantra.app.config.SAMPLE_RATE

/**
 * Direct port of src/core/audio/AudioCaptureService.ts.
 *
 * Turns the variable-length PCM buffers delivered by AudioRecord.read()
 * into the fixed-size frames the (future) VAD needs. AudioRecord does not
 * guarantee exactly [frameSize] samples per read call, so this class
 * absorbs that: it resamples to 16 kHz if the hardware ever delivers a
 * different rate, and re-chops the stream into exact [frameSize] frames,
 * carrying the remainder across calls.
 *
 * Deliberately holds no VAD/segmentation logic — same separation of
 * concerns as the TS source (VAD scoring happens one layer up, outside
 * this class, in the future transmitter controller).
 *
 * Not thread-confined by itself; callers must serialize calls to
 * [pushBuffer] (the AudioRecord read loop calls it from one background
 * thread today, which satisfies this).
 */
class AudioCaptureService(
    private val frameSize: Int,
    private val onFrame: (AudioFrame) -> Unit,
) {
    /** Samples left over from the previous buffer, awaiting a full frame. */
    private var residue: FloatArray = FloatArray(0)

    /** Total 16 kHz samples emitted, used to derive a monotonic timestamp. */
    private var samplesEmitted: Long = 0

    /** Sample rate actually delivered by the hardware. */
    var actualSampleRate: Int = SAMPLE_RATE
        private set

    /** Discard buffered audio and reset the clock. Call on every start(). */
    fun reset() {
        residue = FloatArray(0)
        samplesEmitted = 0
    }

    /**
     * Feed one native buffer. Emits zero or more complete frames
     * synchronously, on the calling thread.
     *
     * @param shorts int16 PCM, mono, as delivered by AudioRecord.read(ShortArray, ...).
     * @param length number of valid samples in [shorts] (AudioRecord.read()'s return value).
     * @param sampleRate the rate the hardware actually delivered.
     */
    fun pushBuffer(shorts: ShortArray, length: Int, sampleRate: Int) {
        actualSampleRate = sampleRate

        var samples = PcmMath.int16ToFloat32(shorts, length)
        if (sampleRate != SAMPLE_RATE) {
            samples = PcmMath.resampleLinear(samples, sampleRate, SAMPLE_RATE)
        }

        val combined = if (residue.isEmpty()) {
            samples
        } else {
            val merged = FloatArray(residue.size + samples.size)
            residue.copyInto(merged, 0)
            samples.copyInto(merged, residue.size)
            merged
        }

        var offset = 0
        while (combined.size - offset >= frameSize) {
            val frame = combined.copyOfRange(offset, offset + frameSize)
            offset += frameSize
            samplesEmitted += frameSize

            onFrame(
                AudioFrame(
                    samples = frame,
                    sampleRate = SAMPLE_RATE,
                    rms = PcmMath.rms(frame),
                    timestampMs = (samplesEmitted.toDouble() / SAMPLE_RATE) * 1000.0,
                )
            )
        }

        residue = if (offset == 0) combined else combined.copyOfRange(offset, combined.size)
    }
}
