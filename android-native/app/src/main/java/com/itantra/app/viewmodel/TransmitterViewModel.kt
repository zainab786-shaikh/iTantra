package com.itantra.app.viewmodel

import android.content.Context
import android.util.Log
import com.itantra.app.audio.AudioCapture
import com.itantra.app.audio.PcmMath
import com.itantra.app.config.DEFAULT_LANGUAGE
import com.itantra.app.config.DEFAULT_VAD_CONFIG
import com.itantra.app.config.NEMO_CTC_ENGLISH
import com.itantra.app.config.SttModelDescriptor
import com.itantra.app.config.VadConfig
import com.itantra.app.config.findLanguage
import com.itantra.app.config.resolveModelForLanguage
import com.itantra.app.core.INITIAL_TRANSCRIPTION_STATE
import com.itantra.app.core.LogEntry
import com.itantra.app.core.TranscriptionResult
import com.itantra.app.core.TranscriptionState
import com.itantra.app.core.TransmitterStatus
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.buildPacket
import com.itantra.app.stt.NonSpeechFilter
import com.itantra.app.stt.SttEngineKind
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.stt.SttModelManager
import com.itantra.app.stt.SttModelStatus
import com.itantra.app.stt.checkSttModelStatus
import com.itantra.app.transport.Transport
import com.itantra.app.vad.AudioSegment
import com.itantra.app.vad.EnergyVad
import com.itantra.app.vad.SentenceSegmenter
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

private const val TAG = "TransmitterViewModel"

/**
 * Direct port of src/hooks/useTransmitterController.ts's state and
 * lifecycle, replacing React state/refs with a plain Kotlin state holder
 * exposing StateFlow (owned and lifecycle-managed by AppViewModel, the one
 * real androidx.lifecycle.ViewModel in this app - see AppViewModel.kt for
 * why). Connects the components already ported in earlier phases:
 * AudioCapture (3), EnergyVad/SentenceSegmenter (4), SttEngineProvider (5),
 * buildPacket/DeviceId (6), Transport (7). No new behavior invented beyond
 * what is required to replace the RN hook with a Kotlin equivalent.
 *
 * The source's `SyntheticAudioSource` fallback and web-only branches are
 * not ported, per Phase 3's own established reasoning: a native Android
 * app always has a real `AudioRecord`. Permission is checked, not
 * requested, here, mirroring the same split `AudioCapture.kt` already
 * documents — requesting needs an Activity, which is the UI layer's job
 * (Phase 10), not the ViewModel's.
 */
