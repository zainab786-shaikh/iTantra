/** Tuning for the voice-activity detector and the sentence segmenter. */
export interface VadConfig {
  /** Samples per VAD frame. 512 @ 16 kHz = 32 ms, the Silero v5 window size. */
  frameSize: number;
  /** Speech probability above which a frame counts as speech. */
  speechThreshold: number;
  /** Probability below which a frame counts as silence (hysteresis band). */
  silenceThreshold: number;
  /**
   * Continuous silence that ends an utterance. The brief calls for a
   * configurable 600-1000 ms pause; 750 ms is the default sweet spot between
   * cutting people off mid-sentence and feeling sluggish.
   */
  endOfSpeechSilenceMs: number;
  /** Speech shorter than this is discarded as a click/cough rather than flushed. */
  minSpeechMs: number;
  /** Hard cap on one utterance; forces a flush so latency stays bounded. */
  maxSpeechMs: number;
  /**
   * Audio retained from *before* START_OF_SPEECH. Without this the decoder loses
   * the onset consonant of the first word.
   */
  preSpeechPaddingMs: number;
}

export const DEFAULT_VAD_CONFIG: VadConfig = {
  frameSize: 512,
  speechThreshold: 0.5,
  silenceThreshold: 0.35,
  endOfSpeechSilenceMs: 750,
  minSpeechMs: 220,
  maxSpeechMs: 15_000,
  preSpeechPaddingMs: 240,
};

/** Pause presets exposed in the UI. */
export const PAUSE_PRESETS = [
  { label: 'Snappy', ms: 600 },
  { label: 'Balanced', ms: 750 },
  { label: 'Relaxed', ms: 1000 },
] as const;

export const SAMPLE_RATE = 16_000;
