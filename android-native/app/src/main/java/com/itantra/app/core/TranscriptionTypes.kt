package com.itantra.app.core

import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority
import com.itantra.app.stt.SttEngineKind

/** Direct port of TransmitterStatus in src/core/types.ts. High-level state of the transmitter, surfaced to the UI. */
enum class TransmitterStatus(val value: String) {
    /** Engine warm, mic closed. */
    IDLE("IDLE"),
    /** Models loading. */
    INITIALIZING("INITIALIZING"),
    /** Mic open, no speech yet. */
    LISTENING("LISTENING"),
    /** VAD says speech is in progress. */
    SPEAKING("SPEAKING"),
    /** Segment flushed to STT, awaiting text. */
    TRANSCRIBING("TRANSCRIBING"),
    ERROR("ERROR"),
}

/** Direct port of TranscriptionResult in src/core/types.ts. A finalized utterance, before it is packaged into a packet. */
data class TranscriptionResult(
    val text: String,
    val language: String,
    /** Wall-clock ms from END_OF_SPEECH to decoded text. */
    val latencyMs: Long,
    /** Duration of the audio segment that produced this text, in ms. */
    val durationMs: Double,
    /** Set when the utterance was cut short by stopPtt() rather than a natural pause. */
    val forced: Boolean,
)

/** Direct port of TranscriptionState in src/core/types.ts. Everything the UI needs to render the transmitter, in one object. */
data class TranscriptionState(
    val status: TransmitterStatus,
    /** Text decoded so far for the utterance in flight (empty when idle). */
    val liveText: String,
    /** The most recent finalized transcription. */
    val lastResult: TranscriptionResult?,
    /** Smoothed 0..1 input level, for the visualizer. */
    val level: Float,
    /** True between START_OF_SPEECH and END_OF_SPEECH. */
    val isSpeaking: Boolean,
    /** Decode latency of the last finalized utterance, in ms. */
    val latencyMs: Long?,
    /** ms since the current utterance started, or null when not speaking. */
    val utteranceMs: Long?,
    val error: String?,
    /** Which STT backend actually serviced the last request. */
    val engine: SttEngineKind,
)

val INITIAL_TRANSCRIPTION_STATE = TranscriptionState(
    status = TransmitterStatus.IDLE,
    liveText = "",
    lastResult = null,
    level = 0f,
    isSpeaking = false,
    latencyMs = null,
    utteranceMs = null,
    error = null,
    engine = SttEngineKind.NONE,
)

/**
 * One entry in the transmitted-message log.
 *
 * Several fields moved here from [ITantraPacket] in Phase 0. They are all
 * facts the **sender** legitimately knows and the wire no longer carries
 * (`packet-security-transport-spec.md` §1.3): the plaintext, its size, the
 * priority and the language. Holding them on this side is what lets the
 * transmitter's log and the M-01/M-05 measurements keep working without
 * re-duplicating anything onto the link.
 */
data class LogEntry(
    val packet: ITantraPacket,
    /**
     * What the operator said.
     *
     * Held here rather than on the packet: the packet carries only the
     * encoded payload, so the plaintext lives on the side that legitimately
     * already knows it.
     */
    val text: String,
    /** UTF-8 size of [text] — M-01 (`contract §6.1`). Sender-side; never transmitted. */
    val originalBytes: Int,
    /** The priority written into the native payload. Sender-side copy. */
    val priority: PacketPriority,
    /** The sender's language. Sender-side copy. */
    val language: String,
    /** Whether the transport accepted the packet. */
    val delivered: Boolean,
    val latencyMs: Long,
    /** True when the text came from the simulated recogniser, not a real decode. */
    val simulated: Boolean,
)
