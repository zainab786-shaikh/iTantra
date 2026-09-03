import React, { memo, useEffect, useMemo } from 'react';
import { StyleSheet, View } from 'react-native';
import Animated, {
  Easing,
  interpolateColor,
  useAnimatedStyle,
  useDerivedValue,
  useSharedValue,
  withRepeat,
  withSpring,
  withTiming,
  type SharedValue,
} from 'react-native-reanimated';

import { theme } from '../theme';

const BAR_COUNT = 27;
const BAR_WIDTH = 4;
const MAX_HEIGHT = 64;
const MIN_HEIGHT = 4;

interface Props {
  /** 0..1 input level, written from the audio callback. */
  level: SharedValue<number>;
  /** Drives the colour shift from cyan (listening) to green (speech). */
  isSpeaking: boolean;
  active: boolean;
}

/**
 * Symmetric bar spectrum driven entirely on the UI thread.
 *
 * The level arrives as a shared value rather than a prop, so audio frames never
 * trigger a React render — the bars are re-laid-out by the compositor while the
 * JS thread stays free for VAD and decoding.
 *
 * Each bar mixes three signals: a fixed bell envelope so the middle is tallest,
 * the live level, and a per-bar travelling wave that keeps the display alive
 * during steady tones (a pure level meter looks frozen when someone holds a
 * vowel).
 */
function WaveVisualizerImpl({ level, isSpeaking, active }: Props) {
  const clock = useSharedValue(0);
  const speakingMix = useSharedValue(0);
  const activeMix = useSharedValue(0);

  useEffect(() => {
    clock.value = withRepeat(
      withTiming(Math.PI * 2, { duration: 2200, easing: Easing.linear }),
      -1,
      false
    );
  }, [clock]);

  useEffect(() => {
    speakingMix.value = withTiming(isSpeaking ? 1 : 0, { duration: 260 });
  }, [isSpeaking, speakingMix]);

  useEffect(() => {
    activeMix.value = withTiming(active ? 1 : 0, { duration: 320 });
  }, [active, activeMix]);

  // Spring-smooth the raw level: RMS per 32 ms frame is jumpy, and unsmoothed
  // it reads as flicker rather than as a voice.
  const smoothed = useDerivedValue(() =>
    withSpring(level.value, { damping: 16, stiffness: 170, mass: 0.5 })
  );

  const bars = useMemo(
    () => Array.from({ length: BAR_COUNT }, (_, i) => i),
    []
  );

  return (
    <View style={styles.row} pointerEvents="none">
      {bars.map((i) => (
        <Bar
          key={i}
          index={i}
          clock={clock}
          smoothed={smoothed}
          speakingMix={speakingMix}
          activeMix={activeMix}
        />
      ))}
    </View>
  );
}

interface BarProps {
  index: number;
  clock: SharedValue<number>;
  smoothed: SharedValue<number>;
  speakingMix: SharedValue<number>;
  activeMix: SharedValue<number>;
}

const Bar = memo(function Bar({
  index,
  clock,
  smoothed,
  speakingMix,
  activeMix,
}: BarProps) {
  // Distance from centre, 0 at the middle bar and 1 at the outermost.
  const centre = (BAR_COUNT - 1) / 2;
  const distance = Math.abs(index - centre) / centre;
  // Bell envelope: the middle carries the most energy, like a real spectrum.
  const envelope = Math.pow(Math.cos((distance * Math.PI) / 2), 1.6);
  const phase = index * 0.45;

  const style = useAnimatedStyle(() => {
    // Travelling wave so neighbouring bars are never perfectly in step.
    const wobble = 0.62 + 0.38 * Math.sin(clock.value * 2 + phase);
    const idleBreath = 0.06 + 0.05 * Math.sin(clock.value + phase * 0.6);

    const driven = smoothed.value * envelope * wobble * activeMix.value;
    // The idle shimmer is only damped when inactive, never cut to zero: a row
    // of dead dots reads as a broken control rather than a standby one. Kept
    // subtle — this is a status indicator, not a flashy audio visualizer.
    const resting = idleBreath * envelope * (0.25 + 0.35 * activeMix.value);
    const amplitude = Math.max(driven, resting);

    const height = MIN_HEIGHT + amplitude * (MAX_HEIGHT - MIN_HEIGHT);

    const color = interpolateColor(
      speakingMix.value,
      [0, 1],
      [theme.color.primary, theme.color.accent]
    );

    return {
      height,
      backgroundColor: color,
      opacity: 0.3 + 0.6 * Math.min(1, amplitude * 2.2 + 0.25),
    };
  });

  return <Animated.View style={[styles.bar, style]} />;
});

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    height: MAX_HEIGHT,
    gap: 4,
  },
  bar: {
    width: BAR_WIDTH,
    borderRadius: BAR_WIDTH / 2,
    backgroundColor: theme.color.primary,
  },
});

export const WaveVisualizer = memo(WaveVisualizerImpl);
