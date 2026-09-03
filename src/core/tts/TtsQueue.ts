import type { SpeakRequest } from './types';

/**
 * Ordering for received messages waiting to be spoken.
 *
 * Rules (from the PS spec): NORMAL/MEDIUM/HIGH messages play in arrival
 * order; a CRITICAL message jumps ahead of everything queued and, if
 * something is already playing, the caller (TtsManager) interrupts it —
 * this class only owns ordering, not playback, so it stays trivially
 * testable and reusable if the app moves to native Kotlin later.
 */
export class TtsQueue {
  private normal: SpeakRequest[] = [];
  private critical: SpeakRequest[] = [];
  private readonly seen = new Set<string>();

  /** @returns false if `request.id` was already enqueued or spoken (duplicate packet). */
  enqueue(request: SpeakRequest): boolean {
    if (this.seen.has(request.id)) return false;
    this.seen.add(request.id);

    if (request.priority === 'CRITICAL') {
      this.critical.push(request);
    } else {
      this.normal.push(request);
    }
    return true;
  }

  /** True if a CRITICAL message is waiting — callers use this to decide whether to interrupt current playback. */
  hasPendingCritical(): boolean {
    return this.critical.length > 0;
  }

  get length(): number {
    return this.critical.length + this.normal.length;
  }

  /** Pop the next request to speak: critical messages always win. */
  dequeue(): SpeakRequest | null {
    if (this.critical.length > 0) return this.critical.shift()!;
    if (this.normal.length > 0) return this.normal.shift()!;
    return null;
  }

  /**
   * Put an in-flight normal-band request back at the front of its band,
   * because a CRITICAL message just interrupted it. Bypasses the duplicate
   * check — this is the same message being restored, not a new arrival.
   */
  requeueFront(request: SpeakRequest): void {
    if (request.priority === 'CRITICAL') {
      this.critical.unshift(request);
    } else {
      this.normal.unshift(request);
    }
  }

  clear(): void {
    this.normal = [];
    this.critical = [];
    // `seen` is intentionally not cleared: a cleared queue should not let a
    // duplicate of an already-delivered packet back in.
  }
}
