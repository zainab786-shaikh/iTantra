import type { iTantraPacket } from '../types';

/**
 * The seam between the transmitter and whatever carries its packets — mesh
 * radio, BLE, LoRa, sockets.
 *
 * The transmitter knows nothing beyond this interface, so the real transport
 * can be dropped in by swapping the instance passed to
 * `useTransmitterController({ transport })`.
 */
export interface Transport {
  readonly name: string;
  /** @returns true when the packet was handed off successfully. */
  sendPacket(packet: iTantraPacket): Promise<boolean>;
  /** Whether the link is currently usable. Drives the connection badge. */
  isConnected(): boolean;
  /** Subscribe to link state changes. @returns an unsubscribe function. */
  onConnectionChange(listener: (connected: boolean) => void): () => void;
}
