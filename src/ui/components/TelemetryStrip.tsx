import React, { memo } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { PAUSE_PRESETS } from '../../config/vadConfig';
import type { SttEngineKind } from '../../core/types';
import { theme } from '../theme';

interface Props {
  latencyMs: number | null;
  utteranceMs: number | null;
  engine: SttEngineKind;
  pauseMs: number;
  onPauseChange: (ms: number) => void;
}

const ENGINE_LABEL: Record<SttEngineKind, string> = {
  'sherpa-onnx': 'SHERPA-ONNX',
  simulated: 'SIMULATED',
  none: 'IDLE',
};

/**
 * Compact readouts plus the end-of-speech pause control.
 *
 * The pause length is the single tuning knob an operator actually needs in the
 * field — too short clips people mid-sentence, too long feels unresponsive — so
 * it is surfaced here rather than buried in a settings screen.
 */
function TelemetryStripImpl({
  latencyMs,
  utteranceMs,
  engine,
  pauseMs,
  onPauseChange,
}: Props) {
  return (
    <View style={styles.wrap}>
      <View style={styles.metrics}>
        <Metric
          label="DECODE"
          value={latencyMs != null ? `${latencyMs}` : '—'}
          unit={latencyMs != null ? 'ms' : ''}
          tint={theme.color.primary}
        />
        <View style={styles.vline} />
        <Metric
          label="UTTERANCE"
          value={utteranceMs != null ? (utteranceMs / 1000).toFixed(1) : '—'}
          unit={utteranceMs != null ? 's' : ''}
          tint={theme.color.live}
        />
        <View style={styles.vline} />
        <Metric
          label="ENGINE"
          value={ENGINE_LABEL[engine]}
          unit=""
          tint={
            engine === 'sherpa-onnx' ? theme.color.live : theme.color.warn
          }
          small
        />
      </View>

      <View style={styles.pauseRow}>
        <Text style={styles.pauseLabel}>END-OF-SPEECH PAUSE</Text>
        <View style={styles.pauseOptions}>
          {PAUSE_PRESETS.map((preset) => {
            const selected = preset.ms === pauseMs;
            return (
              <Pressable
                key={preset.ms}
                onPress={() => onPauseChange(preset.ms)}
                accessibilityRole="radio"
                accessibilityState={{ selected }}
                style={[styles.pauseChip, selected && styles.pauseChipOn]}
              >
                <Text
                  style={[styles.pauseText, selected && styles.pauseTextOn]}
                >
                  {preset.ms}ms
                </Text>
              </Pressable>
            );
          })}
        </View>
      </View>
    </View>
  );
}

function Metric({
  label,
  value,
  unit,
  tint,
  small,
}: {
  label: string;
  value: string;
  unit: string;
  tint: string;
  small?: boolean;
}) {
  return (
    <View style={styles.metric}>
      <Text style={styles.metricLabel}>{label}</Text>
      <View style={styles.metricValueRow}>
        <Text
          style={[
            styles.metricValue,
            { color: tint },
            small && styles.metricValueSmall,
          ]}
          numberOfLines={1}
        >
          {value}
        </Text>
        {unit.length > 0 && <Text style={styles.metricUnit}>{unit}</Text>}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.lg,
    borderWidth: 1,
    borderColor: theme.color.hairline,
    backgroundColor: 'rgba(17, 22, 35, 0.6)',
    overflow: 'hidden',
  },
  metrics: { flexDirection: 'row', alignItems: 'center', paddingVertical: 12 },
  metric: { flex: 1, alignItems: 'center', gap: 4 },
  metricLabel: {
    fontSize: 8.5,
    color: theme.color.textFaint,
    fontWeight: '800',
    letterSpacing: 1.4,
  },
  metricValueRow: { flexDirection: 'row', alignItems: 'baseline', gap: 2 },
  metricValue: {
    fontSize: 19,
    fontWeight: '800',
    fontFamily: theme.font.mono,
  },
  metricValueSmall: { fontSize: 11, letterSpacing: 0.6 },
  metricUnit: { fontSize: 9, color: theme.color.textFaint, fontWeight: '700' },
  vline: {
    width: 1,
    height: 26,
    backgroundColor: theme.color.hairline,
  },
  pauseRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 12,
    paddingVertical: 9,
    borderTopWidth: 1,
    borderTopColor: theme.color.hairline,
    backgroundColor: 'rgba(10, 13, 22, 0.5)',
  },
  pauseLabel: {
    fontSize: 8.5,
    color: theme.color.textFaint,
    fontWeight: '800',
    letterSpacing: 1.3,
  },
  pauseOptions: { flexDirection: 'row', gap: 5 },
  pauseChip: {
    paddingHorizontal: 9,
    paddingVertical: 4,
    borderRadius: theme.radius.sm,
    borderWidth: 1,
    borderColor: theme.color.hairline,
  },
  pauseChipOn: {
    borderColor: `${theme.color.primary}77`,
    backgroundColor: `${theme.color.primary}1A`,
  },
  pauseText: {
    fontSize: 10,
    color: theme.color.textFaint,
    fontWeight: '700',
    fontFamily: theme.font.mono,
  },
  pauseTextOn: { color: theme.color.primary },
});

export const TelemetryStrip = memo(TelemetryStripImpl);
