package com.itantra.app.tts

import com.itantra.app.packet.PacketPriority

/** Direct port of src/core/tts/types.ts. */

/** Install state of a TTS voice on this device. Mirrors STT's ModelStatus shape. */
sealed class TtsVoiceStatus {
    object NotInstalled : TtsVoiceStatus()
    data class Downloading(val percent: Int, val phase: String) : TtsVoiceStatus()
    data class Installed(val path: String) : TtsVoiceStatus()
    data class Error(val message: String) : TtsVoiceStatus()
}

/**
 * What the UI needs to render "the receiver is speaking" — deliberately free
 * of engine/model vocabulary (ONNX, VITS, Piper, MMS), same as the source.
 */
enum class TtsPlaybackPhase(val value: String) {
    IDLE("idle"),
    LOADING_VOICE("loading-voice"),
    SPEAKING("speaking"),
    ERROR("error"),
}

data class TtsPlaybackState(
    val phase: TtsPlaybackPhase,
    /** Correlates to the request id passed into TtsManager.speakText. */
    val requestId: String?,
    /** The packet currently being spoken, or the most recently spoken one. */
    val language: String?,
    val text: String?,
    val priority: PacketPriority?,
    /** True while a CRITICAL message holds the floor. */
    val isCritical: Boolean,
    /** Set when [phase] is ERROR. A short, user-facing message — never a raw exception string. */
    val error: String?,
)

/** One request to speak a piece of received text. */
data class SpeakRequest(
    val id: String,
    val text: String,
    val language: String,
    val priority: PacketPriority,
)

val INITIAL_TTS_PLAYBACK_STATE = TtsPlaybackState(
    phase = TtsPlaybackPhase.IDLE,
    requestId = null,
    language = null,
    text = null,
    priority = null,
    isCritical = false,
    error = null,
)
