import * as Haptics from 'expo-haptics';
import { LinearGradient } from 'expo-linear-gradient';
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
 * utterance immediately instead of waiting out the VAD pause.
 *
 * Two halo rings expand continuously while active; their scale is coupled to
 * the live level, so the control visibly reacts to the voice rather than just
 * animating on a timer.
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

  // Two rings offset by half a cycle so the pulse never fully disappears.
  // Written out rather than generated in a loop: useAnimatedStyle is a hook and
  // must be called unconditionally at the top level.
  const haloA = useAnimatedStyle(() => {
    const t = ring.value % 1;
    return {
      opacity: (1 - t) * 0.5 * activeMix.value,
      transform: [{ scale: 1 + t * (0.85 + smoothLevel.value * 0.35) }],
    };
  });

  const haloB = useAnimatedStyle(() => {
    const t = (ring.value + 0.5) % 1;
    return {
      opacity: (1 - t) * 0.5 * activeMix.value,
      transform: [{ scale: 1 + t * (0.85 + smoothLevel.value * 0.35) }],
    };
  });

  const label = busy ? 'DECODING' : active ? 'RELEASE TO SEND' : 'HOLD TO TALK';
  const tint = isSpeaking ? theme.color.live : theme.color.primary;

  return (
    <View style={styles.wrap}>
      <Animated.View
        style={[styles.halo, { borderColor: tint }, haloA]}
        pointerEvents="none"
      />
      <Animated.View
        style={[styles.halo, { borderColor: tint }, haloB]}
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
        <Animated.View style={[styles.core, core]}>
          <LinearGradient
            colors={
              active
                ? [tint, theme.color.primaryDim, '#0B1220']
                : [theme.color.accent, '#4F46E5', '#131A2B']
            }
            start={{ x: 0.1, y: 0 }}
            end={{ x: 0.9, y: 1 }}
            style={styles.gradient}
          >
            <View style={styles.inner}>
              <MicGlyph color={active ? tint : theme.color.text} />
            </View>
          </LinearGradient>
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
    shadowColor: '#000',
    shadowOpacity: 0.55,
    shadowRadius: 24,
    shadowOffset: { width: 0, height: 12 },
    elevation: 14,
  },
  gradient: {
    flex: 1,
    borderRadius: SIZE / 2,
    padding: 2,
  },
  inner: {
    flex: 1,
    borderRadius: SIZE / 2,
    backgroundColor: 'rgba(5, 6, 11, 0.72)',
    alignItems: 'center',
    justifyContent: 'center',
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
