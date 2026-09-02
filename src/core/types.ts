/**
 * Core contracts for the iTantra transmitter pipeline.
 *
 * Signal flow:
 *   mic -> AudioCaptureService (16 kHz / mono / int16)
 *       -> VadBackend (per-frame speech probability)
 *       -> SentenceSegmenter (START_OF_SPEECH .. END_OF_SPEECH)
 *       -> SttBackend.transcribe(segment)
 *       -> packetFactory -> Transport.sendPacket()
 */

/** Priority band carried on the wire. Higher bands may pre-empt lower ones downstream. */
export type PacketPriority = 'NORMAL' | 'MEDIUM' | 'HIGH' | 'CRITICAL';

/**
 * The wire format produced by the transmitter. This is the single contract the
 * transport module consumes; nothing else about the engine is public.
 */
export interface iTantraPacket {
  id: string; // UUID v4
  senderId: string; // Device/User unique ID
  timestamp: number;
  language: string;
  text: string;
  priority: PacketPriority;
  isCompressed: boolean;
}

/** High-level state of the transmitter, surfaced to the UI. */
export type TransmitterStatus =
  | 'IDLE' // engine warm, mic closed
  | 'INITIALIZING' // models loading
  | 'LISTENING' // mic open, no speech yet
  | 'SPEAKING' // VAD says speech is in progress
  | 'TRANSCRIBING' // segment flushed to STT, awaiting text
  | 'ERROR';

/** Voice-activity phase emitted by the segmenter. */
export type VadEvent = 'START_OF_SPEECH' | 'END_OF_SPEECH';

/**
 * A finalized utterance, before it is packaged into an {@link iTantraPacket}.
 */
export interface TranscriptionResult {
  text: string;
  language: string;
  /** Wall-clock ms from END_OF_SPEECH to decoded text. */
  latencyMs: number;
  /** Duration of the audio segment that produced this text, in ms. */
  durationMs: number;
  /** Set when the utterance was cut short by stopPtt() rather than a natural pause. */
  forced: boolean;
}

/** Everything the UI needs to render the transmitter, in one object. */
export interface TranscriptionState {
  status: TransmitterStatus;
  /** Text decoded so far for the utterance in flight (empty when idle). */
  liveText: string;
  /** The most recent finalized transcription. */
  lastResult: TranscriptionResult | null;
  /** Smoothed 0..1 input level, for the visualizer. */
  level: number;
  /** True between START_OF_SPEECH and END_OF_SPEECH. */
  isSpeaking: boolean;
  /** Decode latency of the last finalized utterance, in ms. */
  latencyMs: number | null;
  /** ms since the current utterance started, or null when not speaking. */
  utteranceMs: number | null;
  error: string | null;
  /** Which STT backend actually serviced the last request. */
  engine: SttEngineKind;
}

/** Which concrete STT implementation is live. */
export type SttEngineKind = 'sherpa-onnx' | 'simulated' | 'none';

/** Which concrete VAD implementation is live. */
export type VadBackendKind = 'silero-onnx' | 'energy';

/** A frame of PCM handed down the pipeline. Always 16 kHz, mono. */
export interface PcmFrame {
  /** Normalized samples in [-1, 1]. */
  samples: Float32Array;
  /** Sample rate of {@link samples}; guaranteed 16000 after resampling. */
  sampleRate: number;
  /** Root-mean-square amplitude of this frame, 0..1. */
  rms: number;
  /** ms since capture started. */
  timestamp: number;
}
