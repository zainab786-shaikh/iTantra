import React, { memo, useEffect } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Animated, {
  Easing,
  useAnimatedStyle,
  useSharedValue,
  withRepeat,
  withTiming,
} from 'react-native-reanimated';

import { theme } from '../theme';

interface Props {
  connected: boolean;
  /** Transport identifier, e.g. "mock://itantra-loopback". */
  label: string;
  /**
   * Drop the transport identifier and keep only the state.
   * The header has no room for a full URI on a 375 pt screen.
   */
  compact?: boolean;
}

/**
 * Link-state pill with a breathing dot.
 *
 * The dot animates only while connected — a static dot on a dropped link is a
 * clearer signal than a pulsing one, and stops the animation from implying
 * activity that is not happening.
 */
function ConnectionBadgeImpl({ connected, label, compact }: Props) {
  const pulse = useSharedValue(0);

  useEffect(() => {
    if (!connected) {
      pulse.value = withTiming(0, { duration: 200 });
      return;
    }
    pulse.value = withRepeat(
      withTiming(1, { duration: 1500, easing: Easing.inOut(Easing.ease) }),
      -1,
      true
    );
  }, [connected, pulse]);

  const dotStyle = useAnimatedStyle(() => ({
    opacity: 0.45 + pulse.value * 0.55,
    transform: [{ scale: 0.85 + pulse.value * 0.35 }],
  }));

  const color = connected ? theme.color.live : theme.color.danger;

  return (
    <View style={[styles.pill, { borderColor: `${color}44` }]}>
      <View style={styles.dotWrap}>
        <Animated.View
          style={[styles.glow, { backgroundColor: color }, dotStyle]}
        />
        <View style={[styles.dot, { backgroundColor: color }]} />
      </View>
      <Text style={[styles.text, { color }]}>
        {connected ? 'LINK ACTIVE' : 'LINK DOWN'}
      </Text>
      {!compact && (
        <>
          <View style={styles.divider} />
          <Text style={styles.sub} numberOfLines={1}>
            {label}
          </Text>
        </>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  pill: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingVertical: 7,
    paddingHorizontal: 12,
    borderRadius: theme.radius.pill,
    borderWidth: 1,
    backgroundColor: 'rgba(17, 22, 35, 0.7)',
  },
  dotWrap: { width: 10, height: 10, alignItems: 'center', justifyContent: 'center' },
  glow: { position: 'absolute', width: 16, height: 16, borderRadius: 8, opacity: 0.4 },
  dot: { width: 7, height: 7, borderRadius: 4 },
  text: { fontSize: 10, fontWeight: '800', letterSpacing: 1.3 },
  divider: {
    width: 1,
    height: 11,
    backgroundColor: theme.color.hairlineStrong,
  },
  sub: {
    fontSize: 10,
    color: theme.color.textFaint,
    maxWidth: 130,
    fontFamily: theme.font.mono,
  },
});

export const ConnectionBadge = memo(ConnectionBadgeImpl);
