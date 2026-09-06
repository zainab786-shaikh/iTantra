package com.itantra.app.packet

import java.util.UUID

/**
 * Direct port of src/core/packet/packetFactory.ts.
 *
 * Build a wire packet from a finalized utterance.
 *
 * [isCompressed] defaults to false and is a declaration about the
 * payload, not a request: the transmitter does not compress, so claiming
 * otherwise here would make the receiver attempt a decompression that
 * fails. The transport module would set it if it ever actually
 * compressed — nothing in this migration does, per explicit instruction.
 */
fun buildPacket(
    text: String,
    language: String,
    senderId: String,
    /** Override the keyword-derived band. */
    priority: PacketPriority? = null,
    /** Set when the transport layer has applied payload compression. */
    isCompressed: Boolean = false,
): ITantraPacket = ITantraPacket(
    id = UUID.randomUUID().toString(),
    senderId = senderId,
    timestamp = System.currentTimeMillis(),
    language = language,
    text = text,
    priority = priority ?: classifyPriority(text),
    isCompressed = isCompressed,
)
