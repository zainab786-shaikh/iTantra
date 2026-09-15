package com.itantra.app.tts

import android.content.Context
import android.media.AudioManager
import android.util.Log
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.packet.PacketPriority
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import java.io.File
import java.util.UUID

private const val TAG = "TtsManager"

typealias TtsStateListener = (TtsPlaybackState) -> Unit

/**
 * Direct port of src/core/tts/TtsManager.ts.
 *
 * The public TTS interface: resolves packet.language to a voice, loads it
 * on demand (reusing an already-loaded voice for repeated messages in the
 * same language, via TtsEngine's own loadedModelId check), queues normal
 * messages, lets CRITICAL messages interrupt and jump the queue, and
 * applies Android audio-focus behaviour for each.
 *
 * Android's audio-focus model differs from Expo's cross-platform
 * `setAudioModeAsync({interruptionMode})` abstraction, so the mapping here
 * is a direct, documented judgment call rather than an exact API mirror
 * (same treatment Phase 3 gave `AudioSource.MIC`): the source's
 * `'doNotMix'` (critical, exclusive) maps to
 * `AUDIOFOCUS_GAIN_TRANSIENT` (other apps pause, not just duck), and
 * `'duckOthers'` (normal) maps to `AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK`
 * (other apps duck). `resetAudioFocus()` re-requests the ducking mode
 * afterward, mirroring the source's own best-effort reset-to-duckOthers
 * (not an abandon).
 */
class TtsManager(context: Context, ttsModelsRoot: File) {
    private val appContext = context.applicationContext
    private val cacheDir = appContext.cacheDir
    private val audioManager = appContext.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    @Suppress("DEPRECATION")
    private val focusChangeListener = AudioManager.OnAudioFocusChangeListener { }

    private val models = TtsModelManager(ttsModelsRoot)
    private val engine = TtsEngine()
    private val queue = TtsQueue()

    private var state: TtsPlaybackState = INITIAL_TTS_PLAYBACK_STATE
    private val listeners = mutableSetOf<TtsStateListener>()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var draining = false
    private var current: SpeakRequest? = null
    private var currentFinish: (() -> Unit)? = null

    /**
     * Media volume as the operator had it, saved when a CRITICAL forces the
     * stream to maximum so it can be put back afterwards.
     *
     * Null means "not currently boosted". Only the first critical in a burst
     * saves it, or a second one would record the already-maxed level as the
     * value to restore and the operator would never get their setting back.
     */
    private var volumeBeforeBoost: Int? = null

    fun getState(): TtsPlaybackState = state

    fun subscribe(listener: TtsStateListener): () -> Unit {
        listeners.add(listener)
        return { listeners.remove(listener) }
    }

    /** Install state of the voice for [languageCode], for the UI's per-language download affordance. */
    fun voiceStatus(languageCode: String): TtsVoiceStatus? {
        val model = resolveTtsModelForLanguage(languageCode) ?: return null
        return models.status(model)
    }

    /**
     * Direct port of installVoice() in src/core/tts/TtsManager.ts: download
     * and install the voice for [languageCode], reporting progress the
     * same way [speakText]'s caller expects (0-100 and a phase). Throws if
     * no voice is registered for the language, matching the source's exact
     * error message.
     */
    suspend fun installVoice(languageCode: String, onProgress: (percent: Int, phase: String) -> Unit) {
        val model = resolveTtsModelForLanguage(languageCode)
            ?: throw IllegalArgumentException("No TTS voice registered for \"$languageCode\"")
        withContext(Dispatchers.IO) {
            models.install(model, onProgress)
        }
    }

