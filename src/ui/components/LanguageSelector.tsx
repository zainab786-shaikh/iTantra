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
 * Each chip carries the language's own script, because a user scanning for
 * "मराठी" should not have to read a romanized label first. The selected chip
 * uses Signal Yellow, the design system's one selection colour — not each
 * language's own accent hue, which would turn the rail into a rainbow.
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
    borderColor: selected ? `${theme.color.accent}88` : theme.color.hairline,
    backgroundColor: selected
      ? `${theme.color.accent}1F`
      : theme.color.surface,
    transform: [{ scale: 0.97 + mix.value * 0.03 }],
    opacity: withTiming(disabled ? 0.45 : 1, { duration: 180 }),
  }));

  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      accessibilityRole="radio"
      accessibilityState={{ selected, disabled }}
      accessibilityLabel={`${lang.label}`}
    >
      <Animated.View style={[styles.chip, containerStyle]}>
        {selected && <View style={styles.checkDot} />}
        <View>
          <Text
            style={[styles.chipNative, selected && { color: theme.color.text }]}
          >
            {lang.native}
          </Text>
          <Text
            style={[styles.chipCode, selected && { color: theme.color.accentStrong }]}
          >
            {lang.short}
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
  checkDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: theme.color.accent,
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
