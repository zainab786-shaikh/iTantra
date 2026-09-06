package com.itantra.app.stt

import com.itantra.app.config.SttModelDescriptor
import java.io.File

/**
 * Direct port of ModelStatus in src/core/stt/ModelManager.ts, minus the
 * `unsupported` state: that branch only ever triggered in Expo Go / an
 * unlinked RN build, where the sherpa-onnx native module could be absent.
 * A native Kotlin build always has it linked, so that state is
 * unreachable here and is not represented.
 */
sealed class SttModelStatus {
    object NotInstalled : SttModelStatus()
    data class Downloading(val percent: Int, val phase: String) : SttModelStatus()
    data class Installed(val path: String) : SttModelStatus()
    data class Error(val message: String) : SttModelStatus()
}

/**
 * Side-load / already-downloaded presence check. Same directory convention
 * SherpaSttBackend.load() already uses, and — matching the source exactly —
 * the same directory both a hand-installed model and a downloaded one live
 * in (see SttModelManager.kt); there is no separate "download cache" vs
 * "sideload" location.
 */
fun checkSttModelStatus(modelsRoot: File, descriptor: SttModelDescriptor): SttModelStatus {
    val dir = File(modelsRoot, descriptor.id)
    val hasOnnx = dir.listFiles()?.any { it.name.endsWith(".onnx") } == true
    return if (dir.exists() && hasOnnx) SttModelStatus.Installed(dir.absolutePath) else SttModelStatus.NotInstalled
}
