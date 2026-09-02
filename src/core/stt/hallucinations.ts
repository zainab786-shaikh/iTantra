/**
 * Filters Whisper's non-speech output.
 *
 * Whisper was trained on subtitle data, so when it is handed audio containing no
 * speech it does not return an empty string — it returns whatever the subtitles
 * of such a clip would have said: "[Music]", "[BLANK_AUDIO]", "Thanks for
 * watching!", and so on. These are confident, well-formed outputs, which is
 * exactly what makes them dangerous: without this filter they become packets and
 * get transmitted as if someone had spoken them.
 *
 * The VAD already rejects most silence; this catches what survives it, such as
 * background music, a cough, or room noise loud enough to look like speech.
 */

/**
 * Literal phrases Whisper emits for non-speech audio. Compared
 * case-insensitively after stripping punctuation and whitespace.
 */
const ARTIFACT_PHRASES: readonly string[] = [
  'thank you',
  'thanks for watching',
  'thank you for watching',
  'thanks for watching!',
  'bye',
  'bye bye',
  'you',
  'so',
  'okay',
  'oh',
  'hmm',
  'mm',
  'uh',
  'the',
  'subtitles by the amara org community',
  'subs by www zeoranger co uk',
  'transcription by castingwords',
];

/**
 * Text made up entirely of a bracketed or parenthesised tag — "[Music]",
 * "(applause)", "♪♪♪" — is a subtitle annotation, never speech.
 */
const FULLY_TAGGED = /^[\s]*[[(【♪*][^\])】]*[\])】♪*][\s.!?]*$/u;

/** Strip punctuation and collapse whitespace, for phrase comparison. */
function normalize(text: string): string {
  return text
    .toLowerCase()
    .replace(/[.,!?¡¿;:"'`´“”‘’\-_/\\]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

/**
 * True when `text` is a subtitle artifact rather than a transcription.
 *
 * Deliberately conservative about short real words: a lone "so" or "you" is
 * discarded, which can drop a genuine one-word reply, but transmitting a
 * hallucinated packet is the worse failure for an operational tool.
 */
export function isNonSpeechArtifact(text: string): boolean {
  const trimmed = text.trim();
  if (trimmed.length === 0) return true;

  if (FULLY_TAGGED.test(trimmed)) return true;

  const normalized = normalize(trimmed);
  if (normalized.length === 0) return true;
  if (ARTIFACT_PHRASES.includes(normalized)) return true;

  // A single repeated token ("you you you you") is a decoder loop, not speech.
  const words = normalized.split(' ');
  if (words.length >= 4 && new Set(words).size === 1) return true;

  return false;
}
