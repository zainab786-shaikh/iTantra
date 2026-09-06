package com.itantra.app.vad

import com.itantra.app.audio.PcmMath
import kotlin.math.exp
import kotlin.math.max

/**
 * Direct port of src/core/vad/EnergyVad.ts. Every constant below is copied
 * verbatim from the source — none were re-tuned during migration.
 *
 * Dependency-free voice-activity detector. Tracks the room's noise floor
 * rather than comparing against a fixed threshold, so it holds up in a
 * noisy field environment where an absolute cutoff would either latch
 * permanently on or never trigger:
 *
 *  - the noise floor adapts fast downward and slowly upward, so it settles
 *    onto quiet quickly but is not dragged up by sustained speech;
 *  - a frame must beat the floor by SNR_MARGIN to score as speech;
 *  - zero-crossing rate vetoes broadband noise (wind, handling) that
 *    clears the energy test but has none of the periodicity of voice.
 */
class EnergyVad : VadBackend {

    /** Adaptive estimate of background RMS. */
    private var noiseFloor = 0.005f

    /** Frames seen since reset, used to widen adaptation during the initial settle. */
    private var framesSeen = 0

    companion object {
        /** Speech must exceed the floor by this factor. */
        private const val SNR_MARGIN = 2.4f

        /** Absolute floor; below this it is silence no matter what the ratio says. */
        private const val ABSOLUTE_FLOOR = 0.0045f

        /** ZCR above this is hiss/fricative noise rather than voiced speech. */
        private const val ZCR_CEILING = 0.42f

        /** Frames of leading audio during which the floor adapts aggressively. */
        private const val SETTLE_FRAMES = 12
    }

    override fun initialize() {
        reset()
    }

    override fun process(frame: FloatArray): Float {
        val energy = PcmMath.rms(frame)
        framesSeen++

        val ratio = energy / max(noiseFloor, ABSOLUTE_FLOOR)
        val zcr = PcmMath.zeroCrossingRate(frame)

        // Map the SNR ratio onto a probability with a soft knee around the
        // margin, so the segmenter's hysteresis has a gradient to work with
        // instead of a hard 0/1 step.
        val knee = (ratio - SNR_MARGIN) / SNR_MARGIN
        var probability = (1.0 / (1.0 + exp((-knee * 3).toDouble()))).toFloat()

        if (energy < ABSOLUTE_FLOOR) probability = 0f
        // Attenuate rather than veto outright: a fricative ('s', 'sh') is
        // real speech with a high ZCR, so zeroing it would clip word endings.
        if (zcr > ZCR_CEILING) probability *= 0.35f

        // Adapt only on frames that look like background, otherwise a long
        // utterance would raise the floor until it silences itself.
        val settling = framesSeen < SETTLE_FRAMES
        if (probability < 0.3f || settling) {
            val rising = energy > noiseFloor
            val alpha = if (settling) 0.3f else if (rising) 0.02f else 0.15f
            noiseFloor = noiseFloor * (1 - alpha) + energy * alpha
        }

        return probability
    }

    override fun reset() {
        noiseFloor = 0.005f
        framesSeen = 0
    }

    override fun dispose() {
        // No native resources to release.
    }
}
