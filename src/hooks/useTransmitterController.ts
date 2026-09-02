import {
  requestRecordingPermissionsAsync,
  setAudioModeAsync,
  useAudioStream,
} from 'expo-audio';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Platform } from 'react-native';
import { useSharedValue, type SharedValue } from 'react-native-reanimated';

import { DEFAULT_LANGUAGE, findLanguage } from '../config/languages';
import { PRIMARY_MODEL, resolveModelForLanguage } from '../config/models';
import { DEFAULT_VAD_CONFIG, SAMPLE_RATE, type VadConfig } from '../config/vadConfig';
import { AudioCaptureService } from '../core/audio/AudioCaptureService';
import { dumpSegmentWav } from '../core/audio/debugWav';
import { rmsToLevel } from '../core/audio/pcm';
import { SyntheticAudioSource } from '../core/audio/SyntheticAudioSource';
import { getSenderId } from '../core/device/deviceId';
import { buildPacket } from '../core/packet/packetFactory';
import { isNonSpeechArtifact } from '../core/stt/hallucinations';
import { ModelManager, type ModelStatus } from '../core/stt/ModelManager';
import { SttEngineProvider } from '../core/stt/SttEngineProvider';
import { MockTransport } from '../core/transport/MockTransport';
import type { Transport } from '../core/transport/Transport';
import type {
  iTantraPacket,
  PcmFrame,
  SttEngineKind,
  TranscriptionResult,
  TranscriptionState,
} from '../core/types';
import { EnergyVad } from '../core/vad/EnergyVad';
import { SentenceSegmenter, type AudioSegment } from '../core/vad/SentenceSegmenter';
import type { VadBackend } from '../core/vad/VadBackend';

/** One entry in the transmitted-message log. */
export interface LogEntry {
  packet: iTantraPacket;
  /** Whether the transport accepted the packet. */
  delivered: boolean;
  latencyMs: number;
  /** True when the text came from the simulated recogniser, not a real decode. */
  simulated: boolean;
}

export interface TransmitterControllerOptions {
  /** Defaults to a logging MockTransport. */
  transport?: Transport;
  /** VAD/segmenter tuning overrides. */
  vadConfig?: Partial<VadConfig>;
  /** Cap on retained log entries. */
  maxLogEntries?: number;
}

export interface TransmitterController {
  transcriptionState: TranscriptionState;
  /** Begin capture; VAD then segments speech automatically. */
  startPtt: () => Promise<void>;
  /** Finalize the utterance in flight and stop capture. */
  stopPtt: () => Promise<void>;
  isActive: boolean;
  language: string;
  setLanguage: (code: string) => void;
  /** End-of-speech pause, in ms. */
  pauseMs: number;
  setPauseMs: (ms: number) => void;
  log: LogEntry[];
  clearLog: () => void;
  senderId: string;
  connected: boolean;
  /**
   * 0..1 input level on the UI thread. A shared value rather than React state
   * so the visualizer animates at frame rate without re-rendering the tree.
   */
  level: SharedValue<number>;
  /** True when audio is synthesised because no microphone is reachable. */
  usingSyntheticAudio: boolean;
  transport: Transport;
  /** Install state of the speech model. */
  modelStatus: ModelStatus;
  /** Download and install the speech model. Safe to call twice. */
  installModel: () => Promise<void>;
  /** Label of the decoder the app installs. */
  modelLabel: string;
  modelSizeMb: number;
}

/**
 * Owns the transmitter pipeline and exposes it to React.
 *
 * Everything stateful (capture framing, VAD, segmenter, recogniser) lives in
 * refs, because these are mutable engines that must survive re-renders. React
 * state carries only what the UI paints.
 */
