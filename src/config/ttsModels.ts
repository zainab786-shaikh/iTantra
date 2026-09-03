import { LANGUAGES } from './languages';
import type { TTSModelType, TtsModelSource } from './ttsModelTypes';

/**
 * A TTS voice this app knows how to install and run.
 *
 * `id` is the on-disk directory name under the TTS sideload/install root —
 * see TtsModelManager. It intentionally does not collide with any STT model
 * id: TTS and STT models live under separate directories and are managed by
 * separate code, per the "STT is frozen" constraint for this workstream.
 */
export interface TtsModelDescriptor {
  id: string;
  label: string;
  modelType: TTSModelType;
  /** Language codes (iTantraPacket.language values) this voice serves. */
  languages: readonly string[];
  /** Approximate installed size, for the UI to warn before a large transfer. */
  approxMb: number;
  source: TtsModelSource;
}

const TTS_RELEASE_BASE =
  'https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/';
const MMS_HF_BASE =
  'https://huggingface.co/willwade/mms-tts-multilingual-models-onnx/resolve/main/';

/**
 * Piper voices, one per language. Packaged sherpa-onnx release archives —
 * verified by extracting one: each contains `<voice>.onnx`, `tokens.txt`,
 * and a shared `espeak-ng-data/` phoneme database (Piper's VITS variant
 * needs espeak-ng for text-to-phoneme conversion; MMS does not).
 *
 * MIT-licensed per the individual voice's Hugging Face model card — verified
 * directly, not assumed from the repo's blanket license badge.
 */
export const PIPER_ENGLISH: TtsModelDescriptor = {
  id: 'vits-piper-en_US-lessac-medium',
  label: 'English voice',
  modelType: 'vits',
  languages: ['en-IN'],
  approxMb: 64.1,
  source: {
    kind: 'archive',
    url: `${TTS_RELEASE_BASE}vits-piper-en_US-lessac-medium.tar.bz2`,
  },
};

export const PIPER_HINDI: TtsModelDescriptor = {
  id: 'vits-piper-hi_IN-pratham-medium',
  label: 'Hindi voice',
  modelType: 'vits',
  languages: ['hi-IN'],
  approxMb: 64.1,
  source: {
    kind: 'archive',
    url: `${TTS_RELEASE_BASE}vits-piper-hi_IN-pratham-medium.tar.bz2`,
  },
};

export const PIPER_MALAYALAM: TtsModelDescriptor = {
  id: 'vits-piper-ml_IN-arjun-medium',
  label: 'Malayalam voice',
  modelType: 'vits',
  languages: ['ml-IN'],
  approxMb: 64.1,
  source: {
    kind: 'archive',
    url: `${TTS_RELEASE_BASE}vits-piper-ml_IN-arjun-medium.tar.bz2`,
  },
};

/**
 * MMS-VITS voices for the 7 languages Piper does not cover.
 *
 * These are NOT in the official k2-fsa release (verified: only 8 languages
 * exist there, none of ours) — they come from the `willwade` community ONNX
 * conversion of Meta's MMS-TTS checkpoints, which ships as loose
 * `model.onnx` + `tokens.txt` files per language, no archive.
 *
 * License: CC-BY-NC-4.0, inherited from Meta's MMS release regardless of who
 * converted the format. Acceptable here only because this project's use is
 * non-commercial — see the model-selection research this decision came from.
 */
export const MMS_GUJARATI: TtsModelDescriptor = {
  id: 'mms-guj',
  label: 'Gujarati voice',
  modelType: 'vits',
  languages: ['gu-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}guj/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_MARATHI: TtsModelDescriptor = {
  id: 'mms-mar',
  label: 'Marathi voice',
  modelType: 'vits',
  languages: ['mr-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}mar/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_KANNADA: TtsModelDescriptor = {
  id: 'mms-kan',
  label: 'Kannada voice',
  modelType: 'vits',
  languages: ['kn-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}kan/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_TAMIL: TtsModelDescriptor = {
  id: 'mms-tam',
  label: 'Tamil voice',
  modelType: 'vits',
  languages: ['ta-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}tam/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_TELUGU: TtsModelDescriptor = {
  id: 'mms-tel',
  label: 'Telugu voice',
  modelType: 'vits',
  languages: ['te-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}tel/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_ODIA: TtsModelDescriptor = {
  id: 'mms-ory',
  label: 'Odia voice',
  modelType: 'vits',
  languages: ['or-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}ory/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

export const MMS_BENGALI: TtsModelDescriptor = {
  id: 'mms-ben',
  label: 'Bengali voice',
  modelType: 'vits',
  languages: ['bn-IN'],
  approxMb: 114,
  source: {
    kind: 'files',
    baseUrl: `${MMS_HF_BASE}ben/`,
    files: ['model.onnx', 'tokens.txt'],
  },
};

/** Registry of every TTS voice this app can install, one per language. */
export const TTS_MODELS: readonly TtsModelDescriptor[] = [
  PIPER_ENGLISH,
  PIPER_HINDI,
  PIPER_MALAYALAM,
  MMS_GUJARATI,
  MMS_MARATHI,
  MMS_KANNADA,
  MMS_TAMIL,
  MMS_TELUGU,
  MMS_ODIA,
  MMS_BENGALI,
];

/**
 * The single authoritative language -> TTS voice mapping.
 *
 * Reuses the same `LANGUAGES` codes the STT side already defines (see
 * config/languages.ts) so there is exactly one language-code system in the
 * app, not a parallel one for TTS.
 */
export function resolveTtsModelForLanguage(
  languageCode: string
): TtsModelDescriptor | null {
  return TTS_MODELS.find((m) => m.languages.includes(languageCode)) ?? null;
}

/** Sanity check, dev-time only: every app language has exactly one voice. */
if (__DEV__) {
  for (const lang of LANGUAGES) {
    if (!resolveTtsModelForLanguage(lang.code)) {
      console.warn(`[ttsModels] no TTS voice registered for ${lang.code}`);
    }
  }
}
