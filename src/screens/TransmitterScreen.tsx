import { StatusBar } from 'expo-status-bar';
import React, { useCallback } from 'react';
import { ScrollView, StyleSheet, Text, View } from 'react-native';
import Animated, { FadeIn } from 'react-native-reanimated';
import { SafeAreaView } from 'react-native-safe-area-context';

import { findLanguage } from '../config/languages';
import { useTransmitterController } from '../hooks/useTransmitterController';
import { AuroraBackground } from '../ui/components/AuroraBackground';
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
export function TransmitterScreen() {
  const {
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
  } = useTransmitterController();

  const status = STATUS_META[transcriptionState.status] ?? STATUS_META.IDLE!;
  const activeLanguage = findLanguage(language);
  const busy = transcriptionState.status === 'TRANSCRIBING';

  const handlePressIn = useCallback(() => {
    void startPtt();
  }, [startPtt]);

  const handlePressOut = useCallback(() => {
    void stopPtt();
  }, [stopPtt]);

  return (
    <View style={styles.root}>
      <AuroraBackground />
      <StatusBar style="light" />

      <SafeAreaView style={styles.safe} edges={['top', 'bottom']}>
        <ScrollView
          contentContainerStyle={styles.scroll}
          showsVerticalScrollIndicator={false}
        >
          {/* ── Identity + link ─────────────────────────────────── */}
          <View style={styles.header}>
            <View style={styles.brandRow}>
              <View style={styles.brandMark}>
                <View style={styles.brandCore} />
              </View>
              <View>
                <Text style={styles.brand}>iTantra</Text>
                <Text style={styles.brandSub}>TRANSMITTER · OFFLINE STT</Text>
              </View>
            </View>
            <ConnectionBadge
              connected={connected}
              label={transport.name}
              compact
            />
          </View>

          {/* Fixed three-up readout. Each cell clips rather than wraps, so the
              row keeps its height no matter how long an identifier gets. */}
          <View style={styles.senderRow}>
            <Field label="SENDER" value={senderId} />
            <View style={styles.senderDivider} />
            <Field label="PIPELINE" value="16k · MONO · PCM16" />
            <View style={styles.senderDivider} />
            <Field label="TRANSPORT" value={transport.name.replace(/^\w+:\/\//, '')} />
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
                    {(transcriptionState.lastResult.durationMs / 1000).toFixed(1)}s audio
                    {transcriptionState.lastResult.forced ? ' · manual flush' : ' · auto flush'}
                  </Text>
                </>
              ) : (
                <Text style={styles.placeholder}>
                  Hold the mic. Speech is segmented on a{' '}
                  {pauseMs} ms pause and decoded on-device.
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
              <Text style={styles.noticeTitle}>SYNTHETIC AUDIO SOURCE</Text>
              <Text style={styles.noticeText}>
                No microphone stream on this platform, so the VAD and segmenter
                are running on a generated speech-shaped signal. Build for
                Android to capture real audio.
              </Text>
            </View>
          )}

          <ModelCard
            status={modelStatus}
            label={modelLabel}
            sizeMb={modelSizeMb}
            onInstall={() => void installModel()}
          />

          <TelemetryStrip
            latencyMs={transcriptionState.latencyMs}
            utteranceMs={transcriptionState.utteranceMs}
            engine={transcriptionState.engine}
            pauseMs={pauseMs}
            onPauseChange={setPauseMs}
          />

          <LanguageSelector
            value={language}
            onChange={setLanguage}
            disabled={isActive}
          />

          <PacketLog entries={log} onClear={clearLog} />

          <Text style={styles.footer}>
            {activeLanguage.label} decoder · packets are UUID v4 tagged and
            handed to the transport interface
          </Text>
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
  // overflow hidden keeps the horizontal language rail and the aurora from
  // making the whole console pan sideways.
  root: { flex: 1, backgroundColor: theme.color.void, overflow: 'hidden' },
  safe: { flex: 1 },
  scroll: {
    paddingHorizontal: 18,
    paddingTop: 8,
    paddingBottom: 40,
    gap: 18,
  },

  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: 10,
  },
  brandRow: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  brandMark: {
    width: 34,
    height: 34,
    borderRadius: 11,
    borderWidth: 1,
    borderColor: `${theme.color.primary}55`,
    backgroundColor: `${theme.color.primary}14`,
    alignItems: 'center',
    justifyContent: 'center',
  },
  brandCore: {
    width: 11,
    height: 11,
    borderRadius: 6,
    backgroundColor: theme.color.primary,
  },
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
    borderWidth: 1,
    borderColor: theme.color.hairline,
    backgroundColor: 'rgba(17, 22, 35, 0.5)',
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
    fontFamily: theme.font.mono,
  },
  senderDivider: {
    width: 1,
    height: 20,
    backgroundColor: theme.color.hairline,
    marginHorizontal: 9,
  },

  stage: {
    alignItems: 'center',
    gap: 14,
    paddingVertical: 18,
    paddingHorizontal: 16,
    borderRadius: theme.radius.xl,
    borderWidth: 1,
    borderColor: theme.color.hairline,
    backgroundColor: 'rgba(10, 13, 22, 0.55)',
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

  footer: {
    color: theme.color.textFaint,
    fontSize: 10,
    textAlign: 'center',
    lineHeight: 15,
    paddingHorizontal: 20,
    paddingTop: 4,
  },
});
