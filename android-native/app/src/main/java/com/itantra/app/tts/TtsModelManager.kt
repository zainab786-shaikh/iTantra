package com.itantra.app.tts

import com.itantra.app.config.TtsModelDescriptor
import java.io.File

/**
 * Direct port of src/core/tts/TtsModelManager.ts, side-load-resolution only.
 *
 * Sibling of STT's ModelManager/SherpaSttBackend side-load check (Phase 2/5):
 * a separate on-disk directory (`itantra-tts-models`, matching the source's
 * `TTS_SIDELOAD_DIR`) so the TTS workstream cannot touch STT's model state.
 *
 * The source's `install()` HTTP download/extraction path (archive download
 * for Piper, loose-file download for MMS) is NOT ported here, per this
 * migration's explicit no-real-networking scope and the same precedent set
 * for STT's ModelManager in Phase 2/5 — models must be side-loaded onto the
 * device for this migration to exercise them.
 */
class TtsModelManager(private val root: File) {
    private val cachedPaths = mutableMapOf<String, String>()

    /** Where the voice is installed, or null if it is not. */
    fun resolvePath(model: TtsModelDescriptor): String? {
        cachedPaths[model.id]?.let { return it }

        val dir = File(root, model.id)
        if (!dir.exists() || !dirHasOnnx(dir)) return null

        cachedPaths[model.id] = dir.absolutePath
        return dir.absolutePath
    }

    /** Current install state, without starting anything. */
    fun status(model: TtsModelDescriptor): TtsVoiceStatus {
        val path = resolvePath(model)
        return if (path != null) TtsVoiceStatus.Installed(path) else TtsVoiceStatus.NotInstalled
    }

    /** Forget cached paths, e.g. after a delete. */
    fun invalidate() {
        cachedPaths.clear()
    }

    private fun dirHasOnnx(dir: File): Boolean =
        dir.listFiles()?.any { it.name.endsWith(".onnx") } == true
}

/** Directory under the app's files dir where TTS voices are installed. Matches the source's TTS_SIDELOAD_DIR. */
const val TTS_SIDELOAD_DIR = "itantra-tts-models"
