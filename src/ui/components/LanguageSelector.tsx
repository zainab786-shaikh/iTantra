import React, { memo } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import Animated, {
  useAnimatedStyle,
  useDerivedValue,
  withSpring,
  withTiming,
} from 'react-native-reanimated';

import { LANGUAGES, type TransmitterLanguage } from '../../config/languages';
import { theme } from '../theme';

interface Props {
  value: string;
  onChange: (code: string) => void;
  disabled?: boolean;
}

/**
 * Horizontal language rail.
 *
 * Each chip carries the language's own script, because an operator scanning for
 * "मराठी" should not have to read a romanized label first. The active chip
 * takes the language's accent colour, which is the same colour used for its
 * packets in the log, so the two views stay visually linked.
 */
function LanguageSelectorImpl({ value, onChange, disabled }: Props) {
  return (
    <View>
      <View style={styles.headerRow}>
        <Text style={styles.header}>DECODE LANGUAGE</Text>
        <Text style={styles.headerHint}>{LANGUAGES.length} available</Text>
      </View>
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={styles.rail}
      >
        {LANGUAGES.map((lang) => (
          <Chip
            key={lang.code}
            lang={lang}
            selected={lang.code === value}
            disabled={disabled}
            onPress={() => onChange(lang.code)}
          />
        ))}
      </ScrollView>
    </View>
  );
}

interface ChipProps {
  lang: TransmitterLanguage;
  selected: boolean;
  disabled?: boolean;
  onPress: () => void;
}

const Chip = memo(function Chip({ lang, selected, disabled, onPress }: ChipProps) {
  // Animate on a numeric mix rather than swapping styles, so selection glides.
  const mix = useDerivedValue(() =>
    withSpring(selected ? 1 : 0, { damping: 18, stiffness: 200 })
  );

  const containerStyle = useAnimatedStyle(() => ({
    borderColor: selected ? `${lang.accent}88` : theme.color.hairline,
    backgroundColor: selected
      ? `${lang.accent}1F`
      : 'rgba(22, 28, 44, 0.55)',
    transform: [{ scale: 0.97 + mix.value * 0.03 }],
    opacity: withTiming(disabled ? 0.45 : 1, { duration: 180 }),
  }));

  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      accessibilityRole="radio"
      accessibilityState={{ selected, disabled }}
      accessibilityLabel={`${lang.label} decoder`}
    >
      <Animated.View style={[styles.chip, containerStyle]}>
        <View
          style={[
            styles.chipDot,
            {
              backgroundColor: selected ? lang.accent : 'transparent',
              borderColor: lang.accent,
            },
          ]}
        />
        <View>
          <Text
            style={[styles.chipNative, selected && { color: theme.color.text }]}
          >
            {lang.native}
          </Text>
          <Text
            style={[styles.chipCode, selected && { color: lang.accent }]}
          >
            {lang.short} · {lang.code}
          </Text>
        </View>
      </Animated.View>
    </Pressable>
  );
});

const styles = StyleSheet.create({
  headerRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: 10,
    paddingHorizontal: 2,
  },
  header: {
    color: theme.color.textFaint,
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 1.8,
  },
  headerHint: { color: theme.color.textFaint, fontSize: 10 },
  rail: { gap: 8, paddingRight: 8 },
  chip: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 9,
    paddingVertical: 9,
    paddingHorizontal: 13,
    borderRadius: theme.radius.md,
    borderWidth: 1,
  },
  chipDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    borderWidth: 1.5,
  },
  chipNative: {
    color: theme.color.textMuted,
    fontSize: 14,
    fontWeight: '700',
  },
  chipCode: {
    color: theme.color.textFaint,
    fontSize: 9,
    letterSpacing: 0.8,
    marginTop: 2,
    fontFamily: theme.font.mono,
  },
});

export const LanguageSelector = memo(LanguageSelectorImpl);
