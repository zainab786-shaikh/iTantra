import * as Haptics from 'expo-haptics';
import React, { memo, useEffect } from 'react';
import { Platform, Pressable, StyleSheet, Text, View } from 'react-native';
import Animated, {
  Easing,
  useAnimatedStyle,
  useDerivedValue,
  useSharedValue,
  withRepeat,
  withSpring,
  withTiming,
  type SharedValue,
} from 'react-native-reanimated';

import { theme } from '../theme';

const SIZE = 132;

interface Props {
  active: boolean;
  isSpeaking: boolean;
  busy: boolean;
  level: SharedValue<number>;
  onPressIn: () => void;
  onPressOut: () => void;
}

/**
 * Push-to-talk control.
 *
 * Press-and-hold rather than tap-to-toggle: it matches radio muscle memory and
 * makes it impossible to leave the mic open by accident. Release finalizes the
 * utterance immediately.
 *
 * One ring pulses while active, coupled to the live level so it visibly
 * reacts to the voice rather than just animating on a timer. Deliberately
 * flat — a solid fill and a single restrained ring, no gradient core or
 * multi-layer glow.
 */
function PttButtonImpl({
  active,
  isSpeaking,
  busy,
  level,
  onPressIn,
  onPressOut,
}: Props) {
  const press = useSharedValue(0);
  const activeMix = useSharedValue(0);
  const ring = useSharedValue(0);

  useEffect(() => {
    activeMix.value = withTiming(active ? 1 : 0, { duration: 280 });
  }, [active, activeMix]);

  useEffect(() => {
    ring.value = withRepeat(
      withTiming(1, { duration: 2400, easing: Easing.out(Easing.ease) }),
      -1,
      false
    );
  }, [ring]);

  const smoothLevel = useDerivedValue(() =>
    withSpring(level.value, { damping: 18, stiffness: 150 })
  );

  const core = useAnimatedStyle(() => ({
    transform: [
      {
        scale:
          1 -
          press.value * 0.06 +
          smoothLevel.value * 0.07 * activeMix.value,
      },
    ],
  }));

  // One ring, pulsing outward while active. Its scale tracks the live level
  // so it visibly reacts to voice rather than animating on a fixed timer.
  const halo = useAnimatedStyle(() => {
    const t = ring.value % 1;
    return {
      opacity: (1 - t) * 0.45 * activeMix.value,
      transform: [{ scale: 1 + t * (0.7 + smoothLevel.value * 0.3) }],
    };
  });

  const label = busy ? 'PROCESSING' : active ? 'RELEASE TO SEND' : 'HOLD TO TALK';
  // Green while holding/listening, yellow the moment speech is actually
  // detected — the two design accents carry the two-stage meaning instead of
  // a single color standing in for both.
  const tint = isSpeaking ? theme.color.accent : theme.color.primary;

  return (
    <View style={styles.wrap}>
      <Animated.View
        style={[styles.halo, { borderColor: tint }, halo]}
        pointerEvents="none"
      />

      <Pressable
        onPressIn={() => {
          press.value = withSpring(1, { damping: 18, stiffness: 320 });
          if (Platform.OS !== 'web') {
            void Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Medium);
          }
          onPressIn();
        }}
        onPressOut={() => {
          press.value = withSpring(0, { damping: 18, stiffness: 320 });
          if (Platform.OS !== 'web') {
            void Haptics.impactAsync(Haptics.ImpactFeedbackStyle.Light);
          }
          onPressOut();
        }}
        accessibilityRole="button"
        accessibilityLabel="Push to talk"
        accessibilityHint="Hold to capture speech, release to transcribe and send"
        accessibilityState={{ busy, selected: active }}
      >
        <Animated.View
          style={[
            styles.core,
            active
              ? { backgroundColor: tint }
              : { backgroundColor: theme.color.surfaceRaised, borderWidth: 1.5, borderColor: theme.color.primary },
            core,
          ]}
        >
          <MicGlyph color={active ? theme.color.void : theme.color.primary} />
        </Animated.View>
      </Pressable>

      <Text style={[styles.label, active && { color: tint }]}>{label}</Text>
    </View>
  );
}

/** Mic pictogram drawn with views, so the app carries no icon dependency. */
function MicGlyph({ color }: { color: string }) {
  return (
    <View style={styles.glyph}>
      <View style={[styles.capsule, { backgroundColor: color }]} />
      <View style={[styles.arc, { borderColor: color }]} />
      <View style={[styles.stem, { backgroundColor: color }]} />
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { alignItems: 'center', justifyContent: 'center' },
  halo: {
    position: 'absolute',
    top: 0,
    width: SIZE,
    height: SIZE,
    borderRadius: SIZE / 2,
    borderWidth: 1.5,
  },
  core: {
    width: SIZE,
    height: SIZE,
    borderRadius: SIZE / 2,
    alignItems: 'center',
    justifyContent: 'center',
    shadowColor: '#000',
    shadowOpacity: 0.2,
    shadowRadius: 8,
    shadowOffset: { width: 0, height: 4 },
    elevation: 4,
  },
  glyph: { alignItems: 'center', justifyContent: 'center', height: 52 },
  capsule: { width: 17, height: 28, borderRadius: 9 },
  arc: {
    width: 33,
    height: 17,
    borderBottomLeftRadius: 17,
    borderBottomRightRadius: 17,
    borderWidth: 2.5,
    borderTopWidth: 0,
    marginTop: 3,
  },
  stem: { width: 2.5, height: 7, marginTop: 2, borderRadius: 2 },
  label: {
    marginTop: 18,
    color: theme.color.textMuted,
    fontSize: 11,
    letterSpacing: 2.4,
    fontWeight: '700',
  },
});

export const PttButton = memo(PttButtonImpl);
