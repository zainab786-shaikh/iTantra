import React, { memo, useEffect } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Animated, {
  Easing,
  FadeIn,
  FadeOut,
  useAnimatedStyle,
  useSharedValue,
  withRepeat,
  withTiming,
} from 'react-native-reanimated';

import { theme } from '../theme';

interface Props {
  text: string | null;
  loading: boolean;
}

/**
 * Full-width, impossible-to-miss banner for a CRITICAL message. Only
 * rendered while one is active — it is meant to interrupt attention, not to
 * sit quietly in the layout.
 */
function CriticalAlertBannerImpl({ text, loading }: Props) {
  const pulse = useSharedValue(0);

  useEffect(() => {
    pulse.value = withRepeat(
      withTiming(1, { duration: 650, easing: Easing.inOut(Easing.ease) }),
      -1,
      true
    );
  }, [pulse]);

  const pulseStyle = useAnimatedStyle(() => ({
    opacity: 0.7 + pulse.value * 0.3,
  }));

  return (
    <Animated.View
      entering={FadeIn.duration(200)}
      exiting={FadeOut.duration(200)}
      style={styles.wrap}
    >
      <Animated.Text style={[styles.title, pulseStyle]}>
        ⚠ CRITICAL ALERT
      </Animated.Text>
      <Text style={styles.body} numberOfLines={3}>
        {loading ? 'Preparing alert audio…' : text}
      </Text>
      <Text style={styles.sub}>
        {loading ? '' : '🔊 PLAYING · cannot be interrupted'}
      </Text>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.lg,
    borderWidth: 1.5,
    borderColor: theme.color.danger,
    backgroundColor: `${theme.color.danger}1F`,
    padding: 16,
    gap: 6,
    alignItems: 'center',
  },
  title: {
    color: theme.color.danger,
    fontSize: 13,
    fontWeight: '900',
    letterSpacing: 2,
  },
  body: {
    color: theme.color.text,
    fontSize: 16,
    fontWeight: '600',
    textAlign: 'center',
    lineHeight: 22,
  },
  sub: {
    color: theme.color.danger,
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 1,
  },
});

export const CriticalAlertBanner = memo(CriticalAlertBannerImpl);
