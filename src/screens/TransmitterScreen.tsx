import { StatusBar } from 'expo-status-bar';
import React, { useCallback } from 'react';
import { Image, ScrollView, StyleSheet, Text, View } from 'react-native';
import Animated, { FadeIn } from 'react-native-reanimated';
import { SafeAreaView } from 'react-native-safe-area-context';

import { findLanguage } from '../config/languages';
import type { TransmitterController } from '../hooks/useTransmitterController';
import { ConnectionBadge } from '../ui/components/ConnectionBadge';
import { LanguageSelector } from '../ui/components/LanguageSelector';
import { ModelCard } from '../ui/components/ModelCard';
import { PacketLog } from '../ui/components/PacketLog';
import { PttButton } from '../ui/components/PttButton';
import { TelemetryStrip } from '../ui/components/TelemetryStrip';
import { WaveVisualizer } from '../ui/components/WaveVisualizer';
import { STATUS_META, theme } from '../ui/theme';

/**
 * The transmitter console.
 *
 * Layout follows the operator's attention during a transmission: identity and
 * link state at the top (checked once), the live visualizer and status in the
 * middle (watched while talking), the PTT control under the thumb, and the log
 * below the fold (reviewed afterwards).
 */
/**
 * Takes the whole controller as a prop rather than calling
 * useTransmitterController() itself. The controller must be owned by App —
 * a component that stays mounted regardless of which screen is currently
 * shown — because this screen unmounts when the operator switches to
 * Receive, and an unmounted component's hook state (the transmission log,
 * in particular) does not survive that.
 */
type Props = TransmitterController;

export function TransmitterScreen({
  transcriptionState,
  startPtt,
  stopPtt,
  isActive,
  language,
  setLanguage,
  pauseMs,
  setPauseMs,
  log,
  clearLog,
  senderId,
  connected,
  level,
  usingSyntheticAudio,
  transport,
  modelStatus,
  installModel,
  modelLabel,
  modelSizeMb,
}: Props) {

  const status = STATUS_META[transcriptionState.status] ?? STATUS_META.IDLE!;
  const busy = transcriptionState.status === 'TRANSCRIBING';

  const handlePressIn = useCallback(() => {
    void startPtt();
  }, [startPtt]);

  const handlePressOut = useCallback(() => {
    void stopPtt();
  }, [stopPtt]);

  return (
    <View style={styles.root}>
      <StatusBar style="light" />

      <SafeAreaView style={styles.safe} edges={['top', 'bottom']}>
        <ScrollView
          contentContainerStyle={styles.scroll}
          showsVerticalScrollIndicator={false}
        >
          {/* ── Identity + link ─────────────────────────────────── */}
          <View style={styles.header}>
            <View style={styles.brandRow}>
              <Image
                source={require('../../assets/logo.png')}
                style={styles.brandMark}
                resizeMode="contain"
                accessibilityIgnoresInvertColors
              />
              <View>
                <Text style={styles.brand}>iTantra</Text>
                <Text style={styles.brandSub}>TRANSMIT · OFFLINE</Text>
              </View>
            </View>
            <ConnectionBadge
              connected={connected}
              label={transport.name}
              compact
            />
          </View>

          <View style={styles.senderRow}>
            <Field label="DEVICE" value={senderId} />
          </View>

          {/* ── Live stage ──────────────────────────────────────── */}
          <Animated.View entering={FadeIn.duration(500)} style={styles.stage}>
            <View style={styles.statusRow}>
              <View
                style={[styles.statusDot, { backgroundColor: status.color }]}
              />
              <Text style={[styles.statusText, { color: status.color }]}>
                {status.label}
              </Text>
            </View>

            <WaveVisualizer
              level={level}
              isSpeaking={transcriptionState.isSpeaking}
              active={isActive}
            />

            <View style={styles.transcriptBox}>
              {transcriptionState.error ? (
                <Text style={styles.errorText}>
                  {transcriptionState.error}
                </Text>
              ) : transcriptionState.lastResult ? (
                <>
                  <Text style={styles.transcript}>
                    {transcriptionState.lastResult.text}
                  </Text>
                  <Text style={styles.transcriptMeta}>
                    {findLanguage(transcriptionState.lastResult.language).label}
                    {' · '}
                    {(transcriptionState.lastResult.durationMs / 1000).toFixed(1)}s
                  </Text>
                </>
              ) : (
                <Text style={styles.placeholder}>
                  Hold the mic and speak. Pause briefly or release to send.
                </Text>
              )}
            </View>
          </Animated.View>

          {/* ── Control ─────────────────────────────────────────── */}
          <View style={styles.ptt}>
            <PttButton
              active={isActive}
              isSpeaking={transcriptionState.isSpeaking}
              busy={busy}
              level={level}
              onPressIn={handlePressIn}
              onPressOut={handlePressOut}
            />
          </View>

          {usingSyntheticAudio && (
            <View style={styles.notice}>
              <Text style={styles.noticeTitle}>MICROPHONE UNAVAILABLE</Text>
              <Text style={styles.noticeText}>
                This platform has no microphone access. Build for Android to
                transmit real speech.
              </Text>
            </View>
          )}

          <ModelCard
            status={modelStatus}
            label={modelLabel}
            sizeMb={modelSizeMb}
            onInstall={() => void installModel()}
          />

          <TelemetryStrip pauseMs={pauseMs} onPauseChange={setPauseMs} />

          <LanguageSelector
            value={language}
            onChange={setLanguage}
            disabled={isActive}
          />

          <PacketLog entries={log} onClear={clearLog} />
        </ScrollView>
      </SafeAreaView>
    </View>
  );
}

