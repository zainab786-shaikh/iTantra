import * as Crypto from 'expo-crypto';

import type { iTantraPacket, PacketPriority } from '../types';
import { classifyPriority } from './priority';

export interface BuildPacketInput {
  text: string;
  language: string;
  senderId: string;
  /** Override the keyword-derived band. */
  priority?: PacketPriority;
  /** Set when the transport layer has applied payload compression. */
  isCompressed?: boolean;
}

/**
 * Build a wire packet from a finalized utterance.
 *
 * `isCompressed` defaults to false and is a declaration about the payload, not
 * a request: the transmitter does not compress, so claiming otherwise here
 * would make the receiver attempt a decompression that fails. The transport
 * module sets it when it actually compresses.
 */
export function buildPacket(input: BuildPacketInput): iTantraPacket {
  return {
    id: Crypto.randomUUID(),
    senderId: input.senderId,
    timestamp: Date.now(),
    language: input.language,
    text: input.text,
    priority: input.priority ?? classifyPriority(input.text),
    isCompressed: input.isCompressed ?? false,
  };
}
