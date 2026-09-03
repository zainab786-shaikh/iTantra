import type { PacketPriority } from '../types';

/** Install state of a TTS voice on this device. Mirrors STT's ModelStatus shape. */
export type TtsVoiceStatus =
  | { state: 'not-installed' }
  | { state: 'downloading'; percent: number; phase: 'downloading' | 'extracting' }
  | { state: 'installed'; path: string }
  | { state: 'error'; message: string };

/**
 * What the UI needs to render "the receiver is speaking" — deliberately
 * free of engine/model vocabulary (ONNX, VITS, Piper, MMS) per the UI
 * requirement that the normal user sees communication concepts, not model
 * names.
 */
export type TtsPlaybackPhase =
  | 'idle'
  | 'loading-voice'
  | 'speaking'
  | 'error';

export interface TtsPlaybackState {
  phase: TtsPlaybackPhase;
  /** Correlates to the `id` passed into TtsManager.speakText's caller-tracked request, so a receiver UI can tell which specific message this state describes. */
  requestId: string | null;
  /** The packet currently being spoken, or the most recently spoken one. */
  language: string | null;
  text: string | null;
  priority: PacketPriority | null;
  /** True while a CRITICAL message holds the floor. */
  isCritical: boolean;
  /** Set when `phase` is 'error'. A short, user-facing message — never a raw exception string. */
  error: string | null;
}

/** One request to speak a piece of received text. */
export interface SpeakRequest {
  id: string;
  text: string;
  language: string;
  priority: PacketPriority;
}

export const INITIAL_TTS_PLAYBACK_STATE: TtsPlaybackState = {
  phase: 'idle',
  requestId: null,
  language: null,
  text: null,
  priority: null,
  isCritical: false,
  error: null,
};
