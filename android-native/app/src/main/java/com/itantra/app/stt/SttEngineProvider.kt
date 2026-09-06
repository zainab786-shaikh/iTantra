package com.itantra.app.stt

import android.util.Log
import java.io.File

private const val TAG = "SttEngineProvider"

/** Direct port of SttProviderStatus in src/core/stt/SttEngineProvider.ts. */
data class SttProviderStatus(
    val kind: SttEngineKind,
    /** Why the real decoder is not in use, when it is not. */
    val reason: String?,
)

/**
 * Direct port of src/core/stt/SttEngineProvider.ts.
 *
 * Chooses and owns the active recogniser. Resolution order is
 * sherpa-onnx if its model directory for the requested language is
 * present, otherwise the placeholder backend (PlaceholderSttBackend).
 * The decision is made once per language and remembered, so a missing
 * model does not cost a failed load attempt on every utterance.
 */
class SttEngineProvider(private val modelsRootDir: File) {
    private var sherpa: SherpaSttBackend? = null
    private val placeholder = PlaceholderSttBackend()
    private var active: SttBackend = placeholder
    private var reason: String? = null

    /** Languages already known to have no working native decoder. */
    private val unsupported = mutableSetOf<String>()

    val status: SttProviderStatus
        get() = SttProviderStatus(active.kind, reason)

    /**
     * Prepare a decoder for [languageCode].
     * Never throws: a failure downgrades to the placeholder backend and
     * is reported through [status].
     */
    @Synchronized
    fun prepare(languageCode: String): SttProviderStatus {
        if (unsupported.contains(languageCode)) {
            active = placeholder
            return status
        }

        try {
            val backend = sherpa ?: SherpaSttBackend(modelsRootDir).also { sherpa = it }
            backend.load(languageCode)
            active = backend
            reason = null
        } catch (e: Exception) {
            unsupported.add(languageCode)
            active = placeholder
            reason = e.message ?: e.javaClass.simpleName
            // Loud on purpose: this is the moment a language silently
            // loses its real decoder, and swallowing it made a
            // path-cache bug look like a model bug (see MIGRATION_AUDIT.md).
            Log.w(TAG, "no native decoder for \"$languageCode\", falling back to the placeholder: $reason")
        }
        return status
    }

    /**
     * Decode one utterance. Falls back mid-flight if the native decoder
     * throws, so a runtime model failure still produces text instead of
     * a dead end.
     */
    @Synchronized
    fun transcribe(samples: FloatArray, languageCode: String): SttTranscription {
        return try {
            active.transcribe(samples, languageCode)
        } catch (e: Exception) {
            if (active.kind != SttEngineKind.SIMULATED) {
                unsupported.add(languageCode)
                reason = e.message ?: e.javaClass.simpleName
                active = placeholder
                Log.w(TAG, "decode failed for \"$languageCode\": $reason")
                placeholder.transcribe(samples, languageCode)
            } else {
                throw e
            }
        }
    }

    /**
     * Forget every cached "this language has no decoder" verdict. Call
     * after installing a model, or the provider keeps using the
     * placeholder for the rest of the session even though a real decoder
     * now exists.
     */
    @Synchronized
    fun reset() {
        unsupported.clear()
        reason = null
        active = placeholder

        val previous = sherpa
        sherpa = null
        try {
            previous?.dispose()
        } catch (_: Exception) {
            // Best effort — a failed teardown must not block the retry.
        }
    }

    @Synchronized
    fun dispose() {
        sherpa?.dispose()
        placeholder.dispose()
        sherpa = null
        active = placeholder
    }
}
