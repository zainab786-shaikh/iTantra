package com.itantra.app.device

import android.content.Context
import android.provider.Settings
import java.util.UUID

/**
 * Direct port of src/core/device/deviceId.ts (Android path only — the
 * source's iOS branch has no Android-migration equivalent to preserve).
 *
 * Stable per-installation sender ID. Prefers the OS-provided installation
 * identifier (Android's Settings.Secure.ANDROID_ID — the same value
 * expo-application's getAndroidId() reads under the hood) so the ID
 * survives app restarts. When that is unavailable, a random UUID is
 * generated for the session, which keeps packets well-formed without
 * inventing a false identity claim — same fallback reasoning as the
 * source.
 */
object DeviceId {
    private var cached: String? = null

    fun getSenderId(context: Context): String {
        cached?.let { return it }

        val raw: String? = try {
            Settings.Secure.getString(context.contentResolver, Settings.Secure.ANDROID_ID)
        } catch (e: Exception) {
            null
        }

        val result = if (!raw.isNullOrEmpty()) {
            "ITX-${short(raw)}"
        } else {
            "ITX-${short(UUID.randomUUID().toString())}"
        }
        cached = result
        return result
    }

    /** Compact, human-readable 8-char tag for display in the UI. */
    private fun short(value: String): String =
        value.replace(Regex("[^a-zA-Z0-9]"), "").take(8).uppercase()
}
