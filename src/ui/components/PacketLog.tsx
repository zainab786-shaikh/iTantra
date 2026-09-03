import React, { memo } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import Animated, { FadeInDown, LinearTransition } from 'react-native-reanimated';

import { findLanguage } from '../../config/languages';
import { PRIORITY_COLORS } from '../../core/packet/priority';
import type { LogEntry } from '../../hooks/useTransmitterController';
import { theme } from '../theme';

interface Props {
  entries: readonly LogEntry[];
  onClear: () => void;
}

/**
 * Rolling feed of sent messages, newest first.
 *
 * Kept to what a user would actually want to review: what was said, in what
 * language, at what priority, whether it went through, and when. Decode
 * latency and the raw packet UUID are implementation detail and don't
 * appear here.
 */
function PacketLogImpl({ entries, onClear }: Props) {
  return (
    <View style={styles.wrap}>
      <View style={styles.headerRow}>
        <Text style={styles.header}>SENT</Text>
        {entries.length > 0 ? (
          <Pressable
            onPress={onClear}
            hitSlop={16}
            accessibilityRole="button"
            accessibilityLabel="Clear sent messages"
          >
            <Text style={styles.count}>{entries.length} · CLEAR</Text>
          </Pressable>
        ) : (
          <Text style={styles.count}>EMPTY</Text>
        )}
      </View>

      {entries.length === 0 ? (
        <View style={styles.empty}>
          <Text style={styles.emptyText}>
            No messages yet — hold the mic to transmit.
          </Text>
        </View>
      ) : (
        entries.map((entry, index) => (
          <Animated.View
            key={entry.packet.id}
            entering={FadeInDown.delay(Math.min(index, 3) * 40).springify()}
            layout={LinearTransition.springify()}
            style={styles.row}
          >
            <View
              style={[
                styles.priorityBar,
                { backgroundColor: PRIORITY_COLORS[entry.packet.priority] },
              ]}
            />
            <View style={styles.rowBody}>
              <View style={styles.rowMeta}>
                <Text
                  style={[
                    styles.priority,
                    { color: PRIORITY_COLORS[entry.packet.priority] },
                  ]}
                >
                  {entry.packet.priority}
                </Text>
                <Text
                  style={[
                    styles.lang,
                    { color: findLanguage(entry.packet.language).accent },
                  ]}
                >
                  {findLanguage(entry.packet.language).short}
                </Text>
                <View style={styles.spacer} />
                <Text
                  style={[
                    styles.delivery,
                    {
                      color: entry.delivered
                        ? theme.color.live
                        : theme.color.danger,
                    },
                  ]}
                >
                  {entry.delivered ? 'SENT' : 'FAILED'}
                </Text>
              </View>

              <Text style={styles.text}>{entry.packet.text}</Text>

              <Text style={styles.id}>
                {new Date(entry.packet.timestamp).toLocaleTimeString()}
              </Text>
            </View>
          </Animated.View>
        ))
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
    overflow: 'hidden',
  },
  priorityBar: { width: 3 },
  rowBody: { flex: 1, padding: 12, gap: 6 },
  rowMeta: { flexDirection: 'row', alignItems: 'center', gap: 7 },
  priority: { fontSize: 9, fontWeight: '900', letterSpacing: 1 },
  lang: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  spacer: { flex: 1 },
  delivery: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  text: { color: theme.color.text, fontSize: 14, lineHeight: 20 },
  id: {
    fontSize: 10,
    color: theme.color.textFaint,
  },
});

export const PacketLog = memo(PacketLogImpl);
