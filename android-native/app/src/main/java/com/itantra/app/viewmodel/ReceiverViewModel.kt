package com.itantra.app.viewmodel

import android.content.Context
import android.util.Log
import com.itantra.app.config.DEFAULT_LANGUAGE
import com.itantra.app.config.findLanguage
import com.itantra.app.device.CriticalAlert
import com.itantra.app.native.NativeEngine
import com.itantra.app.native.NativeReceiveResult
import com.itantra.app.native.ReceiveStatus
import com.itantra.app.native.SLOT_NAMES
import com.itantra.app.native.appLanguageOf
import com.itantra.app.packet.ITantraPacket
import com.itantra.app.packet.PacketPriority
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

private const val TAG = "ReceiverViewModel"
private const val MAX_HISTORY = 60

/**
 * Direct port of src/hooks/useReceiverController.ts's state and lifecycle.
 *
 * Owns the receiver pipeline: listens for incoming packets on [transport],
 * hands each native payload to the native receive pipeline (Phase 11), then
 * hands what decoded to a TtsManager and tracks per-message playback state for
 * the receiver UI. Mirrors TransmitterViewModel's shape (a plain state holder
 * owning long-lived engines, exposing StateFlow, both lifecycle-managed by
 * AppViewModel) so the two screens read as one consistent architecture.
 */
class ReceiverViewModel(
    context: Context,
    private val transport: Transport,
    /** The phone's native engine (Phase 11). Null only if the library or its packs failed to load. */
    private val engine: NativeEngine?,
) {
    private val appContext = context.applicationContext
    val transportName: String = transport.name
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

    /** Test hook only (Phase 12 pair conformance): every native receive result, emitted or not, as it arrives. */
    @Volatile internal var onNativeResult: ((NativeReceiveResult) -> Unit)? = null

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

        val receiverLanguage = _language.value
        val engine = engine
        if (engine == null || !engine.supports(receiverLanguage)) {
            addRow(
                undecodable(
                    packet,
                    if (engine == null) {
                        "The native engine did not load, so this message cannot be decoded."
                    } else {
                        "No language pack for ${findLanguage(receiverLanguage).label} yet — this message cannot be decoded."
                    },
                )
            )
            return
        }

        // `receiver §1.1`: the outer frame is unwrapped; the native payload goes
        // down whole and the §7 output interface comes back. Nothing here reads
        // tier, priority or language out of the payload itself (`packet §1.1`).
        val result = try {
            engine.receive(receiverLanguage, packet.payload)
        } catch (e: Exception) {
            Log.w(TAG, "native receive refused ${packet.id}", e)
            addRow(undecodable(packet, "This message could not be decoded."))
            return
        }
        Log.i(
            TAG,
            "native receive ${packet.id}: ${result.outcome}, status ${result.status}, tier ${result.mode}, " +
                "priority ${result.priority}, counter ${result.counter}, committed ${result.contextCommitted}",
        )

        onNativeResult?.invoke(result)

        // `receiver §7`, §9: an authentication failure leaves nothing readable
        // and a replay is discarded silently. Neither is output.
        if (!result.emit) return

        val priority = if (result.priority == 1) PacketPriority.CRITICAL else PacketPriority.NORMAL

        // Fired on arrival, before speech is attempted, and regardless of
        // whether it succeeds. A CRITICAL that cannot be spoken - no voice pack,
        // audio focus lost to a call - must still be felt. Restored from the
        // prototype: priority is known again, read from the authenticated
        // payload (`receiver §7.4`).
        if (priority == PacketPriority.CRITICAL) {
            CriticalAlert.vibrate(appContext)
        }

        if (result.status != ReceiveStatus.OK) {
            addRow(failedDelivery(packet, result, priority, receiverLanguage))
            return
        }

        // `receiver §7.2`: the voice follows the output's language id — the
        // receiver's own for Tier 1, the sender's for Tier 2 — never this
        // phone's setting alone.
        val textLanguage = appLanguageOf(result.languageCode) ?: receiverLanguage
        val text = String(result.text, Charsets.UTF_8)

        addRow(
            ReceivedMessage(
                packet = packet,
                text = text,
                textLanguage = textLanguage,
                priority = priority,
                state = ReceivedMessageState.RECEIVED,
                error = null,
                receivedAt = System.currentTimeMillis(),
                tier = tierOf(result),
            )
        )

        correlation[packet.id] = packet.id
        ttsManager.speakText(text, textLanguage, priority, packet.id)

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

    /**
     * An authentic packet that did not deliver a message: context mismatch,
     * integrity failure or render failure (`receiver §7`).
     *
     * `receiver §7.1`, C-31: no text, and unresolved slots appear by NAME only.
     * Nothing is spoken.
     */
    private fun failedDelivery(
        packet: ITantraPacket,
        result: NativeReceiveResult,
        priority: PacketPriority,
        receiverLanguage: String,
    ): ReceivedMessage {
        val unresolved = result.unresolved.map { SLOT_NAMES.getOrElse(it) { "SLOT $it" } }
        val error = when (result.status) {
            ReceiveStatus.CONTEXT_MISMATCH ->
                if (unresolved.isNotEmpty()) {
                    "Context out of step — not delivered. Unresolved: ${unresolved.joinToString()}."
                } else {
                    "Context out of step — this message needs to be resent without context."
                }
            ReceiveStatus.INTEGRITY_FAIL -> "Integrity check failed — message rejected."
            ReceiveStatus.RENDER_FAIL ->
                "Received, but it cannot be rendered in ${findLanguage(receiverLanguage).label}."
            else -> "This message could not be decoded."
        }
        return ReceivedMessage(
            packet = packet,
            text = "",
            textLanguage = appLanguageOf(result.languageCode) ?: receiverLanguage,
            priority = priority,
            state = ReceivedMessageState.ERROR,
            error = error,
            receivedAt = System.currentTimeMillis(),
            tier = tierOf(result),
            unresolved = unresolved,
        )
    }

    private fun undecodable(packet: ITantraPacket, error: String) = ReceivedMessage(
        packet = packet,
        text = "",
        textLanguage = _language.value,
        priority = PacketPriority.NORMAL,
        state = ReceivedMessageState.ERROR,
        error = error,
        receivedAt = System.currentTimeMillis(),
    )

    private fun tierOf(result: NativeReceiveResult): Int = if (result.mode == 1 || result.mode == 2) result.mode else 0

    private fun addRow(message: ReceivedMessage) {
        _messages.value = (listOf(message) + _messages.value).let {
            if (it.size > MAX_HISTORY) it.subList(0, MAX_HISTORY) else it
        }
    }

    fun clearHistory() {
        _messages.value = emptyList()
    }

    /** Re-speak a previously received message, in its original language. */
    fun replay(packetId: String) {
        val entry = _messages.value.find { it.packet.id == packetId } ?: return
        // Nothing decoded, nothing to say — and an unresolved row must never be
        // voiced (`receiver §7.1`).
        if (entry.text.isEmpty()) return

        val replayId = "$packetId::replay::${System.currentTimeMillis()}"
        correlation[replayId] = packetId
        // Reads the already-decoded row rather than decoding again, so a
        // replay can never disagree with what was originally spoken.
        ttsManager.speakText(entry.text, entry.textLanguage, entry.priority, replayId)

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
