package com.itantra.app.stt

import com.itantra.app.config.SttModelDescriptor
import java.io.File

/**
 * Minimal, side-load-only mirror of ModelStatus in src/core/stt/ModelManager.ts.
 * Only the two states reachable without a real HTTP download path exist -
 * `downloading`/`error`/`unsupported` are not represented since this
 * migration has no model-install networking (see SttEngineProvider /
 * SherpaSttBackend's side-load-only resolution, Phase 2/5).
 */
sealed class SttModelStatus {
    object NotInstalled : SttModelStatus()
    data class Installed(val path: String) : SttModelStatus()
}

/** Side-load presence check, same directory convention SherpaSttBackend.load() already uses. */
fun checkSttModelStatus(modelsRoot: File, descriptor: SttModelDescriptor): SttModelStatus {
    val dir = File(modelsRoot, descriptor.id)
    val hasOnnx = dir.listFiles()?.any { it.name.endsWith(".onnx") } == true
    return if (dir.exists() && hasOnnx) SttModelStatus.Installed(dir.absolutePath) else SttModelStatus.NotInstalled
}
