package com.itantra.app.stt

import com.itantra.app.config.SAMPLE_RATE
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Direct port of src/core/stt/SimulatedSttBackend.ts.
 *
 * Placeholder used when no real decoder can be loaded for a language
 * (model not installed on device).
 *
 * IT DOES NOT LISTEN. There is no recognition here of any kind. It
 * reports what the pipeline measured about the audio (how long it was,
 * how loud) and says plainly that no decoder is installed.
 *
 * An earlier version of the RN source returned realistic-sounding
 * sentences so the UI could be demonstrated, which was judged a mistake:
 * the output looked like a bad transcription rather than an absent one.
 * Anything this class emits must be impossible to mistake for a
 * transcript — this constraint is preserved exactly in this port.
 */
class PlaceholderSttBackend : SttBackend {
    override val kind = SttEngineKind.SIMULATED
    override val isReady = true

    override fun load(languageCode: String) {
        // Nothing to load.
    }

    override fun transcribe(samples: FloatArray, languageCode: String): SttTranscription {
        val durationMs = (samples.size.toDouble() / SAMPLE_RATE) * 1000.0
        val seconds = "%.1f".format(durationMs / 1000.0)

        // Peak amplitude confirms the microphone is genuinely feeding the
        // pipeline, which is the one useful thing this stand-in can report.
        var peak = 0f
        for (s in samples) {
            val v = abs(s)
            if (v > peak) peak = v
        }
        val loudness = (peak * 100).roundToInt()

        val text = "[no speech model installed — captured ${seconds}s of your audio " +
            "at ${loudness}% peak level, but there is no decoder to read it]"
        return SttTranscription(text = text, rawText = text)
    }

    override fun dispose() {
        // Nothing to release.
    }
}
