import type { STTModelType } from './modelTypes';

/**
 * A decoder the app knows how to install and run.
 *
 * `id` must match the sherpa-onnx release asset name with its archive extension
 * stripped — that is the identifier the library's download manager uses to look
 * a model up in the `asr-models` release of k2-fsa/sherpa-onnx.
 */
export interface SttModelDescriptor {
  /** Release asset name minus ".tar.bz2". Also the on-disk directory name. */
  id: string;
  label: string;
  /** sherpa-onnx model family. */
  modelType: STTModelType;
  /** Language codes this decoder can serve. */
  languages: readonly string[];
  /** Download size, for the UI to warn before a large transfer. */
  approxMb: number;
  /** Prefer int8 weights when the archive ships both. */
  preferInt8: boolean;
  /** ONNX threads. 2 is a sensible default on mid-range Android. */
  numThreads: number;
}

/**
 * Whisper base, multilingual.
 *
 * Chosen because it is the only realistically-sized model in the sherpa-onnx
 * release that actually covers Hindi and the other Indian languages in the
 * selector. Note there is NO dedicated Hindi or IndicConformer model in that
 * release — an earlier version of this file named one, and it did not exist.
 *
 * The ".en" Whisper variants are English-only and deliberately not used here.
 */
export const WHISPER_SMALL_MULTILINGUAL: SttModelDescriptor = {
  id: 'sherpa-onnx-whisper-small',
  label: 'Whisper small (English)',
  modelType: 'whisper',
  // English only, despite the model itself being multilingual. Whisper uses
  // byte-level BPE, so one token is often a partial UTF-8 sequence; this
  // library converts tokens to strings individually, and every partial sequence
  // collapses to an empty string. Verified on device: Hindi "हमें तुरंत मदद
  // चाहिए" decoded to tokens [" ह","म","े","ं"," ","","","","र"] — three empty
  // tokens where the dropped words should be. ASCII is one byte per character,
  // so English is unaffected. No Whisper size fixes this.
  languages: ['en-IN'],
  approxMb: 610,
  // int8 only. Full precision small is ~920 MB of weights, which is not a
  // sensible resident footprint on a phone; base showed no accuracy difference
  // between the two precisions, so the quantisation cost here is acceptable.
  preferInt8: true,
  numThreads: 2,
};

export const WHISPER_BASE_MULTILINGUAL: SttModelDescriptor = {
  id: 'sherpa-onnx-whisper-base',
  label: 'Whisper base (English)',
  modelType: 'whisper',
  /** English only, for the same byte-BPE reason as the small model. */
  languages: ['en-IN'],
  approxMb: 198,
  // Full precision. int8 was tried first for speed and memory, but Whisper base
  // is small enough that quantisation noticeably degraded accuracy on real
  // speech ("emergency" -> "emerging thing"), which is not a trade worth making
  // for an operational tool. Revisit only if decode latency becomes the binding
  // constraint.
  preferInt8: false,
  numThreads: 2,
};

/**
 * Dolphin base CTC, multilingual.
 *
 * Preferred over Whisper for this app. Its vocabulary carries native tokens for
 * every language in the selector — Hindi, Marathi, Tamil, Bengali, Telugu,
 * Kannada, Gujarati — so Indic output comes back in its own script rather than
 * romanised. It is also a CTC model, so decoding is a single forward pass
 * instead of Whisper's autoregressive loop: far faster on a phone, which matters
 * for push-to-talk.
 *
 * It detects language itself and takes no language hint, which also removes a
 * failure mode Whisper has here: forcing `language="hi"` onto English or mixed
 * speech made it emit Perso-Arabic script.
 */
export const DOLPHIN_SMALL_MULTILINGUAL: SttModelDescriptor = {
  id: 'sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02',
  label: 'Dolphin small CTC (multilingual)',
  modelType: 'dolphin',
  languages: [
    'hi-IN', 'mr-IN', 'ta-IN', 'bn-IN', 'te-IN',
    'kn-IN', 'gu-IN', 'ml-IN', 'or-IN',
  ],
  approxMb: 183,
  preferInt8: true,
  numThreads: 2,
};

export const DOLPHIN_BASE_MULTILINGUAL: SttModelDescriptor = {
  id: 'sherpa-onnx-dolphin-base-ctc-multi-lang-int8-2025-04-02',
  label: 'Dolphin base CTC (multilingual)',
  modelType: 'dolphin',
  // Every Indic language. Deliberately not English: Dolphin detects language
  // itself with no way to pin it, and on Indian-accented English it settled on
  // the wrong one and mixed scripts ("emogenسی" for "emergency").
  languages: [
    'hi-IN',
    'mr-IN',
    'ta-IN',
    'bn-IN',
    'te-IN',
    'kn-IN',
    'gu-IN',
    'ml-IN',
    'or-IN',
  ],
  approxMb: 77,
  preferInt8: true,
  numThreads: 2,
};

/**
 * Fast English CTC decoder.
 *
 * Whisper is accurate on English but decodes autoregressively — measured at
 * multiple seconds per utterance on device, which is too slow for push-to-talk.
 * A CTC model is a single forward pass and lands in the hundreds of
 * milliseconds.
 */
export const NEMO_CTC_ENGLISH: SttModelDescriptor = {
  id: 'sherpa-onnx-nemo-ctc-en-conformer-medium',
  label: 'NeMo CTC (English)',
  modelType: 'nemo_ctc',
  languages: ['en-IN'],
  approxMb: 64,
  preferInt8: true,
  numThreads: 2,
};

/**
 * Registry, most specific first.
 *
 * Whisper leads despite Dolphin being smaller and faster. Dolphin was tried on
 * device and heard the words correctly but rendered them in mixed scripts
 * ("emogenسی" for "emergency"): it detects language itself, the library exposes
 * no way to pin it, and it settled on the wrong one for Indian-accented English.
 * Whisper's forced-language option is worth its extra cost here.
 */
/**
 * Registry, most specific first. Routing is by language, because no single
 * decoder in the sherpa-onnx release serves both English and Indic well:
 * Whisper mangles Indic scripts, Dolphin mangles English.
 */
export const STT_MODELS: readonly SttModelDescriptor[] = [
  DOLPHIN_SMALL_MULTILINGUAL,
  DOLPHIN_BASE_MULTILINGUAL,
  NEMO_CTC_ENGLISH,
  WHISPER_SMALL_MULTILINGUAL,
  WHISPER_BASE_MULTILINGUAL,
];

/** Shown on the install card when no language is selected yet. */
export const PRIMARY_MODEL = DOLPHIN_SMALL_MULTILINGUAL;

/** Silero VAD weights, used by the optional ONNX VAD backend. */
export const SILERO_VAD_MODEL = {
  dir: 'models/silero-vad',
  file: 'silero_vad.onnx',
} as const;

/** The decoder that serves `languageCode`, or null if none claims it. */
export function resolveModelForLanguage(
  languageCode: string
): SttModelDescriptor | null {
  return STT_MODELS.find((m) => m.languages.includes(languageCode)) ?? null;
}
