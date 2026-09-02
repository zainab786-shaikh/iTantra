import { LinearGradient } from 'expo-linear-gradient';
import React, { memo, useEffect } from 'react';
import { StyleSheet, View } from 'react-native';
import Animated, {
  Easing,
  useAnimatedStyle,
  useSharedValue,
  withRepeat,
  withTiming,
} from 'react-native-reanimated';

import { theme } from '../theme';

/**
 * Slow-drifting colour field behind the whole screen.
 *
 * Two large blurred blobs on long, mutually prime periods, so the composition
 * never visibly loops. Kept at very low opacity: it should register as depth,
 * not as decoration competing with the readouts.
 */
function AuroraBackgroundImpl() {
  const driftA = useSharedValue(0);
  const driftB = useSharedValue(0);

  useEffect(() => {
    driftA.value = withRepeat(
      withTiming(1, { duration: 17_000, easing: Easing.inOut(Easing.ease) }),
      -1,
      true
    );
    driftB.value = withRepeat(
      withTiming(1, { duration: 23_000, easing: Easing.inOut(Easing.ease) }),
      -1,
      true
    );
  }, [driftA, driftB]);

  const blobA = useAnimatedStyle(() => ({
    transform: [
      { translateX: -60 + driftA.value * 120 },
      { translateY: -40 + driftA.value * 80 },
      { scale: 1 + driftA.value * 0.18 },
    ],
  }));

  const blobB = useAnimatedStyle(() => ({
    transform: [
      { translateX: 70 - driftB.value * 140 },
      { translateY: 60 - driftB.value * 90 },
      { scale: 1.1 - driftB.value * 0.2 },
    ],
  }));

  return (
    <View style={[StyleSheet.absoluteFill, styles.clip]} pointerEvents="none">
      <LinearGradient
        colors={[theme.color.void, theme.color.abyss, '#0B1020']}
        style={StyleSheet.absoluteFill}
      />
      <Animated.View style={[styles.blob, styles.blobA, blobA]} />
      <Animated.View style={[styles.blob, styles.blobB, blobB]} />
      {/* Vignette: darkens the edges so content in the middle sits forward. */}
      <LinearGradient
        colors={['rgba(5,6,11,0.0)', 'rgba(5,6,11,0.75)']}
        style={StyleSheet.absoluteFill}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  // The blobs deliberately overhang the frame; without this they extend the
  // scrollable area and the whole page can be dragged sideways.
  clip: { overflow: 'hidden' },
  blob: {
    position: 'absolute',
    width: 420,
    height: 420,
    borderRadius: 210,
    opacity: 0.24,
  },
  blobA: {
    top: -120,
    left: -110,
    backgroundColor: '#1E3A8A',
  },
  blobB: {
    bottom: -140,
    right: -120,
    backgroundColor: '#0E7490',
  },
});

export const AuroraBackground = memo(AuroraBackgroundImpl);
