import type { PacketPriority } from '../types';

/**
 * Keyword triggers that raise a packet's priority band, per language.
 *
 * This is a deliberately transparent, auditable rule set rather than a model:
 * an operator needs to be able to predict exactly why a message was escalated,
 * and a misfire on a CRITICAL band is expensive.
 */
const TRIGGERS: Record<PacketPriority, readonly string[]> = {
  CRITICAL: [
    'mayday', 'emergency', 'sos', 'critical', 'casualty', 'fire', 'attack',
    // Hindi
    'आपातकाल', 'खतरा', 'आग', 'हमला',
    // Tamil
    'அவசரம்', 'ஆபத்து', 'தீ', 'தாக்குதல்',
    // Marathi
    'आणीबाणी', 'धोका', 'आग', 'हल्ला',
    // Bengali. The two-word form is listed above the bare "জরুরি" in HIGH, and
    // CRITICAL is tested first, so "emergency" outranks plain "urgent".
    'জরুরি অবস্থা', 'আগুন', 'বিপদ', 'হামলা',
    // Telugu
    'అత్యవసర', 'మంటలు', 'ప్రమాదం', 'దాడి',
    // Kannada
    'ಬೆಂಕಿ', 'ಅಪಾಯ', 'ದಾಳಿ',
    // Gujarati
    'કટોકટી', 'આગ', 'જોખમ', 'હુમલો',
    // Malayalam
    'അപകടം', 'തീ', 'ആക്രമണം', 'അടിയന്തിരം',
    // Odia
    'ନିଆଁ', 'ବିପଦ', 'ଆକ୍ରମଣ',
  ],
  HIGH: [
    'urgent', 'immediate', 'backup', 'medic', 'injured', 'help', 'breach',
    // Hindi
    'तुरंत', 'सहायता', 'घायल', 'मदद',
    // Tamil
    'உடனடி', 'உதவி', 'காயம்',
    // Marathi
    'त्वरित', 'मदत', 'जखमी',
    // Bengali
    'সাহায্য', 'আহত', 'জরুরি', 'তাড়াতাড়ি',
    // Telugu
    'సహాయం', 'గాయ', 'తక్షణ',
    // Kannada
    'ಸಹಾಯ', 'ಗಾಯ', 'ತಕ್ಷಣ', 'ತುರ್ತು',
    // Gujarati
    'મદદ', 'ઘાયલ', 'તાત્કાલિક',
    // Malayalam
    'സഹായം', 'ഉടനടി', 'പരിക്ക്',
    // Odia
    'ସାହାଯ୍ୟ', 'ତୁରନ୍ତ', 'ଆହତ',
  ],
  MEDIUM: [
    'request', 'report', 'status', 'confirm', 'move', 'position',
    // Hindi
    'रिपोर्ट', 'स्थिति', 'पुष्टि',
    // Tamil
    'அறிக்கை', 'நிலை',
    // Marathi
    'अहवाल', 'स्थिती',
    // Bengali
    'রিপোর্ট', 'অবস্থা', 'অবস্থান',
    // Telugu
    'నివేదిక', 'స్థితి',
    // Kannada
    'ವರದಿ', 'ಸ್ಥಿತಿ',
    // Gujarati
    'અહેવાલ', 'સ્થિતિ',
    // Malayalam
    'റിപ്പോർട്ട്', 'സ്ഥിതി',
    // Odia
    'ରିପୋର୍ଟ', 'ସ୍ଥିତି',
  ],
  NORMAL: [],
};

/** Bands in descending order; the first match wins. */
const ORDER: readonly PacketPriority[] = ['CRITICAL', 'HIGH', 'MEDIUM'];

/**
 * Classify an utterance into a priority band by keyword match.
 * Defaults to NORMAL when nothing matches.
 */
export function classifyPriority(text: string): PacketPriority {
  const haystack = text.toLowerCase();
  for (const band of ORDER) {
    if (TRIGGERS[band].some((word) => haystack.includes(word))) return band;
  }
  return 'NORMAL';
}

/**
 * Presentation colour per band, matching iTantra Design.md's restrained
 * palette (kept as literal hex here rather than importing ui/theme, since
 * core/ intentionally has no dependency on the UI layer) — textMuted /
 * info / warning / critical, an escalating ladder rather than four
 * unrelated saturated hues.
 */
export const PRIORITY_COLORS: Record<PacketPriority, string> = {
  NORMAL: '#B5B5B5',
  MEDIUM: '#72A9D8',
  HIGH: '#E5B85C',
  CRITICAL: '#E66B67',
};
