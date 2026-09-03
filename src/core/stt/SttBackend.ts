import type { SttEngineKind } from '../types';

export interface SttTranscription {
  text: string;
  /** Language the decoder actually reported, when it is multilingual. */
  detectedLanguage?: string;
  /**
   * Text exactly as the engine returned it, before repairScript() ran.
   * Diagnostic-only: additive field for accuracy investigation, not consumed
   * by any production code path.
   */
  rawText?: string;
}

/**
 * A speech recogniser bound to one language.
 *
 * `load` is separated from construction because model loading is slow (hundreds
 * of ms to seconds) and the controller wants to show an INITIALIZING state
 * around it.
 */
export interface SttBackend {
  readonly kind: SttEngineKind;
  /** True once a decoder is resident and `transcribe` can be called. */
  readonly isReady: boolean;
  /** Load (or swap to) the decoder for `languageCode`. */
  load(languageCode: string): Promise<void>;
  /** Decode one utterance of 16 kHz mono audio. */
  transcribe(
    samples: Float32Array,
    languageCode: string
  ): Promise<SttTranscription>;
  dispose(): Promise<void>;
}
