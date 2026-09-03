import { StatusBar } from 'expo-status-bar';
import React, { useCallback, useState } from 'react';
import { Image, ScrollView, StyleSheet, Text, View } from 'react-native';
import Animated, { FadeIn } from 'react-native-reanimated';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { Transport } from '../core/transport/Transport';
import type { ReceiverController } from '../hooks/useReceiverController';
import { ConnectionBadge } from '../ui/components/ConnectionBadge';
import { CriticalAlertBanner } from '../ui/components/CriticalAlertBanner';
import { ReceivedMessageLog } from '../ui/components/ReceivedMessageLog';
import { TtsStatusCard } from '../ui/components/TtsStatusCard';
import { theme } from '../ui/theme';

interface Props extends ReceiverController {
  transport: Transport;
}

/**
 * The receiver console: mirrors the transmitter's layout (identity/link at
 * top, live state in the middle, history below) so the two screens read as
 * one app, not two unrelated tools bolted together.
 *
 * Takes the whole controller as a prop rather than calling
 * useReceiverController() itself — the controller (and the transport
 * subscription it holds) must be owned by App, a component that stays
 * mounted regardless of which screen is currently shown. This screen
 * unmounts when the operator switches to Transmit; an unmounted
 * component's effect subscriptions do not survive that, which is exactly
 * why packets sent while this screen was unmounted were going nowhere.
 */
export function ReceiverScreen({
  transport,
  messages,
  ttsState,
  connected,
  clearHistory,
  replay,
  installVoice,
}: Props) {
  const [installing, setInstalling] = useState(false);
  const [installPercent, setInstallPercent] = useState(0);

  const handleInstallVoice = useCallback(
    (languageCode: string) => {
      setInstalling(true);
      setInstallPercent(0);
      installVoice(languageCode, (percent) => setInstallPercent(percent))
        .catch(() => {
          // TtsStatusCard already reflects the failure via ttsState.error on
          // the next spoken attempt; nothing further to do here.
        })
        .finally(() => setInstalling(false));
    },
    [installVoice]
  );

  const criticalActive =
    ttsState.isCritical &&
    (ttsState.phase === 'speaking' || ttsState.phase === 'loading-voice');

  return (
    <View style={styles.root}>
      <StatusBar style="light" />

      <SafeAreaView style={styles.safe} edges={['top', 'bottom']}>
        <ScrollView
          contentContainerStyle={styles.scroll}
          showsVerticalScrollIndicator={false}
        >
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
                <Text style={styles.brandSub}>RECEIVE · OFFLINE</Text>
              </View>
            </View>
            <ConnectionBadge connected={connected} label={transport.name} compact />
          </View>

          {criticalActive && (
            <Animated.View entering={FadeIn.duration(150)}>
              <CriticalAlertBanner
                text={ttsState.text}
                loading={ttsState.phase === 'loading-voice'}
              />
            </Animated.View>
          )}

          <TtsStatusCard
            state={ttsState}
            onInstallVoice={handleInstallVoice}
            installing={installing}
            installPercent={installPercent}
          />

          <ReceivedMessageLog
            messages={messages}
            onClear={clearHistory}
            onReplay={replay}
          />

          <Text style={styles.footer}>
            Messages play in the language they were sent — no translation.
          </Text>
        </ScrollView>
      </SafeAreaView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: theme.color.void },
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
  footer: {
    color: theme.color.textFaint,
    fontSize: 10,
    textAlign: 'center',
    lineHeight: 15,
    paddingHorizontal: 20,
    paddingTop: 4,
  },
});
