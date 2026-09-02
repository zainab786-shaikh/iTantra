/**
 * Mirror of react-native-sherpa-onnx's STTModelType union.
 *
 * Re-declared locally so that this app's config layer stays importable on
 * platforms where the native module cannot be resolved (web, Expo Go), where a
 * type-only import from the package would still drag in its module graph.
 */
export type STTModelType =
  | 'transducer'
  | 'nemo_transducer'
  | 'paraformer'
  | 'nemo_ctc'
  | 'wenet_ctc'
  | 'sense_voice'
  | 'zipformer_ctc'
  | 'ctc'
  | 'whisper'
  | 'funasr_nano'
  | 'qwen3_asr'
  | 'fire_red_asr'
  | 'moonshine'
  | 'dolphin'
  | 'canary'
  | 'omnilingual'
  | 'medasr'
  | 'telespeech_ctc'
  | 'auto';

/** Mirror of react-native-sherpa-onnx's ModelPathConfig. */
export type ModelPathConfig =
  | { type: 'asset'; path: string }
  | { type: 'file'; path: string }
  | { type: 'auto'; path: string };
