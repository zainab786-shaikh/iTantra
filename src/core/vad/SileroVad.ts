import { SAMPLE_RATE } from '../../config/vadConfig';
import type { VadBackend } from './VadBackend';

/**
 * Silero VAD v5 running through onnxruntime-react-native.
 *
 * This is opt-in. `react-native-sherpa-onnx@0.4.3` exposes a `/vad` subpath, but
 * every function in it throws "VAD feature is not yet implemented" — it is a
 * documented placeholder — so Silero is driven directly through ONNX Runtime
 * instead. To enable:
 *
 *   npx expo install onnxruntime-react-native
 *   # place silero_vad.onnx at assets/models/silero-vad/ (see config/models.ts)
 *
 * Both the runtime and the weights are resolved lazily so that neither the
 * bundler nor a web build ever has to see them. When either is missing, the
 * engine silently keeps the EnergyVad default.
 *
 * Inference is pipelined: ONNX Runtime's `run` is async but `process()` is on
 * the audio hot path and must stay synchronous, so each call kicks off
 * inference for the current frame and returns the probability computed for the
 * previous one. That costs exactly one frame (32 ms) of extra reaction time,
 * which is immaterial against a 600-1000 ms end-of-speech pause.
 */
export class SileroVad implements VadBackend {
  readonly kind = 'silero-onnx' as const;

  private session: any = null;
  private ort: any = null;
  /** Silero v5 recurrent state, shape [2, 1, 128]. */
  private state = new Float32Array(2 * 1 * 128);
  private lastProbability = 0;
  private inFlight = false;
  private readonly modelUri: string;

  constructor(modelUri: string) {
    this.modelUri = modelUri;
  }

  async initialize(): Promise<void> {
    if (this.session) return;

    // Optional peer dependency: resolved at runtime so a missing package is a
    // graceful downgrade rather than a bundling failure.
    let ort: any;
    try {
      // eslint-disable-next-line @typescript-eslint/no-require-imports
      ort = require('onnxruntime-react-native');
    } catch {
      throw new Error(
        'onnxruntime-react-native is not installed; Silero VAD unavailable'
      );
    }

    this.ort = ort;
    this.session = await ort.InferenceSession.create(this.modelUri);
    this.reset();
  }

  process(frame: Float32Array): number {
    if (!this.session || !this.ort) return this.lastProbability;

    // Drop frames while a previous inference is still running rather than
    // queueing them; a backlog would make the probability lag reality.
    if (!this.inFlight) {
      this.inFlight = true;
      void this.infer(frame).finally(() => {
        this.inFlight = false;
      });
    }
    return this.lastProbability;
  }

  private async infer(frame: Float32Array): Promise<void> {
    try {
      const { Tensor } = this.ort;
      // Copy: `frame` is a view into the capture service's buffer and will be
      // overwritten before the async run completes.
      const input = new Tensor('float32', Float32Array.from(frame), [
        1,
        frame.length,
      ]);
      const feeds: Record<string, unknown> = {
        input,
        state: new Tensor('float32', this.state, [2, 1, 128]),
        sr: new Tensor('int64', BigInt64Array.from([BigInt(SAMPLE_RATE)]), [1]),
      };

      const output = await this.session.run(feeds);
      const prob = output.output?.data?.[0];
      if (typeof prob === 'number') this.lastProbability = prob;

      const nextState = output.stateN?.data;
      if (nextState) this.state = Float32Array.from(nextState as Float32Array);
    } catch {
      // A failed inference must not take the pipeline down; hold the last value
      // and let the next frame try again.
    }
  }

  reset(): void {
    this.state = new Float32Array(2 * 1 * 128);
    this.lastProbability = 0;
    this.inFlight = false;
  }

  async dispose(): Promise<void> {
    try {
      await this.session?.release?.();
    } catch {
      // Best effort.
    }
    this.session = null;
    this.ort = null;
  }
}
