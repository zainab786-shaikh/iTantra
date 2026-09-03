import type { iTantraPacket } from '../types';
import type { Transport } from './Transport';

/**
 * Logging stand-in for the real transport.
 *
 * Models the two behaviours the UI has to cope with — non-zero send latency and
 * occasional failure — so that retry and error states are exercised before the
 * radio layer exists. Swap it out by passing a different {@link Transport} to
 * the controller hook; nothing else needs to change.
 */
export class MockTransport implements Transport {
  readonly name = 'mock://itantra-loopback';

  private connected = true;
  private readonly listeners = new Set<(connected: boolean) => void>();
  private readonly receiveListeners = new Set<(packet: iTantraPacket) => void>();
  /** Every packet handed to this transport, newest last. */
  readonly sent: iTantraPacket[] = [];

  /** Fraction of sends that fail, to exercise the UI's error path. */
  private readonly failureRate: number;

  constructor(options: { failureRate?: number } = {}) {
    this.failureRate = options.failureRate ?? 0;
  }

  async sendPacket(packet: iTantraPacket): Promise<boolean> {
    // Stand in for radio round-trip time.
    await new Promise((resolve) => setTimeout(resolve, 60 + Math.random() * 90));

    if (!this.connected || Math.random() < this.failureRate) {
      console.warn('[MockTransport] send failed', packet.id);
      return false;
    }

    this.sent.push(packet);
    console.log(
      `[MockTransport] -> ${packet.priority} ${packet.language} "${packet.text}" (${packet.id})`
    );

    // No real P2P transport exists yet (a separate, later workstream), so
    // this loops a successfully "sent" packet back to this same device's
    // receive listeners after a short delay — standing in for a peer
    // receiving it, so the receiver pipeline (TTS, receiver UI) has
    // something real to exercise end-to-end on one device.
    setTimeout(() => {
      for (const listener of this.receiveListeners) listener(packet);
    }, 120 + Math.random() * 180);

    return true;
  }

  onPacketReceived(listener: (packet: iTantraPacket) => void): () => void {
    this.receiveListeners.add(listener);
    return () => this.receiveListeners.delete(listener);
  }

  /** Test/demo affordance: inject a packet as if it arrived from a peer, without a real send. */
  simulateReceive(packet: iTantraPacket): void {
    for (const listener of this.receiveListeners) listener(packet);
  }

  isConnected(): boolean {
    return this.connected;
  }

  /** Test/demo affordance: flip the link and notify subscribers. */
  setConnected(connected: boolean): void {
    if (this.connected === connected) return;
    this.connected = connected;
    for (const listener of this.listeners) listener(connected);
  }

  onConnectionChange(listener: (connected: boolean) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