class TransmitterViewModel(
    context: Context,
    private val transport: Transport,
    maxLogEntries: Int = 40,
) {
    val transportName: String = transport.name
    private val appContext = context.applicationContext
    private val modelsRootDir = File(appContext.filesDir, "itantra-models")
    private val maxLog = maxLogEntries
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val vad = EnergyVad()
    private val sttProvider = SttEngineProvider(modelsRootDir)
    private val sttModelManager = SttModelManager(modelsRootDir)
    private var vadConfig: VadConfig = DEFAULT_VAD_CONFIG.copy(endOfSpeechSilenceMs = DEFAULT_VAD_CONFIG.endOfSpeechSilenceMs)

    private val segmenter: SentenceSegmenter = SentenceSegmenter(
        vadConfig,
        onSpeechStart = {
            speechStartedAtMs = System.currentTimeMillis()
            patch(status = TransmitterStatus.SPEAKING, isSpeaking = true)
        },
        onSegment = { segment ->
            speechStartedAtMs = null
            scope.launch { handleSegment(segment) }
        },
    )

    private val capture = AudioCapture(vadConfig.frameSize, onFrame = { frame ->
        if (isActiveFlag) {
            val probability = vad.process(frame.samples)
            _level.value = PcmMath.rmsToLevel(frame.rms)
            segmenter.push(frame, probability)
        }
    })

    private var speechStartedAtMs: Long? = null
    @Volatile private var isActiveFlag = false

    private val _transcriptionState = MutableStateFlow(INITIAL_TRANSCRIPTION_STATE)
    val transcriptionState: StateFlow<TranscriptionState> = _transcriptionState.asStateFlow()

    private val _isActive = MutableStateFlow(false)
    val isActive: StateFlow<Boolean> = _isActive.asStateFlow()

    private val _language = MutableStateFlow(DEFAULT_LANGUAGE.code)
    val language: StateFlow<String> = _language.asStateFlow()

    private val _pauseMs = MutableStateFlow(DEFAULT_VAD_CONFIG.endOfSpeechSilenceMs)
    val pauseMs: StateFlow<Int> = _pauseMs.asStateFlow()

    private val _log = MutableStateFlow<List<LogEntry>>(emptyList())
    val log: StateFlow<List<LogEntry>> = _log.asStateFlow()

    private val _senderId = MutableStateFlow(DeviceId.getSenderId(appContext))
    val senderId: StateFlow<String> = _senderId.asStateFlow()

    private val _connected = MutableStateFlow(transport.isConnected())
    val connected: StateFlow<Boolean> = _connected.asStateFlow()

    private val _level = MutableStateFlow(0f)
    val level: StateFlow<Float> = _level.asStateFlow()

    private val _modelStatus = MutableStateFlow<SttModelStatus>(SttModelStatus.NotInstalled)
    val modelStatus: StateFlow<SttModelStatus> = _modelStatus.asStateFlow()

    /** The decoder serving the currently selected language. Mirrors `activeModel` in useTransmitterController.ts. */
    val activeModel: SttModelDescriptor
        get() = resolveModelForLanguage(_language.value) ?: NEMO_CTC_ENGLISH

    private val unsubscribeConnection = transport.onConnectionChange { _connected.value = it }

    init {
        // Mirrors the source's useEffect keyed on `language`: report whether
        // the selected language's decoder is on disk and, if so, warm it.
        scope.launch { resolveLanguageModel(_language.value) }
    }

    private fun patch(
        status: TransmitterStatus = _transcriptionState.value.status,
        liveText: String = _transcriptionState.value.liveText,
        lastResult: TranscriptionResult? = _transcriptionState.value.lastResult,
        level: Float = _transcriptionState.value.level,
        isSpeaking: Boolean = _transcriptionState.value.isSpeaking,
        latencyMs: Long? = _transcriptionState.value.latencyMs,
        utteranceMs: Long? = _transcriptionState.value.utteranceMs,
        error: String? = _transcriptionState.value.error,
        engine: SttEngineKind = _transcriptionState.value.engine,
    ) {
        _transcriptionState.value = TranscriptionState(
            status = status,
            liveText = liveText,
            lastResult = lastResult,
            level = level,
            isSpeaking = isSpeaking,
            latencyMs = latencyMs,
            utteranceMs = utteranceMs,
            error = error,
            engine = engine,
        )
    }

    private suspend fun resolveLanguageModel(languageCode: String) {
        val descriptor = resolveModelForLanguage(languageCode) ?: NEMO_CTC_ENGLISH
        _modelStatus.value = checkSttModelStatus(modelsRootDir, descriptor)
        val status = sttProvider.prepare(languageCode)
        patch(engine = status.kind)
    }

    fun setLanguage(code: String) {
        val resolved = findLanguage(code).code
        _language.value = resolved
        scope.launch { resolveLanguageModel(resolved) }
    }

    fun setPauseMs(ms: Int) {
        _pauseMs.value = ms
        vadConfig = vadConfig.copy(endOfSpeechSilenceMs = ms)
        segmenter.setConfig(vadConfig)
    }

    fun clearLog() {
        _log.value = emptyList()
    }

    /**
     * Surfaces the same denial message the source shows when
     * requestRecordingPermissionsAsync() resolves to `granted: false` inside
     * startPtt(). On Android, requesting a runtime permission is an
     * Activity-level operation the ViewModel cannot itself await mid-gesture
     * (unlike RN's single async startPtt()), so the UI layer requests the
     * permission and reports the outcome back here - see
     * MainActivity.kt's permission launcher callback.
     */
    fun reportMicPermissionDenied() {
        patch(
            status = TransmitterStatus.ERROR,
            error = "Microphone permission denied. Enable it in system settings.",
        )
    }

    /**
     * Direct port of useTransmitterController.ts's installModel(): download
     * (and, for the English archive, extract) the decoder for the current
     * language, then re-evaluate the STT provider exactly as the source
     * does - `sttRef.current!.reset()` followed by `prepare()`, so a
     * language that had fallen back to the placeholder decoder picks up
     * the newly-installed model immediately.
     */
    fun installModel() {
        val model = resolveModelForLanguage(_language.value) ?: NEMO_CTC_ENGLISH
        _modelStatus.value = SttModelStatus.Downloading(0, "downloading")
        scope.launch {
            try {
                withContext(Dispatchers.IO) {
                    sttModelManager.install(model) { percent, phase ->
                        _modelStatus.value = SttModelStatus.Downloading(percent, phase)
                    }
                }

                // The provider has already concluded there is no native
                // decoder; make it re-evaluate now that one exists.
                sttProvider.reset()
                val status = sttProvider.prepare(_language.value)
                patch(engine = status.kind, error = null)

                _modelStatus.value = checkSttModelStatus(modelsRootDir, model)
            } catch (e: Exception) {
                _modelStatus.value = SttModelStatus.Error(e.message ?: e.javaClass.simpleName)
            }
        }
    }

    /** Begin capture; VAD then segments speech automatically. */
    fun startPtt(context: Context) {
        if (isActiveFlag) return
        patch(status = TransmitterStatus.INITIALIZING, error = null, liveText = "")

        vad.reset()
        segmenter.reset()
        vad.initialize()

        if (!AudioCapture.hasPermission(context)) {
            patch(
                status = TransmitterStatus.ERROR,
                error = "Microphone permission denied. Enable it in system settings.",
            )
            return
        }

        try {
            capture.start(context)
        } catch (e: Exception) {
            // On a device that genuinely has a microphone, a failed start means
            // something else holds it - most often an in-progress phone call,
            // which Android gives exclusive access. Matches the source: the
            // real exception is logged for diagnostics, but the user always
            // sees the same fixed message regardless of the specific internal
            // reason (mirrors useTransmitterController.ts's single fixed
            // error string here, not e.message).
            Log.w(TAG, "microphone unavailable", e)
            patch(
                status = TransmitterStatus.ERROR,
                error = "Microphone unavailable — another app is using it. " +
                    "End any ongoing call or voice recording, then try again.",
            )
            return
        }

        isActiveFlag = true
        _isActive.value = true
        patch(status = TransmitterStatus.LISTENING)

        // Warm the decoder in the background so the first flush does not stall.
        scope.launch {
            val status = sttProvider.prepare(_language.value)
            patch(engine = status.kind)
        }
    }

    /** Finalize the utterance in flight and stop capture. */
    fun stopPtt() {
        if (!isActiveFlag) return
        isActiveFlag = false
        _isActive.value = false

        capture.stop()
        // Manual finalization: whatever is buffered becomes one last
        // utterance, rather than being discarded because the pause never
        // elapsed.
        segmenter.flush()
        _level.value = 0f

        // Preserve TRANSCRIBING if a decode is already in flight, so the
        // manual flush above still gets to report its result.
        val current = _transcriptionState.value
        patch(
            status = if (current.status == TransmitterStatus.TRANSCRIBING) TransmitterStatus.TRANSCRIBING else TransmitterStatus.IDLE,
            isSpeaking = false,
            utteranceMs = null,
        )
    }

    /** Decode a finalized segment, package it, and hand it to the transport. */
    private suspend fun handleSegment(segment: AudioSegment) {
        val startedAt = System.currentTimeMillis()
        patch(status = TransmitterStatus.TRANSCRIBING, isSpeaking = false)

        val languageCode = _language.value
        try {
            val transcription = sttProvider.transcribe(segment.samples, languageCode)
            val latencyMs = System.currentTimeMillis() - startedAt
            val text = transcription.text

            if (text.isEmpty() || NonSpeechFilter.isNonSpeechArtifact(text)) {
                patch(
                    status = if (isActiveFlag) TransmitterStatus.LISTENING else TransmitterStatus.IDLE,
                    liveText = "",
                )
                return
            }

            val result = TranscriptionResult(
                text = text,
                language = languageCode,
                latencyMs = latencyMs,
                durationMs = segment.durationMs,
                forced = segment.forced,
            )

            val packet = buildPacket(text = text, language = languageCode, senderId = _senderId.value)
            val engineKind = sttProvider.status.kind
            val delivered = transport.sendPacket(packet)

            _log.value = (listOf(
                LogEntry(
                    packet = packet,
                    delivered = delivered,
                    latencyMs = latencyMs,
                    simulated = engineKind == SttEngineKind.SIMULATED,
                )
            ) + _log.value).take(maxLog)

            patch(
                status = if (isActiveFlag) TransmitterStatus.LISTENING else TransmitterStatus.IDLE,
                liveText = "",
                lastResult = result,
                latencyMs = latencyMs,
                engine = engineKind,
                isSpeaking = false,
                error = null,
            )
        } catch (e: Exception) {
            patch(status = TransmitterStatus.ERROR, error = e.message ?: e.javaClass.simpleName)
        }
    }

    fun dispose() {
        isActiveFlag = false
        scope.cancel()
        capture.release()
        vad.dispose()
        sttProvider.dispose()
        unsubscribeConnection()
    }
}
