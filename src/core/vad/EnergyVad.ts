import { rms, zeroCrossingRate } from '../audio/pcm';
import type { VadBackend } from './VadBackend';

/**
 * Dependency-free voice-activity detector.
 *
 * This is the default backend because the Silero path needs a model file on
 * device and `react-native-sherpa-onnx@0.4.3` ships its VAD module as a
 * placeholder that throws (see src/core/vad/SileroVad.ts for the optional
 * ONNX-backed replacement).
 *
 * It tracks the room's noise floor rather than comparing against a fixed
 * threshold, so it holds up in a noisy field environment where an absolute
 * cutoff would either latch permanently on or never trigger:
 *
 *  - the noise floor adapts fast downward and slowly upward, so it settles onto
 *    quiet quickly but is not dragged up by sustained speech;
 *  - a frame must beat the floor by `SNR_MARGIN` to score as speech;
 *  - zero-crossing rate vetoes broadband noise (wind, handling) that clears the
 *    energy test but has none of the periodicity of voice.
 */
export class EnergyVad implements VadBackend {
  readonly kind = 'energy' as const;

  /** Adaptive estimate of background RMS. */
  private noiseFloor = 0.005;
  /** Frames seen since reset, used to widen adaptation during the initial settle. */
  private framesSeen = 0;

  /** Speech must exceed the floor by this factor. */
  private static readonly SNR_MARGIN = 2.4;
  /** Absolute floor; below this it is silence no matter what the ratio says. */
  private static readonly ABSOLUTE_FLOOR = 0.0045;
  /** ZCR above this is hiss/fricative noise rather than voiced speech. */
  private static readonly ZCR_CEILING = 0.42;
  /** Frames of leading audio during which the floor adapts aggressively. */
  private static readonly SETTLE_FRAMES = 12;

  async initialize(): Promise<void> {
    this.reset();
  }

  process(frame: Float32Array): number {
    const energy = rms(frame);
    this.framesSeen++;

    const ratio = energy / Math.max(this.noiseFloor, EnergyVad.ABSOLUTE_FLOOR);
    const zcr = zeroCrossingRate(frame);

    // Map the SNR ratio onto a probability with a soft knee around the margin,
    // so the segmenter's hysteresis has a gradient to work with instead of a
    // hard 0/1 step.
    const knee = (ratio - EnergyVad.SNR_MARGIN) / EnergyVad.SNR_MARGIN;
    let probability = 1 / (1 + Math.exp(-knee * 3));

    if (energy < EnergyVad.ABSOLUTE_FLOOR) probability = 0;
    // Attenuate rather than veto outright: a fricative ('s', 'sh') is real
    // speech with a high ZCR, so zeroing it would clip word endings.
    if (zcr > EnergyVad.ZCR_CEILING) probability *= 0.35;

    // Adapt only on frames that look like background, otherwise a long
    // utterance would raise the floor until it silences itself.
    const settling = this.framesSeen < EnergyVad.SETTLE_FRAMES;
    if (probability < 0.3 || settling) {
      const rising = energy > this.noiseFloor;
      const alpha = settling ? 0.3 : rising ? 0.02 : 0.15;
      this.noiseFloor = this.noiseFloor * (1 - alpha) + energy * alpha;
    }

    return probability;
  }

  reset(): void {
    this.noiseFloor = 0.005;
    this.framesSeen = 0;
  }

  async dispose(): Promise<void> {
    // No native resources to release.
  }
}
