import { SAMPLE_RATE } from '../../config/vadConfig';
import type { SttBackend, SttTranscription } from './SttBackend';

/**
 * Placeholder used when no real decoder can be loaded — Expo Go, web, or a
 * development build with no model installed.
 *
 * IT DOES NOT LISTEN. There is no recognition here of any kind. It reports
 * what the pipeline measured about the audio (how long it was, how loud) and
 * says plainly that no decoder is installed.
 *
 * An earlier version returned realistic-sounding sentences so the UI could be
 * demonstrated, which was a mistake: the output looked like a bad transcription
 * rather than an absent one, and made it seem the recogniser was mishearing.
 * Anything this class emits must be impossible to mistake for a transcript.
 */
export class SimulatedSttBackend implements SttBackend {
  readonly kind = 'simulated' as const;
  readonly isReady = true;

  async load(): Promise<void> {
    // Nothing to load.
  }

  async transcribe(
    samples: Float32Array,
    // Kept to satisfy SttBackend and the provider's call sites. Unused: there
    // is nothing here that could vary by language.
    _languageCode?: string
  ): Promise<SttTranscription> {
    const durationMs = (samples.length / SAMPLE_RATE) * 1000;
    const seconds = (durationMs / 1000).toFixed(1);

    // Peak amplitude confirms the microphone is genuinely feeding the pipeline,
    // which is the one useful thing this stand-in can report.
    let peak = 0;
    for (let i = 0; i < samples.length; i++) {
      const v = Math.abs(samples[i]!);
      if (v > peak) peak = v;
    }
    const loudness = Math.round(peak * 100);

    const text =
      `[no speech model installed — captured ${seconds}s of your audio ` +
      `at ${loudness}% peak level, but there is no decoder to read it]`;
    return { text, rawText: text };
  }

  async dispose(): Promise<void> {
    // Nothing to release.
  }
}
