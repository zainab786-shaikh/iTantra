import React, { memo } from 'react';
import { StyleSheet, Text, View } from 'react-native';
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
 * Rolling feed of transmitted packets, newest first.
 *
 * Each row is the packet as it went on the wire — priority band, language,
 * decode latency, truncated UUID — because when something goes wrong in the
 * field the operator needs the identifier, not a prettified summary.
 */
function PacketLogImpl({ entries, onClear }: Props) {
  return (
    <View style={styles.wrap}>
      <View style={styles.headerRow}>
        <Text style={styles.header}>TRANSMISSION LOG</Text>
        <Text style={styles.count} onPress={onClear} suppressHighlighting>
          {entries.length > 0 ? `${entries.length} · CLEAR` : 'EMPTY'}
        </Text>
      </View>

      {entries.length === 0 ? (
        <View style={styles.empty}>
          <Text style={styles.emptyText}>
            No packets yet — hold the mic to transmit.
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
                {entry.simulated && (
                  <Text style={styles.simulated}>SIMULATED</Text>
                )}
                <View style={styles.spacer} />
                <Text style={styles.latency}>{entry.latencyMs} ms</Text>
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
                  {entry.delivered ? 'SENT' : 'FAIL'}
                </Text>
              </View>

              <Text style={styles.text}>{entry.packet.text}</Text>

              <Text style={styles.id}>
                {new Date(entry.packet.timestamp).toLocaleTimeString()} ·{' '}
                {entry.packet.id.slice(0, 8)}
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
    backgroundColor: 'rgba(17, 22, 35, 0.72)',
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderColor: theme.color.hairline,
    overflow: 'hidden',
  },
  priorityBar: { width: 3 },
  rowBody: { flex: 1, padding: 11, gap: 6 },
  rowMeta: { flexDirection: 'row', alignItems: 'center', gap: 7 },
  priority: { fontSize: 9, fontWeight: '900', letterSpacing: 1 },
  lang: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  simulated: {
    fontSize: 8,
    fontWeight: '800',
    letterSpacing: 0.8,
    color: theme.color.warn,
    borderWidth: 1,
    borderColor: `${theme.color.warn}55`,
    borderRadius: 4,
    paddingHorizontal: 4,
    paddingVertical: 1,
  },
  spacer: { flex: 1 },
  latency: {
    fontSize: 9,
    color: theme.color.textFaint,
    fontFamily: theme.font.mono,
  },
  delivery: { fontSize: 9, fontWeight: '800', letterSpacing: 0.6 },
  text: { color: theme.color.text, fontSize: 14, lineHeight: 20 },
  id: {
    fontSize: 9,
    color: theme.color.textFaint,
    fontFamily: theme.font.mono,
  },
});

export const PacketLog = memo(PacketLogImpl);
