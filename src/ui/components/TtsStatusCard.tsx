import React, { memo, useEffect, useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from 'react-native';

import { findLanguage } from '../../config/languages';
import type { TtsPlaybackState } from '../../core/tts/types';
import { theme } from '../theme';

interface Props {
  state: TtsPlaybackState;
  /** True when the current error looks like a missing voice pack, so we can offer to install it. */
  onInstallVoice: (languageCode: string) => void;
  installing: boolean;
  installPercent: number;
}

/**
 * Receiver's speech status — the mirror of the transmitter's ModelCard, but
 * describing playback rather than decode. Deliberately free of engine
 * vocabulary (ONNX/VITS/Piper/MMS): the operator sees "Ready" / "Speaking"
 * / "Voice pack needed", never a model name.
 */
function TtsStatusCardImpl({
  state,
  onInstallVoice,
  installing,
  installPercent,
}: Props) {
  const [dots, setDots] = useState('');
  useEffect(() => {
    if (state.phase !== 'loading-voice' && state.phase !== 'speaking') {
      setDots('');
      return;
    }
    const id = setInterval(() => {
      setDots((d) => (d.length >= 3 ? '' : `${d}.`));
    }, 400);
    return () => clearInterval(id);
  }, [state.phase]);

  const lang = state.language ? findLanguage(state.language) : null;
  const missingVoice =
    state.phase === 'error' &&
    state.error?.toLowerCase().includes('install the required language pack');

  const tint =
    state.isCritical
      ? theme.color.danger
      : state.phase === 'speaking'
        ? theme.color.live
        : state.phase === 'error'
          ? theme.color.danger
          : state.phase === 'loading-voice'
            ? theme.color.info
            : theme.color.textFaint;

  const title = state.isCritical
    ? 'CRITICAL ALERT'
    : state.phase === 'speaking'
      ? 'SPEAKING'
      : state.phase === 'loading-voice'
        ? 'PREPARING VOICE'
        : state.phase === 'error'
          ? 'SPEECH UNAVAILABLE'
          : 'READY';

  return (
    <View style={[styles.wrap, { borderColor: `${tint}44` }]}>
      <View style={styles.headerRow}>
        <View style={[styles.dot, { backgroundColor: tint }]} />
        <Text style={[styles.title, { color: tint }]}>{title}</Text>
        {(state.phase === 'speaking' || state.phase === 'loading-voice') && (
          <ActivityIndicator size="small" color={tint} />
        )}
      </View>

      {state.phase === 'idle' && (
        <Text style={styles.body}>
          Listening for incoming transmissions. Speech will play automatically.
        </Text>
      )}

      {(state.phase === 'loading-voice' || state.phase === 'speaking') && (
        <>
          <Text style={styles.body} numberOfLines={2}>
            {state.text}
          </Text>
          <Text style={styles.meta}>
            {lang?.label ?? state.language}
            {state.phase === 'loading-voice' ? ` · preparing voice${dots}` : ''}
          </Text>
        </>
      )}

      {state.phase === 'error' && (
        <>
          <Text style={styles.errorBody}>{state.error}</Text>
          {missingVoice && state.language && (
            <Pressable
              onPress={() => onInstallVoice(state.language!)}
              disabled={installing}
              accessibilityRole="button"
              accessibilityLabel={`Install ${lang?.label ?? ''} voice`}
              style={styles.button}
            >
              <Text style={styles.buttonText}>
                {installing
                  ? `INSTALLING · ${installPercent}%`
                  : `INSTALL ${lang?.label.toUpperCase() ?? 'VOICE'} PACK`}
              </Text>
            </Pressable>
          )}
        </>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.lg,
    borderWidth: 1,
    backgroundColor: theme.color.surface,
    padding: 13,
    gap: 8,
  },
  headerRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  dot: { width: 7, height: 7, borderRadius: 4 },
  title: { fontSize: 10, fontWeight: '800', letterSpacing: 1.4, flex: 1 },
  body: { color: theme.color.text, fontSize: 14, lineHeight: 20 },
  meta: { color: theme.color.textFaint, fontSize: 10.5 },
  errorBody: { color: theme.color.textMuted, fontSize: 12, lineHeight: 17 },
  button: {
    marginTop: 2,
    minHeight: theme.sizing.touchTarget,
    justifyContent: 'center',
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

export const TtsStatusCard = memo(TtsStatusCardImpl);
