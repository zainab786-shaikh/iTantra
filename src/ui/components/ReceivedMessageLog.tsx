import React, { memo } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import Animated, { FadeInDown, LinearTransition } from 'react-native-reanimated';

import { findLanguage } from '../../config/languages';
import { PRIORITY_COLORS } from '../../core/packet/priority';
import type { ReceivedMessage } from '../../core/receiver/types';
import { theme } from '../theme';

interface Props {
  messages: readonly ReceivedMessage[];
  onClear: () => void;
  onReplay: (packetId: string) => void;
}

const STATE_LABEL: Record<ReceivedMessage['state'], string> = {
  received: 'RECEIVED',
  queued: 'QUEUED',
  speaking: 'SPEAKING',
  spoken: 'SPOKEN',
  error: 'ERROR',
};

/**
 * Rolling feed of received messages, newest first — the receiver's mirror
 * of the transmitter's PacketLog, same visual language so the two screens
 * read as one app.
 */
function ReceivedMessageLogImpl({ messages, onClear, onReplay }: Props) {
  return (
    <View style={styles.wrap}>
      <View style={styles.headerRow}>
        <Text style={styles.header}>RECEIVED</Text>
        {messages.length > 0 ? (
          <Pressable
            onPress={onClear}
            hitSlop={16}
            accessibilityRole="button"
            accessibilityLabel="Clear received messages"
          >
            <Text style={styles.count}>{messages.length} · CLEAR</Text>
          </Pressable>
        ) : (
          <Text style={styles.count}>EMPTY</Text>
        )}
      </View>

      {messages.length === 0 ? (
        <View style={styles.empty}>
          <Text style={styles.emptyText}>
            No messages yet — received transmissions appear here.
          </Text>
        </View>
      ) : (
        messages.map((m, index) => {
          const critical = m.packet.priority === 'CRITICAL';
          const speaking = m.state === 'speaking';
          return (
            <Animated.View
              key={m.packet.id}
              entering={FadeInDown.delay(Math.min(index, 3) * 40).springify()}
              layout={LinearTransition.springify()}
              style={[
                styles.row,
                critical && styles.rowCritical,
                speaking && styles.rowSpeaking,
              ]}
            >
              <View
                style={[
                  styles.priorityBar,
                  { backgroundColor: PRIORITY_COLORS[m.packet.priority] },
                ]}
              />
              <View style={styles.rowBody}>
                <View style={styles.rowMeta}>
                  <Text
                    style={[
                      styles.priority,
                      { color: PRIORITY_COLORS[m.packet.priority] },
                    ]}
                  >
                    {m.packet.priority}
                  </Text>
                  <Text
                    style={[
                      styles.lang,
                      { color: findLanguage(m.packet.language).accent },
                    ]}
                  >
                    {findLanguage(m.packet.language).short}
                  </Text>
                  <View style={styles.spacer} />
                  <Text
                    style={[
                      styles.state,
                      {
                        color: speaking
                          ? theme.color.live
                          : m.state === 'error'
                            ? theme.color.danger
                            : theme.color.textFaint,
                      },
                    ]}
                  >
                    {speaking ? '🔊 ' : ''}
                    {STATE_LABEL[m.state]}
                  </Text>
                </View>

                <Text style={styles.text}>{m.packet.text}</Text>

                {m.state === 'error' && m.error && (
                  <Text style={styles.errorText}>{m.error}</Text>
                )}

                <View style={styles.footer}>
                  <Text style={styles.id}>
                    {new Date(m.packet.timestamp).toLocaleTimeString()} ·{' '}
                    {m.packet.senderId}
                  </Text>
                  <Pressable
                    onPress={() => onReplay(m.packet.id)}
                    accessibilityRole="button"
                    accessibilityLabel="Replay message"
                    hitSlop={16}
                  >
                    <Text style={styles.replay}>REPLAY</Text>
                  </Pressable>
                </View>
              </View>
            </Animated.View>
          );
        })
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: { gap: 8 },
  headerRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 2,
    marginBottom: 2,
  },
  header: {
    color: theme.color.textFaint,
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 1.8,
  },
  count: {
    color: theme.color.textFaint,
    fontSize: 10,
    fontWeight: '700',
    letterSpacing: 1,
  },
  empty: {
    borderWidth: 1,
    borderColor: theme.color.hairline,
    borderStyle: 'dashed',
    borderRadius: theme.radius.md,
    paddingVertical: 26,
    alignItems: 'center',
  },
  emptyText: { color: theme.color.textFaint, fontSize: 12 },
  row: {
    flexDirection: 'row',
    backgroundColor: theme.color.surface,
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: 'transparent',
    overflow: 'hidden',
  },
  rowCritical: {
    borderColor: `${theme.color.danger}66`,
    backgroundColor: `${theme.color.danger}14`,
  },
  rowSpeaking: {
    borderColor: `${theme.color.live}66`,
  },
  priorityBar: { width: 3 },
  rowBody: { flex: 1, padding: 12, gap: 6 },
  rowMeta: { flexDirection: 'row', alignItems: 'center', gap: 7 },
  priority: { fontSize: 9, fontWeight: '900', letterSpacing: 1 },
  lang: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  spacer: { flex: 1 },
  state: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  text: { color: theme.color.text, fontSize: 14, lineHeight: 20 },
  errorText: { color: theme.color.danger, fontSize: 11.5, lineHeight: 16 },
  footer: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  id: {
    fontSize: 10,
    color: theme.color.textFaint,
  },
  replay: {
    fontSize: 10,
    fontWeight: '800',
    letterSpacing: 0.8,
    color: theme.color.primary,
  },
});

export const ReceivedMessageLog = memo(ReceivedMessageLogImpl);
