import { setAudioModeAsync } from 'expo-audio';
import * as Crypto from 'expo-crypto';

import { resolveTtsModelForLanguage } from '../../config/ttsModels';
import type { PacketPriority } from '../types';
import { TtsEngine } from './TtsEngine';
import { TtsModelManager } from './TtsModelManager';
import { TtsQueue } from './TtsQueue';
import {
  INITIAL_TTS_PLAYBACK_STATE,
  type SpeakRequest,
  type TtsPlaybackState,
  type TtsVoiceStatus,
} from './types';

type Listener = (state: TtsPlaybackState) => void;

/**
 * The public TTS interface. This is the only class the UI or the receiver
 * pipeline talks to — everything else in core/tts is an implementation
 * detail reached through here, which is what keeps this migratable to a
 * Kotlin TtsManager later without touching call sites.
 *
 * Responsibilities: resolve packet.language to a voice, load it on demand
 * (reusing an already-loaded voice for repeated messages in the same
 * language), queue normal messages, let CRITICAL messages interrupt and
 * jump the queue, and apply the right Android audio-focus behaviour for
 * each.
 */
export class TtsManager {
  private readonly models = new TtsModelManager();
  private readonly engine = new TtsEngine();
  private readonly queue = new TtsQueue();

  private state: TtsPlaybackState = INITIAL_TTS_PLAYBACK_STATE;
  private readonly listeners = new Set<Listener>();

  private draining = false;
  private current: SpeakRequest | null = null;
  private currentFinish: (() => void) | null = null;

  getState(): TtsPlaybackState {
    return this.state;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  /** Install state of the voice for `languageCode`, for the UI's per-language download affordance. */
  async voiceStatus(languageCode: string): Promise<TtsVoiceStatus | null> {
    const model = resolveTtsModelForLanguage(languageCode);
    if (!model) return null;
    return this.models.status(model);
  }

  /** Download and install the voice for `languageCode`. Safe to call twice. */
  async installVoice(
    languageCode: string,
    onProgress: (percent: number, phase: 'downloading' | 'extracting') => void
  ): Promise<void> {
    const model = resolveTtsModelForLanguage(languageCode);
    if (!model) {
      throw new Error(`No TTS voice registered for "${languageCode}"`);
    }
    await this.models.install(model, onProgress);
  }

  /**
   * Speak `text` in `language` at `priority`. Never throws — a missing
   * voice, a synthesis failure, or a playback failure all land in
   * `getState().error` instead, so one bad message cannot take down the
   * receiver pipeline.
   *
   * `requestId` is optional and additive to the mandated 3-argument
   * contract: when the caller already has a stable id for this message
   * (the receiver pipeline passes the packet's own `id`), passing it here
   * lets `getState().requestId` correlate exactly to that message instead
   * of the caller having to guess from text/language alone.
   */
  async speakText(
    text: string,
    language: string,
    priority: PacketPriority,
    requestId?: string
  ): Promise<void> {
    const request: SpeakRequest = {
      id: requestId ?? Crypto.randomUUID(),
      text,
      language,
      priority,
    };

    const accepted = this.queue.enqueue(request);
    if (!accepted) return; // duplicate packet id — ignored, per spec

    // A CRITICAL arrival while a lower-priority message is mid-playback
    // interrupts it immediately. The interrupted message is not lost: it
    // goes back to the front of its own band and will be the next thing
    // spoken once the critical message (and anything else critical) is done.
    if (
      priority === 'CRITICAL' &&
      this.current &&
      this.current.priority !== 'CRITICAL'
    ) {
      this.queue.requeueFront(this.current);
      this.engine.stopPlayback();
      this.currentFinish?.();
    }

    void this.drain();
  }

  async dispose(): Promise<void> {
    this.queue.clear();
    await this.engine.dispose();
  }

  private async drain(): Promise<void> {
    if (this.draining) return;
    this.draining = true;
    try {
      let next = this.queue.dequeue();
      while (next) {
        await this.speakOne(next);
        next = this.queue.dequeue();
      }
    } finally {
      this.draining = false;
      this.current = null;
      await resetAudioFocus();
      this.setState({ ...INITIAL_TTS_PLAYBACK_STATE });
    }
  }

  private async speakOne(request: SpeakRequest): Promise<void> {
    this.current = request;
    const isCritical = request.priority === 'CRITICAL';

    const model = resolveTtsModelForLanguage(request.language);
    if (!model) {
      this.reportError(request, 'This language is not supported for speech playback.');
      return;
    }

    const path = await this.models.resolvePath(model);
    if (!path) {
      this.reportError(
        request,
        'Language model unavailable. Install the required language pack.'
      );
      return;
    }

    this.setState({
      phase: 'loading-voice',
      requestId: request.id,
      language: request.language,
      text: request.text,
      priority: request.priority,
      isCritical,
      error: null,
    });

    try {
      await this.engine.load(model, path);
    } catch (error) {
      this.reportError(request, 'Speech engine failed to start.');
      // eslint-disable-next-line no-console
      console.warn('[TtsManager] engine.load failed:', messageOf(error));
      return;
    }

    await setAudioModeAsync({
      interruptionMode: isCritical ? 'doNotMix' : 'duckOthers',
      playsInSilentMode: true,
    });

    this.setState({
      phase: 'speaking',
      requestId: request.id,
      language: request.language,
      text: request.text,
      priority: request.priority,
      isCritical,
      error: null,
    });

    await new Promise<void>((resolve) => {
      this.currentFinish = () => {
        this.currentFinish = null;
        resolve();
      };

      this.engine
        .speak(
          request.text,
          () => this.currentFinish?.(),
          (message) => {
            this.reportError(request, 'Speech playback failed.');
            // eslint-disable-next-line no-console
            console.warn('[TtsManager] playback error:', message);
            this.currentFinish?.();
          }
        )
        .catch((error) => {
          this.reportError(request, 'Speech synthesis failed.');
          // eslint-disable-next-line no-console
          console.warn('[TtsManager] engine.speak failed:', messageOf(error));
          this.currentFinish?.();
        });
    });
  }

  private reportError(request: SpeakRequest, message: string): void {
    this.setState({
      phase: 'error',
      requestId: request.id,
      language: request.language,
      text: request.text,
      priority: request.priority,
      isCritical: request.priority === 'CRITICAL',
      error: message,
    });
  }

  private setState(state: TtsPlaybackState): void {
    this.state = state;
    for (const listener of this.listeners) listener(state);
  }
}

async function resetAudioFocus(): Promise<void> {
  try {
    await setAudioModeAsync({ interruptionMode: 'duckOthers' });
  } catch {
    // Best effort — not resetting the audio mode is not user-visible.
  }
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
