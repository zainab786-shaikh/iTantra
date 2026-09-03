import React, { memo, useRef, useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, View } from 'react-native';

import { formatIsolationTable, runSttIsolationReport } from '../../core/diagnostics/SttIsolationReport';
import { SttEngineProvider } from '../../core/stt/SttEngineProvider';
import { theme } from '../theme';
import { TTS_SAMPLE_TEXT } from './DevTtsTestRow';

/**
 * Dev-only STT isolation diagnostic: for every language, runs model-mapping +
 * direct-to-STT (no VAD/segmenter) + the VAD+segmenter round-trip against a
 * dedicated, isolated SttEngineProvider (never the live transmitter's), then
 * logs the full JSON and the final comparison table to console.
 *
 * Companion to DevSttRoundTripRow: that one only exercises the full pipeline.
 * This one adds the bypass test needed to tell "VAD/segmentation dropped it"
 * apart from "the model itself couldn't transcribe it" — see
 * src/core/diagnostics/SttIsolationReport.ts for the classification logic.
 *
 * Never rendered outside __DEV__. Installs nothing, deletes nothing, and does
 * not touch the live transmitter's STT engine.
 */
function DevSttIsolationRowImpl() {
  const providerRef = useRef<SttEngineProvider | null>(null);
  providerRef.current ??= new SttEngineProvider();

  const [running, setRunning] = useState(false);
  const [summary, setSummary] = useState<string | null>(null);

  const runAll = async () => {
    setRunning(true);
    setSummary(null);
    try {
      const report = await runSttIsolationReport(providerRef.current!, TTS_SAMPLE_TEXT);
      console.log('[SttDiag][isolation] full report', JSON.stringify(report, null, 2));
      const table = formatIsolationTable(report);
      console.log('[SttDiag][isolation] TABLE\n' + table);
      setSummary(`${report.rows.length} languages — see console [SttDiag][isolation]`);
    } finally {
      setRunning(false);
    }
  };

  return (
    <View style={styles.wrap}>
      <View style={styles.headerRow}>
        <Text style={styles.header}>DEV · STT ISOLATION (model map + direct-bypass + VAD)</Text>
        <Pressable onPress={runAll} disabled={running}>
          {running ? (
            <ActivityIndicator size="small" color={theme.color.primary} />
          ) : (
            <Text style={styles.runAll}>RUN ISOLATION</Text>
          )}
        </Pressable>
      </View>
      <Text style={styles.hint}>
        {summary ?? 'Logs [SttDiag][isolation] JSON + a markdown comparison table to console'}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  wrap: {
    borderRadius: theme.radius.md,
    borderWidth: 1,
    borderStyle: 'dashed',
    borderColor: theme.color.hairlineStrong,
    padding: 10,
    gap: 6,
  },
  headerRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  header: {
    color: theme.color.textFaint,
    fontSize: 9,
    fontWeight: '800',
    letterSpacing: 1.2,
  },
  runAll: {
    color: theme.color.primary,
    fontSize: 9,
    fontWeight: '800',
    letterSpacing: 1,
  },
  hint: { color: theme.color.textFaint, fontSize: 9 },
});

export const DevSttIsolationRow = memo(DevSttIsolationRowImpl);
