import React, { memo } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from 'react-native';
import Animated, { useAnimatedStyle, useDerivedValue, withTiming } from 'react-native-reanimated';

import type { ModelStatus } from '../../core/stt/ModelManager';
import { theme } from '../theme';

interface Props {
  status: ModelStatus;
  label: string;
  sizeMb: number;
  onInstall: () => void;
}

/**
 * Install state of the speech model, and the button that fetches it.
 *
 * This is the most consequential control on the screen: until the model is
 * installed the app cannot transcribe at all, and shows an explicit placeholder
 * instead. The card is therefore blunt about which of those two worlds the user
 * is currently in.
 */
function ModelCardImpl({ status, label, sizeMb, onInstall }: Props) {
  const downloading = status.state === 'downloading';
  const percent = downloading ? status.percent : 0;

  const bar = useDerivedValue(() => withTiming(percent, { duration: 220 }));
  const barStyle = useAnimatedStyle(() => ({ width: `${bar.value}%` }));

  const tint =
    status.state === 'installed'
      ? theme.color.live
      : status.state === 'error'
        ? theme.color.danger
        : status.state === 'unsupported'
          ? theme.color.textFaint
          : theme.color.warn;

  return (
    <View style={[styles.wrap, { borderColor: `${tint}44` }]}>
      <View style={styles.headerRow}>
        <View style={[styles.dot, { backgroundColor: tint }]} />
        <Text style={[styles.title, { color: tint }]}>
          {status.state === 'installed'
            ? 'SPEECH MODEL READY'
            : status.state === 'downloading'
              ? `${status.phase === 'extracting' ? 'EXTRACTING' : 'DOWNLOADING'} ${status.percent}%`
              : status.state === 'unsupported'
                ? 'SPEECH MODEL UNAVAILABLE'
                : status.state === 'error'
                  ? 'MODEL ERROR'
                  : 'NO SPEECH MODEL'}
        </Text>
        {downloading && <ActivityIndicator size="small" color={tint} />}
      </View>

      <Text style={styles.body}>
        {status.state === 'installed'
          ? `${label} is installed. Transcription runs fully offline on this device.`
          : status.state === 'downloading'
            ? `Fetching ${label}. Keep the app open — this only happens once.`
            : status.state === 'unsupported'
              ? status.reason
              : status.state === 'error'
                ? status.message
                : `${label} (~${sizeMb} MB) is not installed, so nothing can be transcribed yet. Downloading it needs internet once; after that the app works offline.`}
      </Text>

      {downloading && (
        <View style={styles.track}>
          <Animated.View style={[styles.fill, barStyle]} />
        </View>
      )}

      {(status.state === 'not-installed' || status.state === 'error') && (
        <Pressable
          onPress={onInstall}
          accessibilityRole="button"
          accessibilityLabel={`Download ${label}`}
          style={styles.button}
        >
          <Text style={styles.buttonText}>
            {status.state === 'error' ? 'RETRY DOWNLOAD' : `DOWNLOAD · ${sizeMb} MB`}
          </Text>
        </Pressable>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.lg,
    borderWidth: 1,
    backgroundColor: 'rgba(17, 22, 35, 0.6)',
    padding: 13,
    gap: 9,
  },
  headerRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  dot: { width: 7, height: 7, borderRadius: 4 },
  title: { fontSize: 10, fontWeight: '800', letterSpacing: 1.4, flex: 1 },
  body: { color: theme.color.textMuted, fontSize: 11.5, lineHeight: 17 },
  track: {
    height: 4,
    borderRadius: 2,
    backgroundColor: theme.color.hairline,
    overflow: 'hidden',
  },
  fill: { height: 4, borderRadius: 2, backgroundColor: theme.color.primary },
  button: {
    marginTop: 2,
    paddingVertical: 10,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: `${theme.color.primary}66`,
    backgroundColor: `${theme.color.primary}1A`,
    alignItems: 'center',
  },
  buttonText: {
    color: theme.color.primary,
    fontSize: 11,
    fontWeight: '800',
    letterSpacing: 1.2,
  },
});

export const ModelCard = memo(ModelCardImpl);
