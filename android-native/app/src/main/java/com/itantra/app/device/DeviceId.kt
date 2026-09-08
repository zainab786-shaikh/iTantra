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

    /**
     * The 16-bit form of this device's identity, for the radio frame.
     *
     * [getSenderId] returns a 12-character string; spending 12 bytes of a
     * packet on it is not defensible on a link where the whole payload can be
     * 2 bytes, so the wire carries this instead (see transport/PacketCodec.kt)
     * and the receiver renders it back as [nodeLabel].
     *
     * Derived deterministically from the sender ID, so it is stable across
     * app restarts exactly as far as the sender ID itself is. A 16-bit space
     * collides after a few hundred devices by the birthday bound; that is
     * fine for a point-to-point link between two known nodes, and is *not*
     * a claim of global uniqueness.
     */
    fun getNodeId(context: Context): Short {
        val id = getSenderId(context)
        // FNV-1a, 32-bit, folded to 16. Any stable hash would do; this one is
        // three lines and has no dependencies.
        var hash = -0x7ee3623b // 0x811C9DC5
        for (ch in id) {
            hash = hash xor ch.code
            hash *= 0x01000193
        }
        val folded = ((hash ushr 16) xor hash) and 0xFFFF
        // 0 is reserved as "unknown node", so never hand it out.
        return (if (folded == 0) 1 else folded).toShort()
    }

    /** How a node id is shown to the operator, on both the sending and receiving side. */
    fun nodeLabel(nodeId: Short): String = "NODE-%04X".format(nodeId.toInt() and 0xFFFF)

    /** Compact, human-readable 8-char tag for display in the UI. */
    private fun short(value: String): String =
        value.replace(Regex("[^a-zA-Z0-9]"), "").take(8).uppercase()
}
