import type { iTantraPacket } from '../types';

/** Lifecycle of one received message through the TTS pipeline. */
export type ReceivedMessageState = 'received' | 'queued' | 'speaking' | 'spoken' | 'error';

export interface ReceivedMessage {
  packet: iTantraPacket;
  state: ReceivedMessageState;
  /** Set when `state` is 'error' — a short, user-facing message. */
  error: string | null;
  receivedAt: number;
}