/** One labelled readout in the identity row. Clips rather than wraps. */
function Field({ label, value }: { label: string; value: string }) {
  return (
    <View style={styles.field}>
      <Text style={styles.senderLabel}>{label}</Text>
      <Text style={styles.senderId} numberOfLines={1}>
        {value}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  // overflow hidden keeps the horizontal language rail from making the whole
  // screen pan sideways.
  root: { flex: 1, backgroundColor: theme.color.void, overflow: 'hidden' },
  safe: { flex: 1 },
  scroll: {
    paddingHorizontal: theme.sizing.screenPadding,
    paddingTop: 8,
    // Extra clearance so the floating Transmit/Receive switcher (position:
    // absolute in App.tsx) never overlaps the last card.
    paddingBottom: 96,
    gap: 18,
  },

  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: 10,
  },
  brandRow: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  brandMark: { width: 34, height: 34 },
  brand: {
    color: theme.color.text,
    fontSize: 19,
    fontWeight: '800',
    letterSpacing: -0.3,
  },
  brandSub: {
    color: theme.color.textFaint,
    fontSize: 8.5,
    letterSpacing: 1.5,
    fontWeight: '700',
    marginTop: 1,
  },

  senderRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 9,
    paddingHorizontal: 12,
    borderRadius: theme.radius.md,
    backgroundColor: theme.color.surface,
  },
  // flex + minWidth 0 is what actually lets the child Text ellipsize instead of
  // forcing the row wider than the screen.
  field: { flex: 1, minWidth: 0, gap: 3 },
  senderLabel: {
    fontSize: 8,
    color: theme.color.textFaint,
    fontWeight: '800',
    letterSpacing: 1.1,
  },
  senderId: {
    fontSize: 9.5,
    color: theme.color.textMuted,
  },

  stage: {
    alignItems: 'center',
    gap: 14,
    paddingVertical: 18,
    paddingHorizontal: 16,
    borderRadius: theme.radius.xl,
    backgroundColor: theme.color.surface,
  },
  statusRow: { flexDirection: 'row', alignItems: 'center', gap: 7 },
  statusDot: { width: 6, height: 6, borderRadius: 3 },
  statusText: { fontSize: 10, fontWeight: '800', letterSpacing: 1.8 },

  transcriptBox: {
    width: '100%',
    minHeight: 62,
    justifyContent: 'center',
    alignItems: 'center',
    gap: 6,
  },
  transcript: {
    color: theme.color.text,
    fontSize: 17,
    lineHeight: 25,
    textAlign: 'center',
    fontWeight: '500',
  },
  transcriptMeta: {
    color: theme.color.textFaint,
    fontSize: 10,
    letterSpacing: 0.4,
  },
  placeholder: {
    color: theme.color.textFaint,
    fontSize: 13,
    lineHeight: 19,
    textAlign: 'center',
    paddingHorizontal: 12,
  },
  errorText: {
    color: theme.color.danger,
    fontSize: 13,
    textAlign: 'center',
    lineHeight: 19,
  },

  ptt: { alignItems: 'center', paddingVertical: 8 },

  notice: {
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: `${theme.color.warn}44`,
    backgroundColor: `${theme.color.warn}12`,
    padding: 12,
    gap: 4,
  },
  noticeTitle: {
    color: theme.color.warn,
    fontSize: 9,
    fontWeight: '800',
    letterSpacing: 1.3,
  },
  noticeText: {
    color: theme.color.textMuted,
    fontSize: 11.5,
    lineHeight: 17,
  },
});