    /**
     * Speak [text] in [language] at [priority]. Never throws — a missing
     * voice, a synthesis failure, or a playback failure all land in
     * `getState().error` instead, so one bad message cannot take down the
     * receiver pipeline.
     */
    fun speakText(
        text: String,
        language: String,
        priority: PacketPriority,
        requestId: String? = null,
    ): Boolean {
        val request = SpeakRequest(
            id = requestId ?: UUID.randomUUID().toString(),
            text = text,
            language = language,
            priority = priority,
        )

        val accepted = queue.enqueue(request)
        if (!accepted) {
            // Logged, not silent. This return is why a message can arrive,
            // appear on screen, and never be spoken - if it ever happens
            // again, it should be findable in one logcat line rather than by
            // reading the queue's source.
            Log.w(TAG, "duplicate request ${request.id}; not queued for speech")
            return false
        }

        // A CRITICAL arrival while a lower-priority message is mid-playback
        // interrupts it immediately. The interrupted message is not lost:
        // it goes back to the front of its own band and will be the next
        // thing spoken once the critical message (and anything else
        // critical) is done.
        val runningCurrent = current
        if (priority == PacketPriority.CRITICAL && runningCurrent != null && runningCurrent.priority != PacketPriority.CRITICAL) {
            queue.requeueFront(runningCurrent)
            engine.stopPlayback()
            currentFinish?.invoke()
        }

        scope.launch { drain() }
        return true
    }

    fun dispose() {
        scope.cancel()
        queue.clear()
        // The scope is cancelled, so drain()'s finally may never run - put
        // the operator's volume back here rather than leaving the device
        // pinned at maximum after the app closes.
        restoreVolume()
        engine.dispose()
    }

    private suspend fun drain() {
        if (draining) return
        draining = true
        try {
            var next = queue.dequeue()
            while (next != null) {
                speakOne(next)
                next = queue.dequeue()
            }
        } finally {
            draining = false
            current = null
            // Restored once the queue is empty rather than after each
            // message, so a run of criticals does not flap the system volume
            // up and down between them.
            restoreVolume()
            resetAudioFocus()
            setState(INITIAL_TTS_PLAYBACK_STATE)
        }
    }

    private suspend fun speakOne(request: SpeakRequest) {
        current = request
        val isCritical = request.priority == PacketPriority.CRITICAL

        val model = resolveTtsModelForLanguage(request.language)
        if (model == null) {
            reportError(request, "This language is not supported for speech playback.")
            return
        }

        val path = models.resolvePath(model)
        if (path == null) {
            reportError(request, "Language model unavailable. Install the required language pack.")
            return
        }

        setState(
            TtsPlaybackState(
                phase = TtsPlaybackPhase.LOADING_VOICE,
                requestId = request.id,
                language = request.language,
                text = request.text,
                priority = request.priority,
                isCritical = isCritical,
                error = null,
            )
        )

        try {
            engine.load(model, path)
        } catch (e: Exception) {
            reportError(request, "Speech engine failed to start.")
            return
        }

        requestAudioFocus(isCritical)
        if (isCritical) boostVolume() else {
            restoreVolume()
            raiseIfSilent()
        }
        unmuteForPlayback()

        setState(
            TtsPlaybackState(
                phase = TtsPlaybackPhase.SPEAKING,
                requestId = request.id,
                language = request.language,
                text = request.text,
                priority = request.priority,
                isCritical = isCritical,
                error = null,
            )
        )

        suspendCancellableCoroutine<Unit> { cont ->
            currentFinish = {
                currentFinish = null
                if (cont.isActive) cont.resumeWith(Result.success(Unit))
            }

            try {
                engine.speak(
                    text = request.text,
                    cacheDir = cacheDir,
                    critical = isCritical,
                    onFinished = { currentFinish?.invoke() },
                    onPlaybackError = { message ->
                        reportError(request, "Speech playback failed.")
                        currentFinish?.invoke()
                    },
                )
            } catch (e: Exception) {
                reportError(request, "Speech synthesis failed.")
                currentFinish?.invoke()
            }
        }
    }

    private fun reportError(request: SpeakRequest, message: String) {
        setState(
            TtsPlaybackState(
                phase = TtsPlaybackPhase.ERROR,
                requestId = request.id,
                language = request.language,
                text = request.text,
                priority = request.priority,
                isCritical = request.priority == PacketPriority.CRITICAL,
                error = message,
            )
        )
    }

    private fun setState(next: TtsPlaybackState) {
        state = next
        for (listener in listeners.toList()) listener(next)
    }

