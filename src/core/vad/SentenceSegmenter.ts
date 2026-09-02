import { SAMPLE_RATE, type VadConfig } from '../../config/vadConfig';
import { concatFloat32 } from '../audio/pcm';
import type { PcmFrame } from '../types';

/** A completed utterance, ready to hand to the recogniser. */
export interface AudioSegment {
  /** Contiguous 16 kHz mono audio, including pre-speech padding. */
  samples: Float32Array;
  durationMs: number;
  /** True when stopPtt() cut the utterance rather than a natural pause. */
  forced: boolean;
}

export interface SegmenterCallbacks {
  onSpeechStart: () => void;
  /** Fired on END_OF_SPEECH, on maxSpeechMs, or on a forced flush. */
  onSegment: (segment: AudioSegment) => void;
}

/**
 * Turns a stream of per-frame speech probabilities into discrete utterances.
 *
 * State machine:
 *
 *   SILENCE --p > speechThreshold--> SPEECH        (emit START_OF_SPEECH)
 *   SPEECH  --p < silenceThreshold for endOfSpeechSilenceMs--> SILENCE
 *                                                  (emit segment)
 *
 * Three details that matter more than the state machine itself:
 *
 *  1. **Hysteresis.** Entering speech needs `speechThreshold`; leaving it needs
 *     to fall under the lower `silenceThreshold`. A single threshold would
 *     chatter on every frame that sits near the line.
 *
 *  2. **Pre-speech padding.** A rolling ring buffer of the last
 *     `preSpeechPaddingMs` of *pre-trigger* audio is prepended to every segment.
 *     VAD always fires a frame or two into the first phoneme, and without this
 *     the decoder receives "ello" instead of "hello".
 *
 *  3. **Silence is buffered, not dropped.** Trailing silence stays in the
 *     segment because CTC decoders need the trailing context to close the final
 *     token; it is only discarded when it exceeds the end-of-speech window.
 */
export class SentenceSegmenter {
  private config: VadConfig;
  private readonly callbacks: SegmenterCallbacks;

  private speaking = false;
  /** Frames accumulated for the utterance in flight. */
  private buffer: Float32Array[] = [];
  /** Rolling pre-trigger audio, kept only while not speaking. */
  private padding: Float32Array[] = [];
  private paddingSamples = 0;
  /** Consecutive sub-threshold audio while speaking, in ms. */
  private silenceMs = 0;
  /** Length of the utterance in flight, in ms. */
  private speechMs = 0;
  /** Frames in the utterance whose probability cleared the speech threshold. */
  private voicedFrames = 0;
  /** Total frames in the utterance, for the voiced-ratio gate. */
  private totalFrames = 0;

  /**
   * Minimum fraction of an utterance that must look like speech.
   *
   * A door slam or a burst of music can trip the threshold for a frame or two
   * and then decay, producing a segment that is mostly silence. Whisper answers
   * such audio with a confident subtitle artifact ("[Music]"), so it is far
   * cheaper to refuse the decode than to filter the output afterwards.
   */
  private static readonly MIN_VOICED_RATIO = 0.35;

  constructor(config: VadConfig, callbacks: SegmenterCallbacks) {
    this.config = config;
    this.callbacks = callbacks;
  }

  /** Live-update tuning (e.g. the pause-length selector) without losing state. */
  setConfig(config: VadConfig): void {
    this.config = config;
  }

  get isSpeaking(): boolean {
    return this.speaking;
  }

  /** ms of speech accumulated so far, or null when idle. */
  get currentUtteranceMs(): number | null {
    return this.speaking ? this.speechMs : null;
  }

  /**
   * Feed one frame plus its speech probability.
   *
   * The frame is copied on the way in: `PcmFrame.samples` is a view into the
   * capture service's scratch buffer and is invalidated as soon as this returns.
   */
  push(frame: PcmFrame, probability: number): void {
    const frameMs = (frame.samples.length / SAMPLE_RATE) * 1000;
    const copy = Float32Array.from(frame.samples);

    if (!this.speaking) {
      this.retainPadding(copy);

      if (probability >= this.config.speechThreshold) {
        this.speaking = true;
        this.silenceMs = 0;
        this.speechMs = 0;
        this.voicedFrames = 1;
        this.totalFrames = 1;
        // Seed the utterance with the pre-trigger ring, then the trigger frame.
        this.buffer = [...this.padding, copy];
        this.padding = [];
        this.paddingSamples = 0;
        this.callbacks.onSpeechStart();
      }
      return;
    }

    this.buffer.push(copy);
    this.speechMs += frameMs;
    this.totalFrames++;
    if (probability >= this.config.speechThreshold) this.voicedFrames++;

    if (probability < this.config.silenceThreshold) {
      this.silenceMs += frameMs;
    } else {
      // Any confident frame resets the pause clock; that is what lets someone
      // pause mid-sentence for breath without being cut off.
      this.silenceMs = 0;
    }

    if (this.silenceMs >= this.config.endOfSpeechSilenceMs) {
      this.finalize(false);
      return;
    }

    if (this.speechMs >= this.config.maxSpeechMs) {
      // Bound the worst case: flush and immediately continue a new utterance so
      // a monologue still produces packets.
      this.finalize(true);
    }
  }

  /**
   * Force the utterance in flight to close, for stopPtt().
   * No-op when nothing is buffered.
   */
  flush(): void {
    if (!this.speaking) return;
    this.finalize(true);
  }

  /** Drop everything without emitting. */
  reset(): void {
    this.speaking = false;
    this.buffer = [];
    this.padding = [];
    this.paddingSamples = 0;
    this.silenceMs = 0;
    this.speechMs = 0;
    this.voicedFrames = 0;
    this.totalFrames = 0;
  }

  /** Maintain the fixed-length pre-trigger ring buffer. */
  private retainPadding(frame: Float32Array): void {
    const maxSamples = Math.floor(
      (this.config.preSpeechPaddingMs / 1000) * SAMPLE_RATE
    );
    if (maxSamples <= 0) return;

    this.padding.push(frame);
    this.paddingSamples += frame.length;

    while (this.paddingSamples > maxSamples && this.padding.length > 0) {
      this.paddingSamples -= this.padding.shift()!.length;
    }
  }

  private finalize(forced: boolean): void {
    const frames = this.buffer;
    const durationMs = this.speechMs;
    const voicedRatio =
      this.totalFrames > 0 ? this.voicedFrames / this.totalFrames : 0;

    this.speaking = false;
    this.buffer = [];
    this.silenceMs = 0;
    this.speechMs = 0;
    this.voicedFrames = 0;
    this.totalFrames = 0;

    // Discard clicks, coughs and door slams rather than paying for a decode
    // that will return an empty string.
    if (durationMs < this.config.minSpeechMs || frames.length === 0) return;

    if (voicedRatio < SentenceSegmenter.MIN_VOICED_RATIO) {
      console.log(
        `[Segmenter] dropped ${Math.round(durationMs)}ms segment: only ` +
          `${Math.round(voicedRatio * 100)}% voiced (needs ` +
          `${SentenceSegmenter.MIN_VOICED_RATIO * 100}%)`
      );
      return;
    }

    this.callbacks.onSegment({
      samples: concatFloat32(frames),
      durationMs,
      forced,
    });
  }
}
