package com.itantra.app.packet

/**
 * Direct port of PacketPriority in src/core/types.ts. Priority band
 * carried on the wire. Higher bands may pre-empt lower ones downstream
 * (see tts/TtsQueue, Phase 8).
 *
 * The string value matches the TS union's literal exactly, so anything
 * that needs the wire-format string (logging, future serialization) gets
 * the same text the RN app used, not an enum name that happens to look
 * similar.
 */
enum class PacketPriority(val value: String) {
    NORMAL("NORMAL"),
    MEDIUM("MEDIUM"),
    HIGH("HIGH"),
    CRITICAL("CRITICAL"),
}

/**
 * Direct port of iTantraPacket in src/core/types.ts. This is the single
 * contract the transport module consumes; nothing else about the engine
 * is public. Field set, names, and types are unchanged from the source.
 */
data class ITantraPacket(
    /** UUID v4. */
    val id: String,
    /** Device/User unique ID. */
    val senderId: String,
    val timestamp: Long,
    val language: String,
    val text: String,
    val priority: PacketPriority,
    val isCompressed: Boolean,
)
