package com.itantra.app.device

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log

private const val TAG = "CriticalAlert"

/**
 * The physical half of a CRITICAL alert: a distinct vibration the operator
 * feels without looking at the screen.
 *
 * Deliberately separate from the speech path. A critical message must
 * announce itself even when the voice cannot play — no voice pack installed,
 * audio focus lost to a call, the handset face-down in a pocket. Tying the
 * alert to successful TTS would make the loudest failure mode silent.
 */
object CriticalAlert {

    /**
     * Three long pulses. Long and evenly spaced so it is distinguishable by
     * feel from a notification buzz, which is typically one short tick.
     *
     * Leading 0 is the initial delay, then alternating on/off durations.
     */
    private val PATTERN = longArrayOf(0, 400, 180, 400, 180, 400)

    /** Full strength for every pulse; this is the one message that should not be subtle. */
    private val AMPLITUDES = intArrayOf(0, 255, 0, 255, 0, 255)

    /**
     * Vibrate for a received CRITICAL message. Never throws — an alert that
     * crashed the receive pipeline would be worse than one that did not fire.
     */
    fun vibrate(context: Context) {
        try {
            val vibrator = resolveVibrator(context) ?: return
            if (!vibrator.hasVibrator()) return

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val effect = if (vibrator.hasAmplitudeControl()) {
                    VibrationEffect.createWaveform(PATTERN, AMPLITUDES, -1)
                } else {
                    VibrationEffect.createWaveform(PATTERN, -1)
                }
                vibrator.vibrate(effect)
            } else {
                @Suppress("DEPRECATION")
                vibrator.vibrate(PATTERN, -1)
            }
        } catch (e: Exception) {
            Log.w(TAG, "vibration unavailable", e)
        }
    }

    private fun resolveVibrator(context: Context): Vibrator? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val manager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
            manager?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        }
}
