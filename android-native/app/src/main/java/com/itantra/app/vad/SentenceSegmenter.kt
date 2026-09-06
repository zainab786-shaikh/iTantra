package com.itantra.app.vad

import android.util.Log
import com.itantra.app.audio.AudioFrame
import com.itantra.app.audio.PcmMath
import com.itantra.app.config.SAMPLE_RATE
import com.itantra.app.config.VadConfig

/** Direct port of AudioSegment in src/core/vad/SentenceSegmenter.ts. A completed utterance, ready to hand to the recogniser. */
data class AudioSegment(
    /** Contiguous 16 kHz mono audio, including pre-speech padding. */
    val samples: FloatArray,
    val durationMs: Double,
    /** True when a forced flush cut the utterance rather than a natural pause. */
    val forced: Boolean,
)

private const val TAG = "SentenceSegmenter"

/**
 * Direct port of src/core/vad/SentenceSegmenter.ts.
 *
 * Turns a stream of per-frame speech probabilities into discrete
 * utterances.
 *
 * State machine:
 *
 *   SILENCE --p > speechThreshold--> SPEECH        (emit onSpeechStart)
 *   SPEECH  --p < silenceThreshold for endOfSpeechSilenceMs--> SILENCE
 *                                                  (emit segment)
 *
 * Three details that matter more than the state machine itself (kept
 * verbatim from the source's own reasoning):
 *
 *  1. Hysteresis. Entering speech needs [VadConfig.speechThreshold];
 *     leaving it needs to fall under the lower [VadConfig.silenceThreshold].
 *     A single threshold would chatter on every frame that sits near the
 *     line.
 *
 *  2. Pre-speech padding. A rolling ring buffer of the last
 *     [VadConfig.preSpeechPaddingMs] of *pre-trigger* audio is prepended to
 *     every segment. VAD always fires a frame or two into the first
 *     phoneme, and without this the decoder receives "ello" instead of
 *     "hello".
 *
 *  3. Silence is buffered, not dropped. Trailing silence stays in the
 *     segment because CTC decoders need the trailing context to close the
 *     final token; it is only discarded when it exceeds the end-of-speech
 *     window.
 */
class SentenceSegmenter(
    private var config: VadConfig,
    private val onSpeechStart: () -> Unit,
    private val onSegment: (AudioSegment) -> Unit,
) {
    private var speaking = false

    /** Frames accumulated for the utterance in flight. */
    private var buffer: MutableList<FloatArray> = mutableListOf()

    /** Rolling pre-trigger audio, kept only while not speaking. */
    private var padding: MutableList<FloatArray> = mutableListOf()
    private var paddingSamples = 0

    /** Consecutive sub-threshold audio while speaking, in ms. */
    private var silenceMs = 0.0

    /** Length of the utterance in flight, in ms. */
    private var speechMs = 0.0

    /** Frames in the utterance whose probability cleared the speech threshold. */
    private var voicedFrames = 0

    /** Total frames in the utterance, for the voiced-ratio gate. */
    private var totalFrames = 0

    companion object {
        /**
         * Minimum fraction of an utterance that must look like speech.
         *
         * A door slam or a burst of music can trip the threshold for a
         * frame or two and then decay, producing a segment that is mostly
         * silence. It is far cheaper to refuse the decode than to filter
         * the output afterward.
         */
        private const val MIN_VOICED_RATIO = 0.35
    }

    /** Live-update tuning (e.g. the pause-length selector) without losing state. */
    fun setConfig(newConfig: VadConfig) {
        config = newConfig
    }

    val isSpeaking: Boolean get() = speaking

    /** ms of speech accumulated so far, or null when idle. */
    val currentUtteranceMs: Double? get() = if (speaking) speechMs else null

    /**
     * Feed one frame plus its speech probability.
     *
     * The frame is copied on the way in, same as the TS source (there,
     * because PcmFrame.samples is a view into the capture service's
     * scratch buffer; here, defensively, since AudioFrame.samples may
     * likewise be reused by the caller).
     */
    fun push(frame: AudioFrame, probability: Float) {
        val frameMs = (frame.samples.size.toDouble() / SAMPLE_RATE) * 1000.0
        val copy = frame.samples.copyOf()

        if (!speaking) {
            retainPadding(copy)

            if (probability >= config.speechThreshold) {
                speaking = true
                silenceMs = 0.0
                speechMs = 0.0
                voicedFrames = 1
                totalFrames = 1
                // Seed the utterance with the pre-trigger ring, then the trigger frame.
                buffer = (padding + copy).toMutableList()
                padding = mutableListOf()
                paddingSamples = 0
                onSpeechStart()
            }
            return
        }

        buffer.add(copy)
        speechMs += frameMs
        totalFrames++
        if (probability >= config.speechThreshold) voicedFrames++

        if (probability < config.silenceThreshold) {
            silenceMs += frameMs
        } else {
            // Any confident frame resets the pause clock; that is what lets
            // someone pause mid-sentence for breath without being cut off.
            silenceMs = 0.0
        }

        if (silenceMs >= config.endOfSpeechSilenceMs) {
            finalize(false)
            return
        }

        if (speechMs >= config.maxSpeechMs) {
            // Bound the worst case: flush and immediately continue a new
            // utterance so a monologue still produces packets.
            finalize(true)
        }
    }

    /**
     * Force the utterance in flight to close, for stopPtt().
     * No-op when nothing is buffered.
     */
    fun flush() {
        if (!speaking) return
        finalize(true)
    }

    /** Drop everything without emitting. */
    fun reset() {
        speaking = false
        buffer = mutableListOf()
        padding = mutableListOf()
        paddingSamples = 0
        silenceMs = 0.0
        speechMs = 0.0
        voicedFrames = 0
        totalFrames = 0
    }

    /** Maintain the fixed-length pre-trigger ring buffer. */
    private fun retainPadding(frame: FloatArray) {
        val maxSamples = ((config.preSpeechPaddingMs / 1000.0) * SAMPLE_RATE).toInt()
        if (maxSamples <= 0) return

        padding.add(frame)
        paddingSamples += frame.size

        while (paddingSamples > maxSamples && padding.isNotEmpty()) {
            paddingSamples -= padding.removeAt(0).size
        }
    }

    private fun finalize(forced: Boolean) {
        val frames = buffer
        val durationMs = speechMs
        val voicedRatio = if (totalFrames > 0) voicedFrames.toDouble() / totalFrames else 0.0

        speaking = false
        buffer = mutableListOf()
        silenceMs = 0.0
        speechMs = 0.0
        voicedFrames = 0
        totalFrames = 0

        // Discard clicks, coughs and door slams rather than paying for a
        // decode that will return an empty string.
        if (durationMs < config.minSpeechMs || frames.isEmpty()) return

        if (voicedRatio < MIN_VOICED_RATIO) {
            Log.d(
                TAG,
                "dropped ${durationMs.toInt()}ms segment: only " +
                    "${(voicedRatio * 100).toInt()}% voiced (needs ${(MIN_VOICED_RATIO * 100).toInt()}%)"
            )
            return
        }

        Log.d(
            TAG,
            "segment accepted: durationMs=${durationMs.toInt()} " +
                "voicedRatio=${(voicedRatio * 100).toInt()}% forced=$forced frames=${frames.size}"
        )

        onSegment(
            AudioSegment(
                samples = PcmMath.concatFloat32(frames),
                durationMs = durationMs,
                forced = forced,
            )
        )
    }
}
