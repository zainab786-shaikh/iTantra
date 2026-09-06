package com.itantra.app.audio

/**
 * Direct port of PcmFrame in src/core/types.ts. A frame of PCM handed down
 * the pipeline. Always 16 kHz, mono.
 */
data class AudioFrame(
    /** Normalized samples in [-1, 1]. */
    val samples: FloatArray,
    /** Sample rate of [samples]; guaranteed 16000 after resampling. */
    val sampleRate: Int,
    /** Root-mean-square amplitude of this frame, 0..1. */
    val rms: Float,
    /** ms since capture started. */
    val timestampMs: Double,
)
