import Constants, { ExecutionEnvironment } from 'expo-constants';

import { findLanguage } from '../../config/languages';
import { resolveModelForLanguage, type SttModelDescriptor } from '../../config/models';
import { SAMPLE_RATE } from '../../config/vadConfig';
import { toSampleArray } from '../audio/pcm';
import { hasSherpaOnnx, tryRequireFs } from '../nativeModules';
import { ModelManager } from './ModelManager';
import { repairScript } from './indicScript';
import type { SttBackend, SttTranscription } from './SttBackend';

/**
 * Offline recogniser backed by react-native-sherpa-onnx.
 *
 * The package is required lazily: it is a TurboModule with prebuilt native
 * libraries, so a static import would break the bundle on web and in Expo Go.
 *
 * Whisper takes its target language at construction time — there is no runtime
 * setter for it — so the engine is cached per (model, language) pair and rebuilt
 * when the operator switches languages. That rebuild costs one model load, which
 * is why `load()` is called ahead of time when the selector changes rather than
 * lazily on the first utterance.
 */
export class SherpaSttBackend implements SttBackend {
  readonly kind = 'sherpa-onnx' as const;

  private readonly models = new ModelManager();
  private engine: any = null;
  /** Cache key: `${modelId}:${whisperLang}`. */
  private loadedKey: string | null = null;
  private loading: Promise<void> | null = null;

  get isReady(): boolean {
    return this.engine !== null;
  }

  async load(languageCode: string): Promise<void> {
    const descriptor = resolveModelForLanguage(languageCode);
    if (!descriptor) {
      throw new Error(`No STT model registered for language "${languageCode}"`);
    }

    const key = `${descriptor.id}:${findLanguage(languageCode).sherpaLang}`;
    if (this.loadedKey === key && this.engine) return;

    // Collapse concurrent loads from rapid language switching.
    if (this.loading) await this.loading.catch(() => undefined);
    if (this.loadedKey === key && this.engine) return;

    this.loading = this.doLoad(descriptor, languageCode, key);
    try {
      await this.loading;
    } finally {
      this.loading = null;
    }
  }

  private async doLoad(
    descriptor: SttModelDescriptor,
    languageCode: string,
    key: string
  ): Promise<void> {
    const { createSTT } = requireSherpaStt();

    const path = await this.models.resolvePath(descriptor);
    if (!path) {
      throw new Error(
        `The "${descriptor.label}" model is not installed on this device yet. ` +
          'Download it from the transmitter screen.'
      );
    }

    await this.disposeEngine();

    const whisperLang = findLanguage(languageCode).sherpaLang;
    console.log(
      `[SherpaSTT] Loading model: ${descriptor.label} (${descriptor.id})\n` +
        `  Path: ${path}\n` +
        `  Type: ${descriptor.modelType}\n` +
        `  Language: ${findLanguage(languageCode).label} (${languageCode})`
    );

    this.engine = await createSTT({
      modelPath: { type: 'file', path },
      modelType: descriptor.modelType,
      preferInt8: descriptor.preferInt8,
      numThreads: descriptor.numThreads,
      // Only Whisper takes a forced language. Dolphin detects it itself, and
      // sending it a whisper block would be meaningless.
      ...(descriptor.modelType === 'whisper'
        ? {
            modelOptions: {
              whisper: {
                language: whisperLang,
                // Never 'translate': that would silently return English for
                // Hindi speech, which reads as a mistranscription rather than
                // a setting.
                task: 'transcribe',
              },
            },
          }
        : {}),
    });
    this.loadedKey = key;

    if (__DEV__) void this.selfTest(path, languageCode);
  }

