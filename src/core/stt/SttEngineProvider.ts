import type { SttEngineKind } from '../types';
import { SherpaSttBackend } from './SherpaSttBackend';
import { SimulatedSttBackend } from './SimulatedSttBackend';
import type { SttBackend, SttTranscription } from './SttBackend';

export interface SttProviderStatus {
  kind: SttEngineKind;
  /** Why the real decoder is not in use, when it is not. */
  reason: string | null;
}

/**
 * Chooses and owns the active recogniser.
 *
 * Resolution order is: sherpa-onnx if its native module loads *and* the model
 * directory for the requested language is present, otherwise the simulated
 * backend. The decision is made once per language and remembered, so a missing
 * model does not cost a failed native call on every utterance.
 */
export class SttEngineProvider {
  private sherpa: SherpaSttBackend | null = null;
  private simulated = new SimulatedSttBackend();
  private active: SttBackend = this.simulated;
  private reason: string | null = null;
  /** Languages already known to have no working native decoder. */
  private readonly unsupported = new Set<string>();

  get status(): SttProviderStatus {
    return { kind: this.active.kind, reason: this.reason };
  }

  /**
   * Prepare a decoder for `languageCode`.
   * Never rejects: a failure downgrades to the simulated backend and is
   * reported through {@link status}.
   */
  async prepare(languageCode: string): Promise<SttProviderStatus> {
    if (this.unsupported.has(languageCode)) {
      this.active = this.simulated;
      return this.status;
    }

    try {
      this.sherpa ??= new SherpaSttBackend();
      await this.sherpa.load(languageCode);
      this.active = this.sherpa;
      this.reason = null;
    } catch (error) {
      this.unsupported.add(languageCode);
      this.active = this.simulated;
      this.reason = extractMessage(error);
      // Loud on purpose: this is the moment a language silently loses its real
      // decoder, and swallowing it made a path-cache bug look like a model bug.
      console.warn(
        `[SttEngineProvider] no native decoder for "${languageCode}", ` +
          `falling back to the placeholder: ${this.reason}`
      );
    }
    return this.status;
  }

  /**
   * Decode one utterance. Falls back mid-flight if the native decoder throws,
   * so a runtime model failure still produces a packet instead of a dead end.
   */
  async transcribe(
    samples: Float32Array,
    languageCode: string
  ): Promise<SttTranscription> {
    try {
      return await this.active.transcribe(samples, languageCode);
    } catch (error) {
      if (this.active.kind !== 'simulated') {
        this.unsupported.add(languageCode);
        this.reason = extractMessage(error);
        this.active = this.simulated;
        console.warn(
          `[SttEngineProvider] decode failed for "${languageCode}": ${this.reason}`
        );
        return this.simulated.transcribe(samples, languageCode);
      }
      throw error;
    }
  }

  /**
   * Forget every cached "this language has no decoder" verdict.
   * Call after installing a model, or the provider keeps using the placeholder
   * for the rest of the session even though a real decoder now exists.
   */
  async reset(): Promise<void> {
    this.unsupported.clear();
    this.reason = null;
    this.active = this.simulated;

    // Tear the old backend down before dropping the reference. It may hold a
    // native recognizer; letting it go unreferenced would leak that until the
    // process dies.
    const previous = this.sherpa;
    this.sherpa = null;
    try {
      await previous?.dispose();
    } catch {
      // Best effort — a failed teardown must not block the retry.
    }
  }

  async dispose(): Promise<void> {
    await this.sherpa?.dispose();
    await this.simulated.dispose();
    this.sherpa = null;
    this.active = this.simulated;
  }
}

function extractMessage(error: unknown): string {
  if (error instanceof Error) return error.message;
  return String(error);
}