    /**
     * Raise the media stream to maximum for a CRITICAL message.
     *
     * This is the loudest Android will allow without hijacking another
     * stream: the alarm channel would be louder still, but it is not the
     * stream this manager holds focus on, and routing an emergency message
     * somewhere the operator's media controls cannot reach is a worse
     * failure than being one notch quieter.
     *
     * [restoreVolume] always puts the operator's setting back.
     */
    private fun boostVolume() {
        if (volumeBeforeBoost != null) return
        try {
            val stream = AudioManager.STREAM_MUSIC
            val current = audioManager.getStreamVolume(stream)
            val max = audioManager.getStreamMaxVolume(stream)
            if (current >= max) return
            Log.i(TAG, "critical: media volume $current -> $max (will be restored)")
            volumeBeforeBoost = current
            audioManager.setStreamVolume(stream, max, 0)
        } catch (e: SecurityException) {
            Log.w(TAG, "cannot raise volume (Do Not Disturb?)", e)
            // Changing volume is refused while Do Not Disturb is active
            // unless the app holds notification-policy access, which this
            // app does not ask for. The message still plays, just at the
            // operator's own level.
            volumeBeforeBoost = null
        } catch (e: Exception) {
            Log.w(TAG, "cannot raise volume", e)
            volumeBeforeBoost = null
        }
    }

    /** True while this manager has unmuted the media stream for a received message. */
    private var unmutedForPlayback = false

    /**
     * A received message is played even when the media stream is muted: the
     * operator is not holding the phone, and a message that completes playback
     * in silence is lost. Muted again by [restoreVolume] once the queue is empty.
     */
    private fun unmuteForPlayback() {
        try {
            if (audioManager.isStreamMute(AudioManager.STREAM_MUSIC)) {
                audioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_UNMUTE, 0)
                unmutedForPlayback = true
                Log.i(TAG, "media stream was muted: unmuted for playback (will be restored)")
            }
        } catch (e: Exception) {
            Log.w(TAG, "cannot unmute media stream", e)
        }
    }

    /** A NORMAL message on a stream at volume 0 plays at half volume, restored afterwards. */
    private fun raiseIfSilent() {
        if (volumeBeforeBoost != null) return
        try {
            val stream = AudioManager.STREAM_MUSIC
            if (audioManager.getStreamVolume(stream) > 0) return
            volumeBeforeBoost = 0
            audioManager.setStreamVolume(stream, audioManager.getStreamMaxVolume(stream) / 2, 0)
            Log.i(TAG, "media volume 0: raised for playback (will be restored)")
        } catch (e: Exception) {
            Log.w(TAG, "cannot raise volume", e)
            volumeBeforeBoost = null
        }
    }

    /** Put the operator's media volume back. Safe to call when nothing was boosted. */
    private fun restoreVolume() {
        if (unmutedForPlayback) {
            unmutedForPlayback = false
            try {
                audioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_MUTE, 0)
            } catch (e: Exception) {
                // Best effort.
            }
        }
        val previous = volumeBeforeBoost ?: return
        volumeBeforeBoost = null
        try {
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, previous, 0)
            Log.i(TAG, "media volume restored to ${audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)}")
        } catch (e: Exception) {
            // Best effort; leaving it loud is preferable to crashing here.
        }
    }

    @Suppress("DEPRECATION")
    private fun requestAudioFocus(critical: Boolean) {
        val durationHint = if (critical) {
            AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
        } else {
            AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK
        }
        try {
            audioManager.requestAudioFocus(focusChangeListener, AudioManager.STREAM_MUSIC, durationHint)
        } catch (e: Exception) {
            // Best effort - not obtaining focus is not user-visible enough to fail the request over.
        }
    }

    @Suppress("DEPRECATION")
    private fun resetAudioFocus() {
        try {
            audioManager.requestAudioFocus(
                focusChangeListener,
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK,
            )
        } catch (e: Exception) {
            // Best effort - not resetting the audio mode is not user-visible.
        }
    }
}
