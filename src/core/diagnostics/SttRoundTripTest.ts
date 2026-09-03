import { DEFAULT_VAD_CONFIG, SAMPLE_RATE } from '../../config/vadConfig';
import type { SttEngineProvider } from '../stt/SttEngineProvider';
import type { PcmFrame } from '../types';
import { EnergyVad } from '../vad/EnergyVad';
import { SentenceSegmenter, type AudioSegment } from '../vad/SentenceSegmenter';
import { synthesizeReferenceAudio } from './synthesizeReferenceAudio';

/**
 * STT accuracy diagnostic tool. TEMPORARY, dev-only — not part of the app's
 * production TTS or STT code paths, and does not modify either.
 *
 * The problem this solves: neither the developer nor most testers can judge
 * whether Kannada/Tamil/Telugu/etc. STT output is *correct*, only whether it
 * produced *something*. But TTS for all 10 languages was just independently
 * verified on-device, so it can serve as a known-good text-to-audio source:
 * synthesize a reference sentence, feed the resulting audio through the
 * REAL EnergyVad + SentenceSegmenter + STT pipeline (the same classes and
 * config the live transmitter uses, just fresh instances so this cannot
 * interfere with an in-progress real transmission), and compare the STT
 * output against the known input text. This isolates recognition/
 * segmentation problems from "I can't tell if this Odia sentence is right."
 *
 * Caveat, stated plainly: TTS audio is clean synthetic speech, not a real
 * microphone recording with room acoustics, accent variation, or handling
 * noise. A pass here proves the pipeline CAN produce correct text for that
 * exact sentence; it does not prove real-world mic accuracy. A failure here
 * is strong evidence of a genuine pipeline/model problem, independent of
 * real speech.
 *
 * Segment aggregation: a single synthesized sentence can legitimately trip the
 * segmenter more than once (a mid-sentence pause crossing endOfSpeechSilenceMs
 * closes one utterance and opens another). An earlier version of this harness
 * kept only the LAST segment the segmenter emitted (`captured = segment`
 * overwrote on every callback) and transcribed just that one — for a
 * multi-segment utterance this silently threw away everything before the
 * final fragment and scored the test on a partial sentence. This version
 * collects every accepted segment and transcribes each one, so the reported
 * result reflects the complete utterance. This is a diagnostic-harness-only
 * fix; SentenceSegmenter itself is untouched.
 */
export interface SegmentOutcome {
  index: number;
  durationMs: number;
  forced: boolean;
  rawSttText: string | null;
  finalSttText: string | null;
  decodeMs: number | null;
  error: string | null;
}

export interface RoundTripResult {
  language: string;
  referenceText: string;
  synthesizedAudioMs: number;
  /** How many segments the segmenter accepted for this utterance. */
  segmentCount: number;
  /** Per-segment outcome, in emission order. */
  segments: SegmentOutcome[];
  /** Sum of every accepted segment's duration, or null if none were accepted. */
  segmentedAudioMs: number | null;
  /** True when the segmenter accepted zero segments (dropped or never triggered). */
  segmentDropped: boolean;
  /** Every segment's STT output concatenated in order, space-joined. */
  combinedRawSttText: string | null;
  /** Every segment's repaired STT output concatenated in order, space-joined — this is what the old single-segment `finalSttText` meant, now covering the whole utterance. */
  finalSttText: string | null;
  /** Sum of every segment's decode time. */
  decodeMs: number | null;
  error: string | null;
}

