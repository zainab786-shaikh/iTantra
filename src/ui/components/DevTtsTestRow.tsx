import React, { memo } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import { LANGUAGES } from '../../config/languages';
import { theme } from '../theme';

/** One short, real-language sample per app language, for functional TTS testing. */
export const TTS_SAMPLE_TEXT: Record<string, string> = {
  'en-IN': 'Message received. Position is secure.',
  'hi-IN': 'संदेश प्राप्त हुआ। स्थिति सुरक्षित है।',
  'mr-IN': 'संदेश मिळाला. स्थिती सुरक्षित आहे.',
  'ta-IN': 'செய்தி பெறப்பட்டது. நிலை பாதுகாப்பானது.',
  'bn-IN': 'বার্তা পাওয়া গেছে। অবস্থান নিরাপদ।',
  'te-IN': 'సందేశం అందింది. స్థానం సురక్షితం.',
  'kn-IN': 'ಸಂದೇಶ ಸ್ವೀಕರಿಸಲಾಗಿದೆ. ಸ್ಥಳ ಸುರಕ್ಷಿತವಾಗಿದೆ.',
  'gu-IN': 'સંદેશ મળ્યો. સ્થિતિ સુરક્ષિત છે.',
  'ml-IN': 'സന്ദേശം ലഭിച്ചു. സ്ഥാനം സുരക്ഷിതമാണ്.',
  'or-IN': 'ବାର୍ତ୍ତା ମିଳିଲା। ଅବସ୍ଥାନ ସୁରକ୍ଷିତ।',
};

interface Props {
  onInject: (languageCode: string, priority: 'NORMAL' | 'CRITICAL') => void;
}

/**
 * Dev-only functional test harness: injects a sample packet per language
 * straight into the transport's receive side, the same way a real peer's
 * packet would arrive. Exists because there is no real P2P transport yet
 * (separate workstream) and asking a human to speak all 9 Indic languages
 * for every test pass is not practical — this is the receiver-side
 * equivalent of SherpaSttBackend's existing dev-only self-test clip.
 *
 * Never rendered outside __DEV__.
 */
function DevTtsTestRowImpl({ onInject }: Props) {
  return (
    <View style={styles.wrap}>
      <Text style={styles.header}>DEV · INJECT TEST MESSAGE</Text>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.rail}>
        {LANGUAGES.map((lang) => (
          <Pressable
            key={lang.code}
            onPress={() => onInject(lang.code, 'NORMAL')}
            onLongPress={() => onInject(lang.code, 'CRITICAL')}
            style={styles.chip}
          >
            <Text style={[styles.chipText, { color: lang.accent }]}>{lang.short}</Text>
          </Pressable>
        ))}
      </ScrollView>
      <Text style={styles.hint}>Tap = NORMAL · hold = CRITICAL</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderStyle: 'dashed',
    borderColor: theme.color.hairlineStrong,
    padding: 10,
    gap: 6,
  },
  header: {
    color: theme.color.textFaint,
    fontSize: 9,
    fontWeight: '800',
    letterSpacing: 1.2,
  },
  rail: { gap: 6 },
  chip: {
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderRadius: theme.radius.sm,
    borderWidth: 1,
    borderColor: theme.color.hairline,
    backgroundColor: 'rgba(22, 28, 44, 0.55)',
  },
  chipText: { fontSize: 10, fontWeight: '800' },
  hint: { color: theme.color.textFaint, fontSize: 9 },
});

export const DevTtsTestRow = memo(DevTtsTestRowImpl);
