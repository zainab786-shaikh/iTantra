package com.itantra.app.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.core.content.ContextCompat
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * Native audio capture using android.media.AudioRecord — the direct
 * replacement for expo-audio's useAudioStream() call in
 * useTransmitterController.ts.
 *
 * Responsibilities, matching the phase brief exactly: microphone
 * permission (checked, not requested — requesting needs an Activity and is
 * MainActivity's job, same split of responsibility as the RN controller
 * requesting permission before calling stream.start()), AudioRecord
 * lifecycle, buffer management, frame extraction (delegated to
 * AudioCaptureService), start, stop, release.
 *
 * Deliberately holds no VAD/segmentation logic (that is Phase 4) and no
 * synthetic-audio fallback: SyntheticAudioSource.ts in the RN app existed
 * only to cover expo-audio's web/Expo Go gap, where no real microphone
 * stream exists at all. A native Android app always has a real
 * AudioRecord, so there is nothing for a synthetic source to stand in
 * for — see MIGRATION_AUDIT.md §C.
 */
class AudioCapture(
    private val frameSize: Int = DEFAULT_FRAME_SIZE,
    private val onFrame: (AudioFrame) -> Unit,
) {
    private var audioRecord: AudioRecord? = null
    private var readThread: Thread? = null
    private val running = AtomicBoolean(false)
    private val captureService = AudioCaptureService(frameSize, onFrame)

    val isCapturing: Boolean get() = running.get()

    companion object {
        fun hasPermission(context: Context): Boolean =
            ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
                PackageManager.PERMISSION_GRANTED
    }

    /**
     * Start capturing 16 kHz mono PCM-16. Throws IllegalStateException if
     * RECORD_AUDIO is not granted, or if AudioRecord fails to initialize or
     * start — mirroring the RN controller's explicit "microphone
     * unavailable" error path (most commonly another app, e.g. an
     * in-progress call, holding exclusive access) rather than silently
     * continuing. See useTransmitterController.ts's startPtt().
     */
    fun start(context: Context) {
        check(hasPermission(context)) { "RECORD_AUDIO permission not granted" }
        if (running.get()) return

        captureService.reset()

        val minBufferSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        check(minBufferSize > 0) { "AudioRecord.getMinBufferSize failed for this device/sample rate" }

        // AudioSource.MIC: the plain, unprocessed microphone input. Chosen
        // over VOICE_RECOGNITION/VOICE_COMMUNICATION, both of which apply
        // device-side signal processing (AGC/noise suppression/echo
        // cancellation) that could shift audio dynamics — expo-audio's own
        // internal source configuration is not independently inspectable
        // from this repo, so the plainest, least-presumptuous option was
        // chosen rather than guessing at a "better" one.
        val record = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            minBufferSize * 2,
        )

        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            throw IllegalStateException(
                "Microphone unavailable — another app may be using it, or the device refused this configuration."
            )
        }

        audioRecord = record
        running.set(true)

        try {
            record.startRecording()
        } catch (e: Exception) {
            running.set(false)
            record.release()
            audioRecord = null
            throw IllegalStateException("Microphone unavailable — startRecording() failed.", e)
        }

        if (record.recordingState != AudioRecord.RECORDSTATE_RECORDING) {
            running.set(false)
            record.release()
            audioRecord = null
            throw IllegalStateException(
                "Microphone unavailable — another app is using it. End any ongoing call or voice recording, then try again."
            )
        }

        readThread = thread(name = "AudioCapture-read") {
            val buffer = ShortArray(frameSize * 4)
            while (running.get()) {
                val read = record.read(buffer, 0, buffer.size)
                if (read > 0) {
                    captureService.pushBuffer(buffer, read, SAMPLE_RATE)
                }
            }
        }
    }

    /**
     * Stop capturing. Safe to call when not capturing.
     *
     * Order matters: AudioRecord.stop() is called before joining the read
     * thread, because the read thread is typically blocked inside
     * AudioRecord.read() — stopping the record is what unblocks that call
     * so the thread can actually observe `running == false` and exit.
     * Joining first would risk waiting the full timeout for no reason.
     * This mirrors the RN controller's own documented teardown-ordering
     * concern (stop feeding -> halt the native stream) even though the
     * underlying objects are different.
     */
    fun stop() {
        if (!running.get()) return
        running.set(false)
        try {
            audioRecord?.stop()
        } catch (_: Exception) {
            // Already stopped by the platform.
        }
        readThread?.join(500)
        readThread = null
    }

    /** Release native resources. Call after stop(); safe to call more than once. */
    fun release() {
        stop()
        audioRecord?.release()
        audioRecord = null
    }
}
