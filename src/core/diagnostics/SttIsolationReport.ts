import { LANGUAGES } from '../../config/languages';
import type { SttEngineProvider } from '../stt/SttEngineProvider';
import { repairScript } from '../stt/indicScript';
import { runDirectSttTest, type DirectSttResult } from './DirectSttTest';
import {
  runModelMappingReport,
  verifySharedModelIsIdentical,
  type ModelMappingEntry,
} from './ModelMappingReport';
import { runSttRoundTripTest, type RoundTripResult } from './SttRoundTripTest';

/**
 * Isolation orchestrator. TEMPORARY, dev-only.
 *
 * Runs, per language: model mapping, direct TTS->STT (no VAD/segmenter), and
 * the VAD+segmenter round-trip — then classifies the result against the A/B
 * comparison rule this investigation is built on:
 *
 *   A (VAD+segmentation+STT) fails, B (direct STT) succeeds -> VAD/segmentation
 *   A and B both fail                                       -> model/config/audio-format more likely
 *   B succeeds                                               -> do NOT blame the model yet either way
 *
 * Deliberately does not compute a match/accuracy percentage against the
 * reference text — one synthetic sentence per language is triage evidence,
 * not a corpus, and this file must not manufacture an accuracy claim it
 * cannot support. It only classifies WHERE a failure most likely sits.
 *
 * No production STT/TTS/VAD code is modified by running this.
 */
export type Diagnosis =
  | 'VAD/segmentation'
  | 'model selection/configuration'
  | 'audio format'
  | 'preprocessing'
  | 'postprocessing'
  | 'model recognition quality'
  | 'insufficient evidence';

export interface IsolationRow {
  languageCode: string;
  languageLabel: string;
  mapping: ModelMappingEntry;
  direct: DirectSttResult;
  roundTrip: RoundTripResult;
  diagnosis: Diagnosis;
  diagnosisNote: string;
}

export interface IsolationReport {
  rows: IsolationRow[];
  /** Languages sharing a decoder id whose on-disk directories disagree — would mean the "multilingual" claim is not actually true on disk. */
  sharedModelInconsistencies: ReturnType<typeof verifySharedModelIsIdentical>;
}

function isEmpty(text: string | null | undefined): boolean {
  return !text || text.trim().length === 0;
}

function classify(
  mapping: ModelMappingEntry,
  direct: DirectSttResult,
  roundTrip: RoundTripResult
): { diagnosis: Diagnosis; note: string } {
  if (!mapping.loadedSuccessfully) {
    return {
      diagnosis: 'model selection/configuration',
      note: `Real decoder never loaded for this language: ${mapping.loadFailureReason ?? 'unknown reason'}.`,
    };
  }

  if (direct.error && !direct.modelLoaded) {
    return {
      diagnosis: 'model selection/configuration',
      note: `Direct test could not load the model: ${direct.error}`,
    };
  }

  if (!direct.audioFormat.formatOk) {
    return {
      diagnosis: 'audio format',
      note: `Synthesized audio failed the 16kHz/mono/Float32 check before it ever reached the engine.`,
    };
  }

  const directFailed = direct.error !== null || isEmpty(direct.finalSttText);
  const roundTripFailed = roundTrip.error !== null || isEmpty(roundTrip.finalSttText);

  if (roundTripFailed && !directFailed) {
    return {
      diagnosis: 'VAD/segmentation',
      note: roundTrip.segmentDropped
        ? 'VAD+segmenter pipeline dropped or never captured this utterance (voiced-ratio gate or no trigger), but the exact same audio decoded fine when sent straight to STT.'
        : 'VAD+segmenter pipeline produced no usable text, but the exact same audio decoded fine when sent straight to STT.',
    };
  }

  if (directFailed) {
    // Both A and B failed (or B failed outright). Distinguish a postprocessing
    // rejection (repairScript discarded recoverable-looking Indic text) from a
    // genuine recognition failure (foreign/unrecoverable script, or truly
    // empty output) using the same repair logic the production path applies —
    // called here read-only, for classification, not to alter any output.
    if (!isEmpty(direct.rawSttText)) {
      const repaired = repairScript(direct.rawSttText!, direct.language);
      if (repaired.rejected || (!isEmpty(repaired.text) && isEmpty(direct.finalSttText))) {
        return {
          diagnosis: 'postprocessing',
          note: `Engine returned non-empty raw text ("${direct.rawSttText}") but it was rejected/discarded by script repair — check whether repairScript's rejection threshold is discarding recoverable output, separately from model quality.`,
        };
      }
    }

    return {
      diagnosis: 'model recognition quality',
      note:
        'Direct-to-STT (no VAD/segmentation in the way) still produced no usable text on this one synthetic sentence. This is evidence of a problem, not a corpus-level accuracy measurement — do not generalize to "X% accuracy" from a single sample.',
    };
  }

  return {
    diagnosis: 'insufficient evidence',
    note: 'Direct STT produced non-empty output for this one synthetic sentence. That rules out VAD/segmentation and total model failure for this sample, but one synthetic sentence is not enough to certify accuracy — needs a corpus-level pass before any quality claim.',
  };
}

