import type { VadBackendKind } from '../types';

/**
 * A voice-activity detector: given one fixed-size frame of 16 kHz mono audio,
 * return the probability that it contains speech.
 *
 * Kept deliberately narrow so the energy detector and a Silero ONNX session are
 * interchangeable, and so the segmenter never learns which one it is driving.
 */
export interface VadBackend {
  readonly kind: VadBackendKind;
  /** Load weights / allocate state. Must be safe to call twice. */
  initialize(): Promise<void>;
  /** @returns speech probability in [0, 1]. */
  process(frame: Float32Array): number;
  /** Drop recurrent state between utterances. */
  reset(): void;
  /** Free native resources. */
  dispose(): Promise<void>;
}
