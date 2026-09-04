import Constants, { ExecutionEnvironment } from 'expo-constants';
import { createAudioPlayer, type AudioPlayer } from 'expo-audio';

import type { TtsModelDescriptor } from '../../config/ttsModels';
import { hasSherpaOnnx, tryRequireFs } from '../nativeModules';

export interface SpeakResult {
  synthesisMs: number;
  audioDurationMs: number;
}

/**
 * One loaded native TTS voice, plus its playback.
 *
 * Mirrors SherpaSttBackend's shape on the STT side: a thin wrapper around
 * the native engine's create/use/destroy lifecycle, with exactly one voice
 * resident at a time. `speak()` synthesizes to a WAV file (via the
 * package's own `saveAudioToFile`, not a hand-rolled WAV writer) and plays
 * it with expo-audio — the same audio library the mic side already depends
 * on, so this adds no new audio dependency.
 */
export class TtsEngine {
  private native: any = null;
  private player: AudioPlayer | null = null;
  private tempWavPath: string | null = null;
  private loadedModelId: string | null = null;

  get isReady(): boolean {
    return this.native !== null;
  }

  get loadedFor(): string | null {
    return this.loadedModelId;
  }

  /** Load (or swap to) the voice at `modelPath`. Disposes any previously loaded voice first. */
  async load(model: TtsModelDescriptor, modelPath: string): Promise<void> {
    if (this.loadedModelId === model.id && this.native) return;

    const { createTTS } = requireSherpaTts();
    await this.disposeNative();

    this.native = await createTTS({
      modelPath: { type: 'file', path: modelPath },
      modelType: model.modelType,
      numThreads: 2,
    });
    this.loadedModelId = model.id;
  }

  /**
   * Synthesize `text` and play it. Resolves once playback has actually
   * started (not finished) — callers that need to know when speech ends
   * should use `onFinished`.
   */
  async speak(
    text: string,
    onFinished: () => void,
    onPlaybackError: (message: string) => void
  ): Promise<SpeakResult> {
    if (!this.native) throw new Error('No TTS voice loaded');

    const synthStart = Date.now();
    const audio = await this.native.generateSpeech(text);
    const synthesisMs = Date.now() - synthStart;
    const audioDurationMs = audio.samples.length
      ? (audio.samples.length / audio.sampleRate) * 1000
      : 0;

    const fs = tryRequireFs();
    if (!fs) throw new Error('Filesystem module unavailable in this build.');

    await this.cleanupTempFile();
    const wavPath = `${fs.DocumentDirectoryPath}/tts-playback-${Date.now()}.wav`;
    await saveAudioToFile(audio, wavPath);
    this.tempWavPath = wavPath;

    this.player?.remove();
    const player = createAudioPlayer({ uri: wavPath });
    this.player = player;

    const subscription = player.addListener('playbackStatusUpdate', (status) => {
      if (status.didJustFinish) {
        subscription.remove();
        onFinished();
      }
    });

    try {
      player.play();
    } catch (error) {
      subscription.remove();
      onPlaybackError(messageOf(error));
    }

    return { synthesisMs, audioDurationMs };
  }

  /** Stop playback immediately, without disposing the loaded voice. Used for critical interruption. */
  stopPlayback(): void {
    try {
      this.player?.pause();
    } catch {
      // Best effort.
    }
  }

  async dispose(): Promise<void> {
    await this.disposeNative();
    await this.cleanupTempFile();
  }

  private async disposeNative(): Promise<void> {
    this.player?.remove();
    this.player = null;
    const current = this.native;
    this.native = null;
    this.loadedModelId = null;
    try {
      await current?.destroy?.();
    } catch {
      // Best effort; a failed teardown must not block the next load.
    }
  }

  private async cleanupTempFile(): Promise<void> {
    if (!this.tempWavPath) return;
    const path = this.tempWavPath;
    this.tempWavPath = null;
    try {
      const fs = tryRequireFs();
      if (fs && (await fs.exists(path))) await fs.unlink(path);
    } catch {
      // Non-fatal: a leftover temp WAV costs space, not correctness.
    }
  }
}

function requireSherpaTts(): { createTTS: (options: any) => Promise<any> } {
  if (!hasSherpaOnnx()) {
    throw new Error(
      'Expo Go cannot load react-native-sherpa-onnx (it ships native libraries). ' +
        'Run `npx expo run:android` for a development build to enable speech playback.'
    );
  }
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const mod = require('react-native-sherpa-onnx/tts');
    if (typeof mod?.createTTS !== 'function') {
      throw new Error('createTTS missing from react-native-sherpa-onnx/tts');
    }
    return mod;
  } catch (error) {
    throw new Error(`react-native-sherpa-onnx TTS unavailable: ${String(error)}`);
  }
}

function saveAudioToFile(audio: any, filePath: string): Promise<string> {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const mod = require('react-native-sherpa-onnx/tts');
  return mod.saveAudioToFile(audio, filePath);
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