export async function runSttIsolationReport(
  sttProvider: SttEngineProvider,
  sampleText: Record<string, string>
): Promise<IsolationReport> {
  const mappingEntries = await runModelMappingReport(sttProvider);
  const mappingByLang = new Map(mappingEntries.map((e) => [e.languageCode, e]));
  const sharedModelInconsistencies = verifySharedModelIsIdentical(mappingEntries);

  const rows: IsolationRow[] = [];
  for (const lang of LANGUAGES) {
    const text = sampleText[lang.code];
    const mapping = mappingByLang.get(lang.code)!;
    if (!text) {
      // eslint-disable-next-line no-continue
      continue;
    }

    // Sequential: both tests swap the loaded engine, so parallel runs would
    // race each other's model load exactly like the existing round-trip UI.
    // eslint-disable-next-line no-await-in-loop
    const direct = await runDirectSttTest(lang.code, text, sttProvider);
    // eslint-disable-next-line no-await-in-loop
    const roundTrip = await runSttRoundTripTest(lang.code, text, sttProvider);

    const { diagnosis, note } = classify(mapping, direct, roundTrip);
    rows.push({
      languageCode: lang.code,
      languageLabel: lang.label,
      mapping,
      direct,
      roundTrip,
      diagnosis,
      diagnosisNote: note,
    });
  }

  return { rows, sharedModelInconsistencies };
}

function truncate(text: string | null | undefined, max = 40): string {
  if (!text) return '(empty)';
  const t = text.trim();
  if (t.length === 0) return '(empty)';
  return t.length > max ? `${t.slice(0, max)}…` : t;
}

/** Renders the final report table exactly as requested for review. */
export function formatIsolationTable(report: IsolationReport): string {
  const header =
    '| Language | Actual model | Direct STT | VAD+STT | Audio format OK | Model loaded | Diagnosis |\n' +
    '|----------|--------------|------------|---------|------------------|--------------|-----------|';
  const lines = report.rows.map((row) => {
    const model = row.mapping.modelId
      ? `${row.mapping.modelId} (${row.mapping.modelType})`
      : '(none registered)';
    const directCell = row.direct.error
      ? `ERROR: ${truncate(row.direct.error)}`
      : truncate(row.direct.finalSttText);
    const vadCell = row.roundTrip.error
      ? `ERROR: ${truncate(row.roundTrip.error)}`
      : `${truncate(row.roundTrip.finalSttText)} (${row.roundTrip.segmentCount} seg)`;
    const formatOk = row.direct.audioFormat.formatOk ? 'yes' : 'no';
    const loaded = row.mapping.loadedSuccessfully ? 'yes' : 'no';
    return `| ${row.languageLabel} (${row.languageCode}) | ${model} | ${directCell} | ${vadCell} | ${formatOk} | ${loaded} | ${row.diagnosis} |`;
  });

  const inconsistencyNote = report.sharedModelInconsistencies.some((s) => !s.consistent)
    ? '\n\n**WARNING**: shared-model directories disagree across languages — see sharedModelInconsistencies in the JSON report.'
    : '\n\nAll languages sharing a decoder id resolved to the identical on-disk directory (no per-language config divergence found).';

  return `${header}\n${lines.join('\n')}${inconsistencyNote}`;
}
