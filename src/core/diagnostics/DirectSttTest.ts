import { SAMPLE_RATE } from '../../config/vadConfig';
import type { SttEngineProvider } from '../stt/SttEngineProvider';
import { synthesizeReferenceAudio } from './synthesizeReferenceAudio';

/**
 * Direct TTS -> STT diagnostic. TEMPORARY, dev-only — bypasses EnergyVad,
 * SentenceSegmenter, and the voiced-ratio gate entirely.
 *
 * Purpose: isolate the recognizer from the capture pipeline. runSttRoundTripTest
 * answers "does TTS audio survive VAD + segmentation + STT". This answers "can
 * the model transcribe this audio at all", by handing the complete synthesized
 * clip straight to sttProvider.transcribe() with no gate in between. Comparing
 * the two:
 *
 *   - round-trip fails, direct succeeds -> VAD/segmentation is implicated.
 *   - both fail -> model/configuration/audio-format is more likely than not.
 *   - direct succeeds -> do not blame the STT model yet either way; this is
 *     still one synthetic sentence, not a corpus-level accuracy measurement.
 *
 * Does not modify the production STT pipeline, VAD thresholds, or segmentation.
 */
export interface DirectSttResult {
  language: string;
  referenceText: string;
  /** Confirms the audio handed to the engine matches what Sherpa expects. */
  audioFormat: {
    sampleRate: number;
    channels: 1;
    encoding: 'float32-normalized';
    numSamples: number;
    durationMs: number;
    formatOk: boolean;
  };
  modelLoaded: boolean;
  modelLoadReason: string | null;
  rawSttText: string | null;
  finalSttText: string | null;
  detectedLanguage: string | null;
  decodeMs: number | null;
  error: string | null;
}

export async function runDirectSttTest(
  languageCode: string,
  referenceText: string,
  sttProvider: SttEngineProvider
): Promise<DirectSttResult> {
  const base: DirectSttResult = {
    language: languageCode,
    referenceText,
    audioFormat: {
      sampleRate: SAMPLE_RATE,
      channels: 1,
      encoding: 'float32-normalized',
      numSamples: 0,
      durationMs: 0,
      formatOk: false,
    },
    modelLoaded: false,
    modelLoadReason: null,
    rawSttText: null,
    finalSttText: null,
    detectedLanguage: null,
    decodeMs: null,
    error: null,
  };

  try {
    const reference = await synthesizeReferenceAudio(languageCode, referenceText);

    // Verify the format actually handed to the STT engine, not just assumed.
    // Sherpa's transcribeSamples() call (SherpaSttBackend.transcribe) expects
    // 16 kHz mono, normalized Float32 in [-1, 1] — the same contract
    // synthesizeReferenceAudio produces via resampleLinear to SAMPLE_RATE.
    const numSamples = reference.samples.length;
    let peak = 0;
    for (let i = 0; i < numSamples; i++) {
      const v = Math.abs(reference.samples[i]!);
      if (v > peak) peak = v;
    }
    const formatOk =
      reference.sampleRate === SAMPLE_RATE && numSamples > 0 && peak <= 1.0001;

    const audioFormat = {
      sampleRate: reference.sampleRate,
      channels: 1 as const,
      encoding: 'float32-normalized' as const,
      numSamples,
      durationMs: reference.durationMs,
      formatOk,
    };

    if (!formatOk) {
      return {
        ...base,
        audioFormat,
        error: `Synthesized audio failed the expected-format check (sampleRate=${reference.sampleRate}, samples=${numSamples}, peak=${peak.toFixed(3)})`,
      };
    }

    const prepared = await sttProvider.prepare(languageCode);
    const modelLoaded = prepared.kind === 'sherpa-onnx';
    if (!modelLoaded) {
      return {
        ...base,
        audioFormat,
        modelLoaded: false,
        modelLoadReason: prepared.reason,
        error: `STT engine did not load the real model for ${languageCode}: ${prepared.reason ?? 'unknown reason'}`,
      };
    }

    // The whole point of this test: no EnergyVad, no SentenceSegmenter, no
    // voiced-ratio gate. The complete synthesized clip goes straight in.
    const decodeStart = Date.now();
    const { text, rawText, detectedLanguage } = await sttProvider.transcribe(
      reference.samples,
      languageCode
    );
    const decodeMs = Date.now() - decodeStart;

    return {
      ...base,
      audioFormat,
      modelLoaded: true,
      modelLoadReason: prepared.reason,
      rawSttText: rawText ?? null,
      finalSttText: text,
      detectedLanguage: detectedLanguage ?? null,
      decodeMs,
    };
  } catch (error) {
    return { ...base, error: error instanceof Error ? error.message : String(error) };
  }
}
