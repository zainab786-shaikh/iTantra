import type { STTModelType } from './modelTypes';

/**
 * A language the transmitter can decode. `code` is what travels in the packet;
 * `sherpaLang` is the hint passed to multilingual models (Dolphin, Whisper) that
 * accept a language token.
 */
export interface TransmitterLanguage {
  /** BCP-47-ish code carried in iTantraPacket.language. */
  code: string;
  /** English label. */
  label: string;
  /** Native-script label, shown in the selector. */
  native: string;
  /** Language token understood by multilingual sherpa-onnx models. */
  sherpaLang: string;
  /** Two-letter tag rendered in the compact chip. */
  short: string;
  /** Accent colour used by the UI for this language. */
  accent: string;
  /** Model type expected for this language's dedicated model, when one is bundled. */
  preferredModelType: STTModelType;
}

/**
 * Indian English and Hindi lead because those are the two shipped/verified
 * decoders; the rest are wired and switchable but depend on the matching model
 * directory being present on device.
 */
export const LANGUAGES: readonly TransmitterLanguage[] = [
  {
    code: 'en-IN',
    label: 'English',
    native: 'English',
    sherpaLang: 'en',
    short: 'EN',
    accent: '#5EEAD4',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'hi-IN',
    label: 'Hindi',
    native: 'हिन्दी',
    sherpaLang: 'hi',
    short: 'HI',
    accent: '#FDBA74',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'mr-IN',
    label: 'Marathi',
    native: 'मराठी',
    sherpaLang: 'mr',
    short: 'MR',
    accent: '#C4B5FD',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'gu-IN',
    label: 'Gujarati',
    native: 'ગુજરાતી',
    sherpaLang: 'gu',
    short: 'GU',
    accent: '#FDE68A',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'kn-IN',
    label: 'Kannada',
    native: 'ಕನ್ನಡ',
    sherpaLang: 'kn',
    short: 'KN',
    accent: '#FCA5A5',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'ml-IN',
    label: 'Malayalam',
    native: 'മലയാളം',
    sherpaLang: 'ml',
    short: 'ML',
    accent: '#A5B4FC',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'ta-IN',
    label: 'Tamil',
    native: 'தமிழ்',
    sherpaLang: 'ta',
    short: 'TA',
    accent: '#F9A8D4',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'te-IN',
    label: 'Telugu',
    native: 'తెలుగు',
    sherpaLang: 'te',
    short: 'TE',
    accent: '#BEF264',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'or-IN',
    label: 'Odia',
    native: 'ଓଡ଼ିଆ',
    sherpaLang: 'or',
    short: 'OD',
    accent: '#6EE7B7',
    preferredModelType: 'nemo_ctc',
  },
  {
    code: 'bn-IN',
    label: 'Bengali',
    native: 'বাংলা',
    sherpaLang: 'bn',
    short: 'BN',
    accent: '#7DD3FC',
    preferredModelType: 'nemo_ctc',
  },
] as const;

export const DEFAULT_LANGUAGE = LANGUAGES[0]!;

export function findLanguage(code: string): TransmitterLanguage {
  return LANGUAGES.find((l) => l.code === code) ?? DEFAULT_LANGUAGE;
}
