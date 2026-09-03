import React, { memo, useRef, useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';

import { LANGUAGES } from '../../config/languages';
import { runSttRoundTripTest, type RoundTripResult } from '../../core/diagnostics/SttRoundTripTest';
import { SttEngineProvider } from '../../core/stt/SttEngineProvider';
import { theme } from '../theme';
import { TTS_SAMPLE_TEXT } from './DevTtsTestRow';

/**
 * Dev-only STT accuracy diagnostic: runs runSttRoundTripTest per language
 * against a dedicated, isolated SttEngineProvider (never the live
 * transmitter's), and prints a structured result. STT accuracy
 * investigation tooling — not part of the app's real STT/TTS paths.
 *
 * Never rendered outside __DEV__.
 */
function DevSttRoundTripRowImpl() {
  const providerRef = useRef<SttEngineProvider | null>(null);
  providerRef.current ??= new SttEngineProvider();

  const [running, setRunning] = useState<string | null>(null);
  const [results, setResults] = useState<Record<string, RoundTripResult>>({});

  const runOne = async (languageCode: string) => {
    const text = TTS_SAMPLE_TEXT[languageCode];
    if (!text) return;
    setRunning(languageCode);
    try {
      const result = await runSttRoundTripTest(
        languageCode,
        text,
        providerRef.current!
      );
      console.log('[SttDiag][roundtrip]', JSON.stringify(result, null, 2));
      setResults((prev) => ({ ...prev, [languageCode]: result }));
    } finally {
      setRunning(null);
    }
  };

  const runAll = async () => {
    for (const lang of LANGUAGES) {
      // Sequential on purpose: each run swaps the loaded STT model, so
      // parallel runs would race each other's model load.
      // eslint-disable-next-line no-await-in-loop
      await runOne(lang.code);
    }
  };

  return (
    <View style={styles.wrap}>
      <View style={styles.headerRow}>
        <Text style={styles.header}>DEV · STT ROUND-TRIP TEST (TTS→STT)</Text>
        <Pressable onPress={runAll} disabled={running !== null}>
          <Text style={styles.runAll}>RUN ALL</Text>
        </Pressable>
      </View>
      <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.rail}>
        {LANGUAGES.map((lang) => {
          const result = results[lang.code];
          const status = running === lang.code
            ? 'running'
            : !result
              ? 'idle'
              : result.error
                ? 'error'
                : result.finalSttText === result.referenceText
                  ? 'match'
                  : 'mismatch';
          const color =
            status === 'match'
              ? theme.color.live
              : status === 'mismatch'
                ? theme.color.warn
                : status === 'error'
                  ? theme.color.danger
                  : theme.color.textFaint;
          return (
            <Pressable
              key={lang.code}
              onPress={() => runOne(lang.code)}
              disabled={running !== null}
              style={[styles.chip, { borderColor: `${color}66` }]}
            >
              {status === 'running' ? (
                <ActivityIndicator size="small" color={color} />
              ) : (
                <Text style={[styles.chipText, { color }]}>{lang.short}</Text>
              )}
            </Pressable>
          );
        })}
      </ScrollView>
      <Text style={styles.hint}>Results log to console as [SttDiag][roundtrip]</Text>
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
  rail: { gap: 6 },
  chip: {
    minWidth: 40,
    alignItems: 'center',
    paddingVertical: 6,
    paddingHorizontal: 10,
    borderRadius: theme.radius.sm,
    borderWidth: 1,
    backgroundColor: 'rgba(22, 28, 44, 0.55)',
  },
  chipText: { fontSize: 10, fontWeight: '800' },
  hint: { color: theme.color.textFaint, fontSize: 9 },
});

export const DevSttRoundTripRow = memo(DevSttRoundTripRowImpl);
