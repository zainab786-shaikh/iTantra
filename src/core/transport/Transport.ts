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
  /**
   * Subscribe to packets arriving from the far end. @returns an unsubscribe
   * function. The real P2P transport (BLE/Nearby Connections) is a separate,
   * later workstream — this seam exists now so the receiver pipeline has
   * something concrete to consume in the meantime.
   */
  onPacketReceived(listener: (packet: iTantraPacket) => void): () => void;
}
