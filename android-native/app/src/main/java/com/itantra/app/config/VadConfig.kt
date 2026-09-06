package com.itantra.app.config

/**
 * Direct port of src/config/vadConfig.ts. Every value is copied verbatim —
 * none were re-tuned during migration. See MIGRATION_STATUS.md Phase 4.
 */
data class VadConfig(
    /** Samples per VAD frame. 512 @ 16 kHz = 32 ms, the Silero v5 window size. */
    val frameSize: Int,
    /** Speech probability above which a frame counts as speech. */
    val speechThreshold: Float,
    /** Probability below which a frame counts as silence (hysteresis band). */
    val silenceThreshold: Float,
    /**
     * Continuous silence that ends an utterance. The brief calls for a
     * configurable 600-1000 ms pause; 750 ms is the default sweet spot
     * between cutting people off mid-sentence and feeling sluggish.
     */
    val endOfSpeechSilenceMs: Int,
    /** Speech shorter than this is discarded as a click/cough rather than flushed. */
    val minSpeechMs: Int,
    /** Hard cap on one utterance; forces a flush so latency stays bounded. */
    val maxSpeechMs: Int,
    /**
     * Audio retained from *before* START_OF_SPEECH. Without this the
     * decoder loses the onset consonant of the first word.
     */
    val preSpeechPaddingMs: Int,
)

val DEFAULT_VAD_CONFIG = VadConfig(
    frameSize = 512,
    speechThreshold = 0.5f,
    silenceThreshold = 0.35f,
    endOfSpeechSilenceMs = 750,
    minSpeechMs = 220,
    maxSpeechMs = 15_000,
    preSpeechPaddingMs = 240,
)

/** Pause preset, matching PAUSE_PRESETS in src/config/vadConfig.ts. */
data class PausePreset(val label: String, val ms: Int)

/** Pause presets exposed in the UI. */
val PAUSE_PRESETS = listOf(
    PausePreset("Snappy", 600),
    PausePreset("Balanced", 750),
    PausePreset("Relaxed", 1000),
)

const val SAMPLE_RATE = 16_000