  /**
   * Decode a known-good reference clip through the freshly loaded engine.
   *
   * Isolates a weak model from a broken audio path: the clip ships with the
   * sherpa-onnx archive at a known transcript, and it is 16 kHz mono PCM-16 —
   * byte-identical in format to what the microphone pipeline produces. If this
   * decodes correctly but live speech does not, the fault is in capture, not in
   * the recogniser.
   *
   * Dev builds only, and entirely best-effort.
   */
  private async selfTest(modelDir: string, languageCode: string): Promise<void> {
    try {
      // Prefer a clip recorded for this language; fall back to the generic one.
      const lang = languageCode.split('-')[0];
      let clip = `${modelDir}/selftest-${lang}.wav`;
      const fs = tryRequireFs();
      if (fs && !(await fs.exists(clip))) clip = `${modelDir}/selftest.wav`;

      const startedAt = Date.now();
      const result = await this.engine?.transcribeFile(clip);
      const elapsed = Date.now() - startedAt;
      console.log(
        `[SherpaSTT][selftest] ${languageCode} decode took ${elapsed} ms`
      );
      const text = typeof result?.text === 'string' ? result.text.trim() : '';
      if (text.length === 0) return;
      // Run the same repair the live path applies, so the self-test reflects
      // what the operator would actually see rather than the raw engine output.
      const repaired = repairScript(text, languageCode);
      console.log(`[SherpaSTT][selftest] raw     : "${text}"`);
      console.log(
        `[SherpaSTT][selftest] repaired: "${repaired.rejected ? '<rejected>' : repaired.text}"`
      );
      // Per-token output separates a misrecognition from a text-assembly bug:
      // if individual tokens are already fragments, the decode was fine and the
      // damage happened while joining them.
      console.log(
        `[SherpaSTT][selftest] tokens: ${JSON.stringify(result?.tokens ?? [])}`
      );
      console.log(
        '[SherpaSTT][selftest] expected: "AFTER EARLY NIGHTFALL THE YELLOW ' +
          'LAMPS WOULD LIGHT UP HERE AND THERE THE SQUALID QUARTER OF THE BROTHELS"'
      );
    } catch {
      // No reference clip installed, or the engine refused it. Not a failure.
    }
  }

  async transcribe(
    samples: Float32Array,
    languageCode: string
  ): Promise<SttTranscription> {
    await this.load(languageCode);
    if (!this.engine) throw new Error('STT engine unavailable');

    // --- diagnostic instrumentation (temporary — STT accuracy investigation) ---
    // Additive only: no behavior/threshold/control-flow change. Logs decode
    // timing and the audio duration actually handed to the model, so we can
    // tell a genuine model/recognition problem apart from upstream audio
    // truncation.
    const audioDurationMs = (samples.length / SAMPLE_RATE) * 1000;
    const decodeStartedAt = Date.now();

    // Float32Array -> number[] happens once per utterance, not per frame, so it
    // stays off the audio hot path.
    const result = await this.engine.transcribeSamples(
      toSampleArray(samples),
      SAMPLE_RATE
    );

    const decodeMs = Date.now() - decodeStartedAt;
    const audioSec = (audioDurationMs / 1000).toFixed(2);
    const decodeSec = (decodeMs / 1000).toFixed(2);

    const raw = typeof result?.text === 'string' ? result.text.trim() : '';
    const repair = repairScript(raw, languageCode);

    console.log(
      `[IndicASR] Diagnostics:\n` +
        `  Language: ${findLanguage(languageCode).label} (${languageCode})\n` +
        `  Sample Rate: ${SAMPLE_RATE} Hz (PCM Float32 mono)\n` +
        `  Audio Duration: ${audioSec} sec\n` +
        `  Inference Time: ${decodeSec} sec (${decodeMs} ms)\n` +
        `  Raw Text: "${raw}"\n` +
        `  Result: "${repair.text}"`
    );

    if (repair.transliteratedFrom) {
      console.log(
        `[indicScript] decoded in ${repair.transliteratedFrom}, ` +
          `transliterated to ${languageCode}: "${raw}" -> "${repair.text}"`
      );
    }
    if (repair.rejected) {
      console.warn(
        `[indicScript] rejected unrecoverable output for ${languageCode}: "${raw}"`
      );
    }

    return {
      text: repair.text,
      rawText: raw,
      ...(typeof result?.lang === 'string' && result.lang.length > 0
        ? { detectedLanguage: result.lang }
        : {}),
    };
  }

  private async disposeEngine(): Promise<void> {
    const current = this.engine;
    this.engine = null;
    this.loadedKey = null;
    try {
      await current?.destroy?.();
    } catch {
      // Best effort; a failed teardown must not block the next load.
    }
  }

  async dispose(): Promise<void> {
    await this.disposeEngine();
  }
}

/** Resolve the sherpa-onnx STT subpath at runtime. */
function requireSherpaStt(): { createSTT: (options: any) => Promise<any> } {
  if (!hasSherpaOnnx()) {
    throw new Error(
      'Expo Go cannot load react-native-sherpa-onnx (it ships native libraries). ' +
        'Run `npx expo run:android` for a development build to enable real decoding.'
    );
  }

  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const mod = require('react-native-sherpa-onnx/stt');
    if (typeof mod?.createSTT !== 'function') {
      throw new Error('createSTT missing from react-native-sherpa-onnx/stt');
    }
    return mod;
  } catch (error) {
    throw new Error(`react-native-sherpa-onnx unavailable: ${String(error)}`);
  }
}
