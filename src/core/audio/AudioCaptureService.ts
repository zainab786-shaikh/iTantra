import { SAMPLE_RATE } from '../../config/vadConfig';
import type { PcmFrame } from '../types';
import { int16ToFloat32, resampleLinear, rms } from './pcm';

/**
 * Turns the variable-length PCM buffers delivered by the OS into the fixed-size
 * frames the VAD needs.
 *
 * expo-audio hands us roughly 100 ms per callback (1600 samples at 16 kHz) but
 * makes no guarantee about the exact length, and it silently falls back to a
 * different sample rate when the hardware refuses 16 kHz. This class absorbs
 * both: it resamples to 16 kHz when needed and re-chops the stream into exact
 * `frameSize` frames, carrying the remainder across callbacks.
 *
 * It is deliberately not a React construct — it holds a mutable ring of audio
 * and must survive re-renders untouched.
 */
export class AudioCaptureService {
  private readonly frameSize: number;
  private readonly onFrame: (frame: PcmFrame) => void;

  /** Samples left over from the previous buffer, awaiting a full frame. */
  private residue: Float32Array = new Float32Array(0);
  /** Total 16 kHz samples emitted, used to derive a monotonic timestamp. */
  private samplesEmitted = 0;
  /** Sample rate actually delivered by the hardware. */
  private hardwareRate: number = SAMPLE_RATE;

  constructor(frameSize: number, onFrame: (frame: PcmFrame) => void) {
    this.frameSize = frameSize;
    this.onFrame = onFrame;
  }

  /** Discard buffered audio and reset the clock. Call on every start(). */
  reset(): void {
    this.residue = new Float32Array(0);
    this.samplesEmitted = 0;
  }

  /** The rate the microphone actually opened at, for diagnostics. */
  get actualSampleRate(): number {
    return this.hardwareRate;
  }

  /**
   * Feed one native buffer. Emits zero or more complete frames synchronously.
   *
   * @param data int16 little-endian PCM, mono.
   * @param sampleRate the rate the hardware actually delivered.
   */
  pushBuffer(data: ArrayBuffer, sampleRate: number): void {
    this.hardwareRate = sampleRate;

    let samples = int16ToFloat32(data);
    if (sampleRate !== SAMPLE_RATE) {
      samples = resampleLinear(samples, sampleRate, SAMPLE_RATE);
    }

    // Splice the carry-over in front of the new audio.
    const combined =
      this.residue.length === 0
        ? samples
        : (() => {
            const merged = new Float32Array(this.residue.length + samples.length);
            merged.set(this.residue, 0);
            merged.set(samples, this.residue.length);
            return merged;
          })();

    let offset = 0;
    while (combined.length - offset >= this.frameSize) {
      // subarray() is a view, so no copy: the frame borrows `combined`'s memory.
      // Consumers must not retain it beyond the callback without copying.
      const frame = combined.subarray(offset, offset + this.frameSize);
      offset += this.frameSize;
      this.samplesEmitted += this.frameSize;

      this.onFrame({
        samples: frame,
        sampleRate: SAMPLE_RATE,
        rms: rms(frame),
        timestamp: (this.samplesEmitted / SAMPLE_RATE) * 1000,
      });
    }

    this.residue =
      offset === 0 ? combined : combined.slice(offset);
  }
}
