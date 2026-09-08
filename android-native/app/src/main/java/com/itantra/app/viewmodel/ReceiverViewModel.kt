package com.itantra.app.viewmodel

import android.content.Context
import com.itantra.app.codec.ITantraCodec
import com.itantra.app.config.DEFAULT_LANGUAGE
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.receiver.ReceivedMessage
import com.itantra.app.receiver.ReceivedMessageState
import com.itantra.app.transport.Transport
import com.itantra.app.tts.INITIAL_TTS_PLAYBACK_STATE
import com.itantra.app.tts.TtsManager
import com.itantra.app.tts.TtsPlaybackPhase
import com.itantra.app.tts.TtsPlaybackState
import com.itantra.app.tts.TtsVoiceStatus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

private const val MAX_HISTORY = 60

/**
 * Direct port of src/hooks/useReceiverController.ts's state and lifecycle.
 *
 * Owns the receiver pipeline: listens for incoming packets on [transport],
 * hands each one to a TtsManager (Phase 8), and tracks per-message
 * playback state for the receiver UI. Mirrors TransmitterViewModel's shape
 * (a plain state holder owning long-lived engines, exposing StateFlow, both
 * lifecycle-managed by AppViewModel) so the two screens read as one
 * consistent architecture, same as the source's own "mirrors
 * useTransmitterController's shape" comment.
 */
class ReceiverViewModel(
    context: Context,
    private val transport: Transport,
) {
    private val appContext = context.applicationContext
    val transportName: String = transport.name
    /** Bytes -> text. The only thing that turns an arriving payload into words. */
    private val codec = ITantraCodec()
    private val ttsManager = TtsManager(appContext, File(appContext.filesDir, "itantra-tts-models"))

    /** Maps a TtsManager request id back to the history row it belongs to - identity for normal messages, a synthetic id for replays. */
    private val correlation = mutableMapOf<String, String>()

    private val _messages = MutableStateFlow<List<ReceivedMessage>>(emptyList())
    val messages: StateFlow<List<ReceivedMessage>> = _messages.asStateFlow()

    private val _ttsState = MutableStateFlow(INITIAL_TTS_PLAYBACK_STATE)
    val ttsState: StateFlow<TtsPlaybackState> = _ttsState.asStateFlow()

    private val _language = MutableStateFlow(DEFAULT_LANGUAGE.code)
    val language: StateFlow<String> = _language.asStateFlow()

    private val _connected = MutableStateFlow(transport.isConnected())
    val connected: StateFlow<Boolean> = _connected.asStateFlow()

    fun setLanguage(code: String) {
        _language.value = code
    }

    private val unsubscribeConnection = transport.onConnectionChange { _connected.value = it }
    private val unsubscribeTts = ttsManager.subscribe { state -> onTtsState(state) }
    private val unsubscribePackets = transport.onPacketReceived { packet -> handlePacket(packet) }

    private fun onTtsState(state: TtsPlaybackState) {
        _ttsState.value = state

        val rowId = state.requestId?.let { correlation[it] ?: it }

        val prev = _messages.value
        var changed = false
        val next = prev.map { m ->
            if (rowId != null && m.packet.id == rowId) {
                changed = true
                if (state.phase == TtsPlaybackPhase.ERROR) {
                    m.copy(state = ReceivedMessageState.ERROR, error = state.error)
                } else {
                    m.copy(state = ReceivedMessageState.SPEAKING, error = null)
                }
            } else if (m.state == ReceivedMessageState.SPEAKING && m.packet.id != rowId) {
                // Anything previously "speaking" that is no longer the
                // active request has finished - the manager moved on (or
                // went idle).
                changed = true
                m.copy(state = ReceivedMessageState.SPOKEN)
            } else {
                m
            }
        }
        if (changed) _messages.value = next
    }

    private fun handlePacket(packet: ITantraPacket) {
        // Dedup here as well as in the speech queue, and on the same key, so
        // the two can never disagree. Previously only the queue deduped, so a
        // repeated id produced a visible row that was never spoken - the
        // screen said one thing and the speaker did another.
        if (_messages.value.any { it.packet.id == packet.id }) return

        // Decoded once, here. Everything downstream - the log row, the
        // language chip, the voice - reads these two values rather than
        // re-deriving them, which is the whole reason decode() returns the
        // language alongside the text (see codec.DecodedText).
        val decoded = try {
            codec.decode(packet.mode, packet.payload, packet.language, _language.value)
        } catch (e: Exception) {
            // A payload this build cannot decode is not spoken and not
            // guessed at; it is shown as a failed message.
            val failed = ReceivedMessage(
                packet = packet,
                text = "",
                textLanguage = packet.language,
                state = ReceivedMessageState.ERROR,
                error = "This message could not be decoded.",
                receivedAt = System.currentTimeMillis(),
            )
            _messages.value = (listOf(failed) + _messages.value).let {
                if (it.size > MAX_HISTORY) it.subList(0, MAX_HISTORY) else it
            }
            return
        }

        val entry = ReceivedMessage(
            packet = packet,
            text = decoded.text,
            textLanguage = decoded.languageCode,
            state = ReceivedMessageState.RECEIVED,
            error = null,
            receivedAt = System.currentTimeMillis(),
        )
        val withNew = (listOf(entry) + _messages.value).let {
            if (it.size > MAX_HISTORY) it.subList(0, MAX_HISTORY) else it
        }
        _messages.value = withNew

        correlation[packet.id] = packet.id
        // decoded.languageCode, NOT packet.language: a PHRASE message was
        // rendered in this device's own language and must be spoken in this
        // device's own voice.
        ttsManager.speakText(decoded.text, decoded.languageCode, packet.priority, packet.id)

        // Reflect "handed to the queue" promptly; the subscription above
        // takes over from here once the manager actually starts on it.
        _messages.value = _messages.value.map { m ->
            if (m.packet.id == packet.id && m.state == ReceivedMessageState.RECEIVED) {
                m.copy(state = ReceivedMessageState.QUEUED)
            } else {
                m
            }
        }
    }

    fun clearHistory() {
        _messages.value = emptyList()
    }

    /** Re-speak a previously received message, in its original language. */
    fun replay(packetId: String) {
        val entry = _messages.value.find { it.packet.id == packetId } ?: return

        val replayId = "$packetId::replay::${System.currentTimeMillis()}"
        correlation[replayId] = packetId
        // Reads the already-decoded row rather than decoding again, so a
        // replay can never disagree with what was originally spoken.
        ttsManager.speakText(entry.text, entry.textLanguage, entry.packet.priority, replayId)

        _messages.value = _messages.value.map { m ->
            if (m.packet.id == packetId) m.copy(state = ReceivedMessageState.QUEUED, error = null) else m
        }
    }

    /** Install state of the voice for [languageCode], for the UI's per-language download affordance. */
    fun voiceStatus(languageCode: String): TtsVoiceStatus? = ttsManager.voiceStatus(languageCode)

    /** Download and install the voice for [languageCode]. Safe to call twice (matches the source). */
    suspend fun installVoice(languageCode: String, onProgress: (percent: Int, phase: String) -> Unit) {
        ttsManager.installVoice(languageCode, onProgress)
    }

    fun dispose() {
        unsubscribeConnection()
        unsubscribeTts()
        unsubscribePackets()
        ttsManager.dispose()
    }
}
