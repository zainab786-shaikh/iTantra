/**
 * Mirror of react-native-sherpa-onnx's TTSModelType union.
 *
 * Re-declared locally for the same reason as `STTModelType` in modelTypes.ts:
 * this app's config layer must stay importable on platforms where the native
 * module cannot be resolved (web, Expo Go), and a type-only import from the
 * package would still drag in its module graph.
 */
export type TTSModelType =
  | 'vits'
  | 'matcha'
  | 'kokoro'
  | 'kitten'
  | 'pocket'
  | 'zipvoice'
  | 'supertonic'
  | 'auto';

/**
 * How a TTS model's files are fetched.
 *
 * The two TTS sources this app uses are packaged differently and that
 * difference is real, not an implementation detail to paper over:
 *
 * - Piper voices ship from the k2-fsa/sherpa-onnx `tts-models` GitHub release
 *   as a single `.tar.bz2` archive (model + tokens.txt + espeak-ng-data),
 *   same shape as the STT archives this app already downloads.
 * - The MMS-VITS conversions this app uses live as loose files in a
 *   Hugging Face repo (`model.onnx` + `tokens.txt`, no archive wrapper) —
 *   there is no equivalent packaged release for these languages.
 */
export type TtsModelSource =
  | { kind: 'archive'; url: string }
  | { kind: 'files'; baseUrl: string; files: readonly string[] };