export async function runSttRoundTripTest(
  languageCode: string,
  referenceText: string,
  sttProvider: SttEngineProvider
): Promise<RoundTripResult> {
  const base: RoundTripResult = {
    language: languageCode,
    referenceText,
    synthesizedAudioMs: 0,
    segmentCount: 0,
    segments: [],
    segmentedAudioMs: null,
    segmentDropped: false,
    combinedRawSttText: null,
    finalSttText: null,
    decodeMs: null,
    error: null,
  };

  try {
    const reference = await synthesizeReferenceAudio(languageCode, referenceText);
    const float32 = reference.samples;
    const synthesizedAudioMs = reference.durationMs;

    // Feed the synthesized audio through fresh VAD + segmenter instances —
    // same classes and config as the live transmitter, but isolated so this
    // cannot disturb a real in-progress transmission.
    const vad = new EnergyVad();
    await vad.initialize();
    const captured: AudioSegment[] = [];
    const segmenter = new SentenceSegmenter(DEFAULT_VAD_CONFIG, {
      onSpeechStart: () => undefined,
      onSegment: (segment) => {
        captured.push(segment);
      },
    });

    const frameSize = DEFAULT_VAD_CONFIG.frameSize;
    const originalLog = console.log;
    // Segmenter logs its own drop/accept diagnostics; suppress duplication
    // here since the caller already gets a structured result.
    let sawDropLog = false;
    console.log = (...args: unknown[]) => {
      if (typeof args[0] === 'string' && args[0].includes('[Segmenter] dropped')) {
        sawDropLog = true;
      }
      originalLog(...args);
    };
    try {
      for (let offset = 0; offset + frameSize <= float32.length; offset += frameSize) {
        const frame: PcmFrame = {
          samples: float32.subarray(offset, offset + frameSize),
          sampleRate: SAMPLE_RATE,
          rms: 0,
          timestamp: (offset / SAMPLE_RATE) * 1000,
        };
        const probability = vad.process(frame.samples);
        segmenter.push(frame, probability);
      }
      // Force-close a trailing utterance the way stopPtt() would, so a
      // sentence that runs to the end of the synthesized clip without
      // trailing silence still gets scored.
      segmenter.flush();
    } finally {
      console.log = originalLog;
    }

    if (captured.length === 0) {
      const dropped = sawDropLog;
      return {
        ...base,
        synthesizedAudioMs,
        segmentDropped: dropped,
        error: dropped
          ? 'VAD/segmenter dropped the synthesized speech (insufficiently voiced) — see [Segmenter] log'
          : 'VAD never detected speech in the synthesized audio',
      };
    }

    // SttEngineProvider defaults to its simulated backend until prepare()
    // successfully swaps it to the real sherpa-onnx engine — this call was
    // missing in an earlier version of this harness, which silently made
    // every round-trip result reflect the placeholder backend instead of
    // the actual STT model.
    const prepared = await sttProvider.prepare(languageCode);
    if (prepared.kind !== 'sherpa-onnx') {
      return {
        ...base,
        synthesizedAudioMs,
        segmentCount: captured.length,
        segmentedAudioMs: captured.reduce((sum, s) => sum + s.durationMs, 0),
        error: `STT engine did not load the real model for ${languageCode}: ${prepared.reason ?? 'unknown reason'}`,
      };
    }

    // Transcribe every accepted segment, in order, and aggregate. Sequential
    // on purpose: these share one loaded engine instance.
    const segments: SegmentOutcome[] = [];
    for (let i = 0; i < captured.length; i++) {
      const segment = captured[i]!;
      const decodeStart = Date.now();
      try {
        // eslint-disable-next-line no-await-in-loop
        const { text, rawText } = await sttProvider.transcribe(segment.samples, languageCode);
        segments.push({
          index: i,
          durationMs: segment.durationMs,
          forced: segment.forced,
          rawSttText: rawText ?? null,
          finalSttText: text,
          decodeMs: Date.now() - decodeStart,
          error: null,
        });
      } catch (segError) {
        segments.push({
          index: i,
          durationMs: segment.durationMs,
          forced: segment.forced,
          rawSttText: null,
          finalSttText: null,
          decodeMs: Date.now() - decodeStart,
          error: segError instanceof Error ? segError.message : String(segError),
        });
      }
    }

    const combinedFinal = segments
      .map((s) => s.finalSttText)
      .filter((t): t is string => !!t && t.length > 0)
      .join(' ');
    const combinedRaw = segments
      .map((s) => s.rawSttText)
      .filter((t): t is string => !!t && t.length > 0)
      .join(' ');
    const totalDecodeMs = segments.reduce((sum, s) => sum + (s.decodeMs ?? 0), 0);
    const totalSegmentedMs = captured.reduce((sum, s) => sum + s.durationMs, 0);

    return {
      ...base,
      synthesizedAudioMs,
      segmentCount: captured.length,
      segments,
      segmentedAudioMs: totalSegmentedMs,
      combinedRawSttText: combinedRaw,
      finalSttText: combinedFinal,
      decodeMs: totalDecodeMs,
    };
  } catch (error) {
    return { ...base, error: error instanceof Error ? error.message : String(error) };
  }
}
