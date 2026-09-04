/**
 * Repairs Dolphin's script confusion between Indian languages.
 *
 * Dolphin identifies the language of each segment itself and this library
 * exposes no way to pin it, so it regularly decodes the right *sounds* into the
 * wrong *script*: Bengali speech came back on device as "अपनार ओबस्थान जान" —
 * phonetically "apnar obosthan janan", correct word for word, written in
 * Devanagari instead of Bengali.
 *
 * That is recoverable. The Unicode Indic blocks are deliberately parallel: all
 * of them descend from ISCII and place the same phoneme at the same offset
 * within their block, so converting between them is an arithmetic shift of the
 * code point. Transliterating is therefore lossless for the shared letters and
 * far better than discarding the text.
 *
 * Output in a script with no such correspondence — Arabic, Cyrillic, Han — is
 * not repairable and is rejected instead, so the operator sees nothing rather
 * than a fragment.
 */

/** Start of each Indic block, which is also its transliteration origin. */
const BLOCKS: Record<string, number> = {
  devanagari: 0x0900,
  bengali: 0x0980,
  gujarati: 0x0a80,
  oriya: 0x0b00,
  tamil: 0x0b80,
  telugu: 0x0c00,
  kannada: 0x0c80,
  malayalam: 0x0d00,
};

const BLOCK_SIZE = 0x80;

/** The script each language is written in. */
const LANGUAGE_SCRIPT: Record<string, keyof typeof BLOCKS> = {
  'hi-IN': 'devanagari',
  'mr-IN': 'devanagari',
  'bn-IN': 'bengali',
  'gu-IN': 'gujarati',
  'ta-IN': 'tamil',
  'te-IN': 'telugu',
  'kn-IN': 'kannada',
  'ml-IN': 'malayalam',
  'or-IN': 'oriya',
};

/** Which Indic block a code point belongs to, or null. */
function blockOf(code: number): keyof typeof BLOCKS | null {
  for (const [name, start] of Object.entries(BLOCKS)) {
    if (code >= start && code < start + BLOCK_SIZE) {
      return name as keyof typeof BLOCKS;
    }
  }
  return null;
}

function isLatinOrPunctuation(code: number): boolean {
  return (
    code <= 0x007f ||
    (code >= 0x00a0 && code <= 0x024f) ||
    (code >= 0x2010 && code <= 0x2027)
  );
}

export interface ScriptRepair {
  text: string;
  /** True when the output could not be salvaged and should be discarded. */
  rejected: boolean;
  /** Set when characters were shifted from another Indic script. */
  transliteratedFrom?: string;
}

/**
 * Coerce `text` into the script `languageCode` is written in.
 *
 * Latin is always preserved: code-switching into English ("sector four",
 * "over") is normal in Indian radio traffic.
 */
export function repairScript(text: string, languageCode: string): ScriptRepair {
  const target = LANGUAGE_SCRIPT[languageCode];
  if (!target) return { text, rejected: false };

  const targetStart = BLOCKS[target]!;

  // Tally which scripts the text is actually written in.
  const counts = new Map<string, number>();
  let foreign = 0;
  let letters = 0;

  for (const char of text) {
    const code = char.codePointAt(0)!;
    if (isLatinOrPunctuation(code)) continue;
    letters++;
    const block = blockOf(code);
    if (block) counts.set(block, (counts.get(block) ?? 0) + 1);
    else foreign++;
  }

  // Mostly Arabic/Cyrillic/Han: the language ID was badly wrong and there is no
  // correspondence to exploit. Emitting a stripped fragment would be worse than
  // admitting the failure.
  if (letters > 0 && foreign / letters > 0.4) {
    return { text: '', rejected: true };
  }

  // Pick the dominant Indic block actually present.
  let dominant: string | null = null;
  let best = 0;
  for (const [block, n] of counts) {
    if (n > best) {
      best = n;
      dominant = block;
    }
  }

  const out: string[] = [];
  for (const char of text) {
    const code = char.codePointAt(0)!;

    if (isLatinOrPunctuation(code)) {
      out.push(char);
      continue;
    }

    const block = blockOf(code);
    if (!block) continue; // drop the residual foreign characters

    if (block === target) {
      out.push(char);
      continue;
    }

    // Shift by the difference between block origins: the same offset within
    // each block denotes the same phoneme.
    const shifted = code - BLOCKS[block]! + targetStart;
    out.push(String.fromCodePoint(shifted));
  }

  const repaired = out.join('').replace(/\s{2,}/g, ' ').trim();

  return {
    text: repaired,
    rejected: repaired.length === 0 && text.trim().length > 0,
    ...(dominant && dominant !== target ? { transliteratedFrom: dominant } : {}),
  };
}
