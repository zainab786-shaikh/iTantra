import { SAMPLE_RATE } from '../../config/vadConfig';
import { rms } from './pcm';
import type { PcmFrame } from '../types';

/**
 * Generates speech-shaped audio when no microphone is reachable.
 *
 * `expo-audio`'s `useAudioStream` is an explicit no-op on web, and Expo Go has
 * no native stream at all. Rather than dead-ending the UI on those targets,
 * this source synthesises a signal with the *envelope* of speech — voiced
 * bursts separated by pauses — so that the real VAD, the real segmenter and the
 * real packet path all run and can be observed.
 *
 * It is a signal generator, not a recording: nothing here comes from a
 * microphone, and the engine reports its audio source as synthetic so the UI
 * can say so.
 */
export class SyntheticAudioSource {
  private timer: ReturnType<typeof setInterval> | null = null;
  private readonly frameSize: number;
  private readonly onFrame: (frame: PcmFrame) => void;

  private elapsedMs = 0;
  private phase = 0;
  /** ms into the current burst/pause cycle. */
  private cycleMs = 0;
  private speaking = true;
  private burstMs = 1800;
  private pauseMs = 1100;

  constructor(frameSize: number, onFrame: (frame: PcmFrame) => void) {
    this.frameSize = frameSize;
    this.onFrame = onFrame;
  }

  start(): void {
    if (this.timer) return;
    this.reset();
    const frameMs = (this.frameSize / SAMPLE_RATE) * 1000;
    this.timer = setInterval(() => this.tick(frameMs), frameMs);
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  reset(): void {
    this.elapsedMs = 0;
    this.cycleMs = 0;
    this.phase = 0;
    this.speaking = true;
    this.burstMs = 1400 + Math.random() * 1200;
  }

  private tick(frameMs: number): void {
    this.cycleMs += frameMs;
    this.elapsedMs += frameMs;

    // Alternate between a voiced burst and a pause long enough to trip the
    // segmenter's end-of-speech window.
    const limit = this.speaking ? this.burstMs : this.pauseMs;
    if (this.cycleMs >= limit) {
      this.cycleMs = 0;
      this.speaking = !this.speaking;
      if (this.speaking) this.burstMs = 1400 + Math.random() * 1200;
      else this.pauseMs = 900 + Math.random() * 500;
    }

    const samples = new Float32Array(this.frameSize);

    if (this.speaking) {
      // Sum a few harmonics around a drifting fundamental to land in the
      // energy/ZCR region the detector associates with voiced speech.
      const f0 = 130 + Math.sin(this.elapsedMs / 700) * 35;
      // Syllabic amplitude modulation at ~4 Hz, the rate of natural speech.
      const syllable = 0.55 + 0.45 * Math.abs(Math.sin(this.elapsedMs / 125));
      for (let i = 0; i < samples.length; i++) {
        this.phase += (2 * Math.PI * f0) / SAMPLE_RATE;
        samples[i] =
          syllable *
          0.22 *
          (Math.sin(this.phase) +
            0.5 * Math.sin(2 * this.phase) +
            0.25 * Math.sin(3 * this.phase));
      }
    } else {
      // Room tone, well under the detector's absolute floor.
      for (let i = 0; i < samples.length; i++) {
        samples[i] = (Math.random() - 0.5) * 0.004;
      }
    }

    this.onFrame({
      samples,
      sampleRate: SAMPLE_RATE,
      rms: rms(samples),
      timestamp: this.elapsedMs,
    });
  }
}