export function useTransmitterController(
  options: TransmitterControllerOptions = {}
): TransmitterController {
  const { maxLogEntries = 40 } = options;

  // A default transport must be created once, not on every render.
  const fallbackTransport = useRef<Transport | null>(null);
  fallbackTransport.current ??= new MockTransport();
  const transport = options.transport ?? fallbackTransport.current;

  const [language, setLanguageState] = useState(DEFAULT_LANGUAGE.code);
  const [pauseMs, setPauseMsState] = useState(
    DEFAULT_VAD_CONFIG.endOfSpeechSilenceMs
  );
  const [isActive, setIsActive] = useState(false);
  const [log, setLog] = useState<LogEntry[]>([]);
  const [senderId, setSenderId] = useState('ITX-------');
  const [connected, setConnected] = useState(transport.isConnected());
  const [usingSyntheticAudio, setUsingSyntheticAudio] = useState(false);
  const [state, setState] = useState<TranscriptionState>(INITIAL_STATE);
  const [modelStatus, setModelStatus] = useState<ModelStatus>({
    state: 'not-installed',
  });

  const level = useSharedValue(0);

  // Mirror React state into refs so the audio callbacks — which are created
  // once and must not be torn down per render — always read current values.
  const languageRef = useRef(language);
  languageRef.current = language;
  const activeRef = useRef(false);
  /** Set once the OS has granted RECORD_AUDIO, to skip re-asking each press. */
  const micGrantedRef = useRef(false);

  const vadConfigRef = useRef<VadConfig>({
    ...DEFAULT_VAD_CONFIG,
    ...options.vadConfig,
    endOfSpeechSilenceMs: pauseMs,
  });

  const vadRef = useRef<VadBackend | null>(null);
  const segmenterRef = useRef<SentenceSegmenter | null>(null);
  const captureRef = useRef<AudioCaptureService | null>(null);
  const syntheticRef = useRef<SyntheticAudioSource | null>(null);
  const sttRef = useRef<SttEngineProvider | null>(null);
  const speechStartedAt = useRef<number | null>(null);

  const modelsRef = useRef<ModelManager | null>(null);
  modelsRef.current ??= new ModelManager();
  sttRef.current ??= new SttEngineProvider();
  vadRef.current ??= new EnergyVad();

  const patch = useCallback((next: Partial<TranscriptionState>) => {
    setState((prev) => ({ ...prev, ...next }));
  }, []);

  // Read inside handleSegment, which must not be rebuilt when the ID resolves.
  const senderIdRef = useRef(senderId);
  senderIdRef.current = senderId;

  /** Decode a finalized segment, package it, and hand it to the transport. */
  const handleSegment = useCallback(
    async (segment: AudioSegment) => {
      const startedAt = Date.now();
      patch({ status: 'TRANSCRIBING', isSpeaking: false });

      const languageCode = languageRef.current;
      try {
        const stt = sttRef.current!;
        if (__DEV__) {
          // Capture exactly what the recogniser is about to receive.
          void dumpSegmentWav(
            segment.samples,
            `/data/user/0/com.itantra.app/files/last-segment.wav`
          ).catch(() => undefined);
        }
        const { text } = await stt.transcribe(segment.samples, languageCode);
        const latencyMs = Date.now() - startedAt;

        if (text.length === 0 || isNonSpeechArtifact(text)) {
          // Either nothing decoded, or Whisper answered non-speech audio with a
          // subtitle artifact such as "[Music]". Neither is a transmission.
          if (text.length > 0) {
            console.log(`[Transmitter] discarded non-speech output: "${text}"`);
          }
          patch({
            status: activeRef.current ? 'LISTENING' : 'IDLE',
            liveText: '',
          });
          return;
        }

        const result: TranscriptionResult = {
          text,
          language: languageCode,
          latencyMs,
          durationMs: segment.durationMs,
          forced: segment.forced,
        };

        const packet = buildPacket({
          text,
          language: languageCode,
          senderId: senderIdRef.current,
        });

        const engineKind = stt.status.kind;
        const delivered = await transport.sendPacket(packet);

        setLog((prev) =>
          [
            {
              packet,
              delivered,
              latencyMs,
              simulated: engineKind === 'simulated',
            },
            ...prev,
          ].slice(0, maxLogEntries)
        );

        patch({
          status: activeRef.current ? 'LISTENING' : 'IDLE',
          liveText: '',
          lastResult: result,
          latencyMs,
          engine: engineKind,
          isSpeaking: false,
          error: null,
        });
      } catch (error) {
        patch({
          status: 'ERROR',
          error: error instanceof Error ? error.message : String(error),
        });
      }
    },
    [maxLogEntries, patch, transport]
  );

  /** One frame of 16 kHz mono audio: score it, then advance the segmenter. */
  const handleFrame = useCallback(
    (frame: PcmFrame) => {
      const probability = vadRef.current?.process(frame.samples) ?? 0;
      // Written on the JS thread, read by the UI thread each frame.
      level.value = rmsToLevel(frame.rms);
      segmenterRef.current?.push(frame, probability);
    },
    [level]
  );

  // Build the segmenter once, wiring it to the frame handler above.
  if (!segmenterRef.current) {
    segmenterRef.current = new SentenceSegmenter(vadConfigRef.current, {
      onSpeechStart: () => {
        speechStartedAt.current = Date.now();
        patch({ status: 'SPEAKING', isSpeaking: true });
      },
      onSegment: (segment) => {
        speechStartedAt.current = null;
        void handleSegment(segment);
      },
    });
  }
  if (!captureRef.current) {
    captureRef.current = new AudioCaptureService(
      vadConfigRef.current.frameSize,
      handleFrame
    );
  }
  if (!syntheticRef.current) {
    syntheticRef.current = new SyntheticAudioSource(
      vadConfigRef.current.frameSize,
      handleFrame
    );
  }

  // Real microphone stream. The hook must be called unconditionally; on web it
  // resolves to a documented no-op, which is why the synthetic source exists.
  const { stream } = useAudioStream({
    sampleRate: SAMPLE_RATE,
    channels: 1,
    encoding: 'int16',
    onBuffer: (buffer) => {
      if (!activeRef.current) return;
      captureRef.current?.pushBuffer(buffer.data, buffer.sampleRate);
    },
  });

  useEffect(() => {
    void getSenderId().then(setSenderId);
  }, []);

  // Report whether the selected language's decoder is on disk, and if so load
  // it now rather than on first press — paying a cold model load inside
  // startPtt is what made the button feel dead. Re-runs on language change,
  // because different languages route to different models.
  useEffect(() => {
    const model = resolveModelForLanguage(language) ?? PRIMARY_MODEL;
    let cancelled = false;

    void (async () => {
      // A model swap invalidates the previously resolved path.
      modelsRef.current!.invalidate();
      const status = await modelsRef.current!.status(model);
      if (cancelled) return;
      setModelStatus(status);
      if (status.state !== 'installed') return;
      const engine = await sttRef.current!.prepare(language);
      if (!cancelled) patch({ engine: engine.kind });
    })();

    return () => {
      cancelled = true;
    };
  }, [language, patch]);

  useEffect(() => transport.onConnectionChange(setConnected), [transport]);

  // Keep the live utterance timer ticking for the UI without touching the
  // audio path.
  useEffect(() => {
    if (!state.isSpeaking) return;
    const id = setInterval(() => {
      const started = speechStartedAt.current;
      patch({ utteranceMs: started ? Date.now() - started : null });
    }, 100);
    return () => clearInterval(id);
  }, [state.isSpeaking, patch]);

  const setPauseMs = useCallback((ms: number) => {
    setPauseMsState(ms);
    vadConfigRef.current = { ...vadConfigRef.current, endOfSpeechSilenceMs: ms };
    segmenterRef.current?.setConfig(vadConfigRef.current);
  }, []);

  const setLanguage = useCallback((code: string) => {
    // The effect keyed on `language` resolves and warms the right decoder.
    setLanguageState(findLanguage(code).code);
  }, []);

  const startPtt = useCallback(async () => {
    if (activeRef.current) return;

    patch({ status: 'INITIALIZING', error: null, liveText: '' });

    try {
      // Reset the cheap, synchronous engines first. Loading the decoder is
      // deliberately NOT awaited here: it can take seconds, and it is not
      // needed until a segment is flushed, which is at minimum minSpeechMs plus
      // the end-of-speech pause away. Awaiting it was what made the button feel
      // unresponsive on the first press.
      vadRef.current!.reset();
      segmenterRef.current!.reset();
      captureRef.current!.reset();
      await vadRef.current!.initialize();

      let synthetic = Platform.OS === 'web' || !stream;

      if (!synthetic) {
        if (!micGrantedRef.current) {
          const { granted } = await requestRecordingPermissionsAsync();
          if (!granted) {
            patch({
              status: 'ERROR',
              error: 'Microphone permission denied. Enable it in system settings.',
            });
            return;
          }
          micGrantedRef.current = true;
          await setAudioModeAsync({
            allowsRecording: true,
            playsInSilentMode: true,
          });
        }

        try {
          await stream.start();
        } catch (error) {
          // On a device that genuinely has a microphone, a failed start means
          // something else holds it — most often an in-progress phone call,
          // which Android gives exclusive access. Silently substituting
          // generated audio here would be actively misleading: the visualizer
          // would move and packets would flow while the operator's voice went
          // nowhere. Report it instead.
          console.warn('[Transmitter] microphone unavailable', error);
          patch({
            status: 'ERROR',
            error:
              'Microphone unavailable — another app is using it. ' +
              'End any ongoing call or voice recording, then try again.',
          });
          return;
        }
      }

      if (synthetic) syntheticRef.current!.start();

      // The single most useful line in the log: whether this is a real voice.
      if (synthetic) {
        console.log(
          '[Transmitter] AUDIO SOURCE = SYNTHETIC (no microphone stream on ' +
            `${Platform.OS}); the VAD is running on a generated signal.`
        );
      } else {
        console.log(
          `[Transmitter] AUDIO SOURCE = MICROPHONE | hardware ${stream.sampleRate} Hz, ` +
            `${stream.channels} ch -> pipeline ${SAMPLE_RATE} Hz mono` +
            `${stream.sampleRate !== SAMPLE_RATE ? ' (resampling)' : ''}`
        );
      }

      setUsingSyntheticAudio(synthetic);
      activeRef.current = true;
      setIsActive(true);
      patch({ status: 'LISTENING' });

      // Warm the decoder in the background so the first flush does not stall.
      void sttRef
        .current!.prepare(languageRef.current)
        .then((status) => patch({ engine: status.kind }));
    } catch (error) {
      patch({
        status: 'ERROR',
        error: error instanceof Error ? error.message : String(error),
      });
    }
  }, [patch, stream]);

  const stopPtt = useCallback(async () => {
    if (!activeRef.current) return;
    activeRef.current = false;
    setIsActive(false);

    syntheticRef.current?.stop();
    try {
      stream?.stop?.();
    } catch {
      // Already stopped.
    }

    // Manual finalization: whatever is buffered becomes one last utterance,
    // rather than being discarded because the pause never elapsed.
    segmenterRef.current?.flush();
    level.value = 0;

    // Preserve TRANSCRIBING if a decode is already in flight, so the manual
    // flush above still gets to report its result.
    setState((prev) => ({
      ...prev,
      status: prev.status === 'TRANSCRIBING' ? 'TRANSCRIBING' : 'IDLE',
      isSpeaking: false,
      utteranceMs: null,
    }));
  }, [level, patch, stream]);

  // Release native resources when the screen unmounts.
  useEffect(() => {
    return () => {
      activeRef.current = false;
      syntheticRef.current?.stop();
      try {
        stream?.stop?.();
      } catch {
        // Already stopped.
      }
      void vadRef.current?.dispose();
      void sttRef.current?.dispose();
    };
    // Intentionally mount-only: this is teardown for the whole controller.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const installModel = useCallback(async () => {
    const model = resolveModelForLanguage(languageRef.current) ?? PRIMARY_MODEL;
    setModelStatus({ state: 'downloading', percent: 0, phase: 'downloading' });
    try {
      await modelsRef.current!.install(model, (percent, phase) => {
        setModelStatus({ state: 'downloading', percent, phase });
      });

      // The provider has already concluded there is no native decoder; make it
      // re-evaluate now that one exists.
      await sttRef.current!.reset();
      const status = await sttRef.current!.prepare(languageRef.current);
      patch({ engine: status.kind, error: null });

      setModelStatus(await modelsRef.current!.status(model));
    } catch (error) {
      setModelStatus({
        state: 'error',
        message: error instanceof Error ? error.message : String(error),
      });
    }
  }, [patch]);

  const clearLog = useCallback(() => setLog([]), []);

  /** The decoder serving the currently selected language. */
  const activeModel = resolveModelForLanguage(language) ?? PRIMARY_MODEL;

  return useMemo(
    () => ({
      transcriptionState: state,
      startPtt,
      stopPtt,
      isActive,
      language,
      setLanguage,
      pauseMs,
      setPauseMs,
      log,
      clearLog,
      senderId,
      connected,
      level,
      usingSyntheticAudio,
      transport,
      modelStatus,
      installModel,
      modelLabel: activeModel.label,
      modelSizeMb: activeModel.approxMb,
    }),
    [
      state, startPtt, stopPtt, isActive, language, setLanguage, pauseMs,
      setPauseMs, log, clearLog, senderId, connected, level,
      usingSyntheticAudio, transport, modelStatus, installModel, activeModel,
    ]
  );
}

const INITIAL_STATE: TranscriptionState = {
  status: 'IDLE',
  liveText: '',
  lastResult: null,
  level: 0,
  isSpeaking: false,
  latencyMs: null,
  utteranceMs: null,
  error: null,
  engine: 'none' as SttEngineKind,
};
