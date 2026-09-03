import React, { memo } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { PAUSE_PRESETS } from '../../config/vadConfig';
import { theme } from '../theme';

interface Props {
  pauseMs: number;
  onPauseChange: (ms: number) => void;
}

/**
 * End-of-speech pause control.
 *
 * The pause length is the single tuning knob a user actually needs — too
 * short clips people mid-sentence, too long feels unresponsive — so it is
 * surfaced here rather than buried elsewhere. Decode latency and engine name
 * were dropped: backend diagnostics, not something a normal user acts on.
 */
function TelemetryStripImpl({ pauseMs, onPauseChange }: Props) {
  return (
    <View style={styles.wrap}>
      <View style={styles.pauseRow}>
        <Text style={styles.pauseLabel}>RESPONSE PAUSE</Text>
        <View style={styles.pauseOptions}>
          {PAUSE_PRESETS.map((preset) => {
            const selected = preset.ms === pauseMs;
            return (
              <Pressable
                key={preset.ms}
                onPress={() => onPauseChange(preset.ms)}
                accessibilityRole="radio"
                accessibilityState={{ selected }}
                accessibilityLabel={`Response pause: ${preset.label}, ${preset.ms} milliseconds`}
                style={[styles.pauseChip, selected && styles.pauseChipOn]}
              >
                <Text
                  style={[styles.pauseText, selected && styles.pauseTextOn]}
                >
                  {preset.label}
                </Text>
              </Pressable>
            );
          })}
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.lg,
    borderWidth: 1,
    borderColor: theme.color.hairline,
    backgroundColor: theme.color.surface,
    overflow: 'hidden',
  },
  pauseRow: {
    gap: 8,
    paddingHorizontal: 14,
    paddingVertical: 12,
  },
  pauseLabel: {
    fontSize: 11,
    color: theme.color.textMuted,
    fontWeight: '600',
  },
  pauseOptions: { flexDirection: 'row', gap: 8 },
  pauseChip: {
    flex: 1,
    minHeight: theme.sizing.touchTarget,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 11,
    paddingVertical: 8,
    borderRadius: theme.radius.sm,
    borderWidth: 1,
    borderColor: theme.color.hairline,
  },
  pauseChipOn: {
    borderColor: `${theme.color.accent}88`,
    backgroundColor: `${theme.color.accent}1F`,
  },
  pauseText: {
    fontSize: 11,
    color: theme.color.textMuted,
    fontWeight: '600',
  },
  pauseTextOn: { color: theme.color.accentStrong, fontWeight: '700' },
});

export const TelemetryStrip = memo(TelemetryStripImpl);
