import Constants, { ExecutionEnvironment } from 'expo-constants';

import { resolveTtsModelForLanguage } from '../../config/ttsModels';
import { SAMPLE_RATE } from '../../config/vadConfig';
import { resampleLinear } from '../audio/pcm';
import { TtsModelManager } from '../tts/TtsModelManager';

/**
 * Shared TTS-synthesis step for STT diagnostics. TEMPORARY, dev-only.
 *
 * Extracted from SttRoundTripTest.ts so the VAD+segmenter round-trip test and
 * the direct-to-STT bypass test start from the exact same audio, produced the
 * exact same way — otherwise a difference between the two results could be an
 * artifact of two different synthesis paths instead of the pipeline stage
 * being isolated.
 */
export interface SynthesizedReference {
  /** 16 kHz mono, normalized [-1, 1]. */
  samples: Float32Array;
  durationMs: number;
  sampleRate: number;
}

export async function synthesizeReferenceAudio(
  languageCode: string,
  referenceText: string
): Promise<SynthesizedReference> {
  const ttsModel = resolveTtsModelForLanguage(languageCode);
  if (!ttsModel) {
    throw new Error(`No TTS voice registered for ${languageCode}`);
  }
  const ttsModels = new TtsModelManager();
  const ttsPath = await ttsModels.resolvePath(ttsModel);
  if (!ttsPath) {
    throw new Error(
      `TTS voice for ${languageCode} is not installed — cannot generate reference audio`
    );
  }

  const { createTTS } = requireSherpaTts();
  const ttsEngine = await createTTS({
    modelPath: { type: 'file', path: ttsPath },
    modelType: ttsModel.modelType,
    numThreads: 2,
  });

  try {
    const audio = await ttsEngine.generateSpeech(referenceText);
    const samples = resampleLinear(
      Float32Array.from(audio.samples as number[]),
      audio.sampleRate,
      SAMPLE_RATE
    );
    return {
      samples,
      durationMs: (samples.length / SAMPLE_RATE) * 1000,
      sampleRate: SAMPLE_RATE,
    };
  } finally {
    await ttsEngine.destroy?.().catch(() => undefined);
  }
}

function requireSherpaTts(): { createTTS: (options: any) => Promise<any> } {
  if (Constants.executionEnvironment === ExecutionEnvironment.StoreClient) {
    throw new Error('Expo Go cannot load react-native-sherpa-onnx.');
  }
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const mod = require('react-native-sherpa-onnx/tts');
  if (typeof mod?.createTTS !== 'function') {
    throw new Error('createTTS missing from react-native-sherpa-onnx/tts');
  }
  return mod;
}
