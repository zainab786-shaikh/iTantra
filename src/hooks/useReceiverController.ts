import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import type { ReceivedMessage } from '../core/receiver/types';
import { TtsManager } from '../core/tts/TtsManager';
import { INITIAL_TTS_PLAYBACK_STATE, type TtsPlaybackState } from '../core/tts/types';
import type { Transport } from '../core/transport/Transport';
import type { iTantraPacket } from '../core/types';

export interface ReceiverController {
  messages: ReceivedMessage[];
  ttsState: TtsPlaybackState;
  connected: boolean;
  clearHistory: () => void;
  /** Re-speak a previously received message, in its original language. */
  replay: (packetId: string) => void;
  /** TTS voice install state / installer, forwarded from TtsManager for the UI's model-download affordance. */
  voiceStatus: TtsManager['voiceStatus'];
  installVoice: TtsManager['installVoice'];
}

const MAX_HISTORY = 60;

/**
 * Owns the receiver pipeline: listens for incoming packets on `transport`,
 * hands each one to a TtsManager, and tracks per-message playback state for
 * the receiver UI.
 *
 * Mirrors useTransmitterController's shape (a single hook owning a
 * long-lived engine in a ref, exposing plain state to React) so the two
 * screens read as one consistent architecture.
 */
export function useReceiverController(transport: Transport): ReceiverController {
  const ttsManagerRef = useRef<TtsManager | null>(null);
  ttsManagerRef.current ??= new TtsManager();

  const [messages, setMessages] = useState<ReceivedMessage[]>([]);
  const [ttsState, setTtsState] = useState<TtsPlaybackState>(
    INITIAL_TTS_PLAYBACK_STATE
  );
  const [connected, setConnected] = useState(transport.isConnected());

  /** Maps a TtsManager request id back to the history row it belongs to — identity for normal messages, a synthetic id for replays. */
  const correlationRef = useRef(new Map<string, string>());
  const speakingRowRef = useRef<string | null>(null);

  useEffect(() => {
    const manager = ttsManagerRef.current!;
    return () => {
      void manager.dispose();
    };
  }, []);

  useEffect(
    () => transport.onConnectionChange(setConnected),
    [transport]
  );

  useEffect(() => {
    const manager = ttsManagerRef.current!;
    return manager.subscribe((state) => {
      setTtsState(state);

      const rowId = state.requestId
        ? (correlationRef.current.get(state.requestId) ?? state.requestId)
        : null;

      setMessages((prev) => {
        let changed = false;
        const next = prev.map((m) => {
          if (rowId && m.packet.id === rowId) {
            changed = true;
            if (state.phase === 'error') {
              return { ...m, state: 'error' as const, error: state.error };
            }
            return { ...m, state: 'speaking' as const, error: null };
          }
          // Anything previously "speaking" that is no longer the active
          // request has finished — the manager moved on (or went idle).
          if (m.state === 'speaking' && m.packet.id !== rowId) {
            changed = true;
            return { ...m, state: 'spoken' as const };
          }
          return m;
        });
        return changed ? next : prev;
      });

      speakingRowRef.current = state.phase === 'speaking' ? rowId : null;
    });
  }, []);

  const handlePacket = useCallback((packet: iTantraPacket) => {
    setMessages((prev) => {
      const entry: ReceivedMessage = {
        packet,
        state: 'received',
        error: null,
        receivedAt: Date.now(),
      };
      const next = [entry, ...prev];
      return next.length > MAX_HISTORY ? next.slice(0, MAX_HISTORY) : next;
    });

    correlationRef.current.set(packet.id, packet.id);
    void ttsManagerRef.current!.speakText(
      packet.text,
      packet.language,
      packet.priority,
      packet.id
    );

    // Reflect "handed to the queue" promptly; the subscription above takes
    // over from here once the manager actually starts on it.
    setMessages((prev) =>
      prev.map((m) =>
        m.packet.id === packet.id && m.state === 'received'
          ? { ...m, state: 'queued' as const }
          : m
      )
    );
  }, []);

  useEffect(
    () => transport.onPacketReceived(handlePacket),
    [transport, handlePacket]
  );

  const clearHistory = useCallback(() => setMessages([]), []);

  const replay = useCallback((packetId: string) => {
    setMessages((prev) => {
      const entry = prev.find((m) => m.packet.id === packetId);
      if (!entry) return prev;

      const replayId = `${packetId}::replay::${Date.now()}`;
      correlationRef.current.set(replayId, packetId);
      void ttsManagerRef.current!.speakText(
        entry.packet.text,
        entry.packet.language,
        entry.packet.priority,
        replayId
      );

      return prev.map((m) =>
        m.packet.id === packetId ? { ...m, state: 'queued' as const, error: null } : m
      );
    });
  }, []);

  return useMemo(
    () => ({
      messages,
      ttsState,
      connected,
      clearHistory,
      replay,
      voiceStatus: ttsManagerRef.current!.voiceStatus.bind(ttsManagerRef.current),
      installVoice: ttsManagerRef.current!.installVoice.bind(ttsManagerRef.current),
    }),
    [messages, ttsState, connected, clearHistory, replay]
  );
}
