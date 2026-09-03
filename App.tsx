import React, { useRef, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider, useSafeAreaInsets } from 'react-native-safe-area-context';

import { MockTransport } from './src/core/transport/MockTransport';
import { useReceiverController } from './src/hooks/useReceiverController';
import { useTransmitterController } from './src/hooks/useTransmitterController';
import { ReceiverScreen } from './src/screens/ReceiverScreen';
import { TransmitterScreen } from './src/screens/TransmitterScreen';
import { theme } from './src/ui/theme';

type Mode = 'transmit' | 'receive';

export default function App() {
  // One transport instance shared by both screens: the transmitter sends
  // through it, the receiver listens on it. MockTransport loops a sent
  // packet back to its own receive listeners (no real P2P transport exists
  // yet — that's a separate workstream), so this is what makes the receiver
  // pipeline exercisable end-to-end on a single device today.
  const transportRef = useRef<MockTransport | null>(null);
  transportRef.current ??= new MockTransport();
  const transport = transportRef.current;

  const [mode, setMode] = useState<Mode>('transmit');

  // Both controllers are owned here, not inside their screens, and both
  // hooks run unconditionally on every render regardless of `mode`. That is
  // required, not a style choice: TransmitterScreen and ReceiverScreen are
  // each mounted only while their mode is active, so a hook living inside
  // either one loses all its state (and, for the receiver, its
  // transport.onPacketReceived subscription) the moment the operator
  // switches away — which was exactly why sent packets were going nowhere
  // and the transmission log was resetting.
  const transmitter = useTransmitterController({ transport });
  const receiver = useReceiverController(transport);

  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <SafeAreaProvider>
        {mode === 'transmit' ? (
          <TransmitterScreen {...transmitter} />
        ) : (
          <ReceiverScreen transport={transport} {...receiver} />
        )}
        <ModeSwitcher mode={mode} onChange={setMode} />
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}

/**
 * Minimal mode toggle. No navigation library is added for two screens —
 * that would be over-engineering for this app's shape.
 */
function ModeSwitcher({
  mode,
  onChange,
}: {
  mode: Mode;
  onChange: (mode: Mode) => void;
}) {
  const insets = useSafeAreaInsets();
  return (
    <View style={[styles.switcher, { bottom: insets.bottom + 12 }]}>
      <SwitchButton
        label="Transmit"
        active={mode === 'transmit'}
        onPress={() => onChange('transmit')}
      />
      <SwitchButton
        label="Receive"
        active={mode === 'receive'}
        onPress={() => onChange('receive')}
      />
    </View>
  );
}

function SwitchButton({
  label,
  active,
  onPress,
}: {
  label: string;
  active: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      onPress={onPress}
      accessibilityRole="button"
      accessibilityState={{ selected: active }}
      style={[styles.button, active && styles.buttonActive]}
    >
      <Text style={[styles.buttonText, active && styles.buttonTextActive]}>
        {label}
      </Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  switcher: {
    position: 'absolute',
    alignSelf: 'center',
    flexDirection: 'row',
    gap: 4,
    padding: 4,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.color.surface,
  },
  button: {
    minHeight: theme.sizing.touchTarget,
    justifyContent: 'center',
    paddingVertical: 8,
    paddingHorizontal: 22,
    borderRadius: theme.radius.pill,
  },
  buttonActive: {
    backgroundColor: `${theme.color.primary}26`,
  },
  buttonText: {
    color: theme.color.textMuted,
    fontSize: 13,
    fontWeight: '700',
  },
  buttonTextActive: {
    color: theme.color.primary,
  },
});
