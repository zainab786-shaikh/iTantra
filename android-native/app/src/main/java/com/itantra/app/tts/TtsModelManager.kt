package com.itantra.app.tts

import com.itantra.app.config.TtsModelDescriptor
import com.itantra.app.config.TtsModelSource
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Direct port of src/core/tts/TtsModelManager.ts.
 *
 * Sibling of STT's ModelManager/SttModelManager (Phase 2/5, and the later
 * STT model-download fix): a separate on-disk directory
 * (`itantra-tts-models`, matching the source's `TTS_SIDELOAD_DIR`) so the
 * TTS workstream cannot touch STT's model state even by accident — STT is
 * frozen for this task.
 *
 * Piper and MMS voices are packaged differently, exactly as the source's
 * own doc comment states (verified again here, not assumed): Piper ships
 * as a `.tar.bz2` archive from the k2-fsa/sherpa-onnx `tts-models` release;
 * the MMS conversions this app uses ship as loose files (`model.onnx` +
 * `tokens.txt`) in a Hugging Face repo, no archive. `install()` branches on
 * `model.source` to handle both, matching the source's branch on
 * `descriptor.source.kind`.
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

    /**
     * Download (and, for Piper, extract) a voice into `root/<model.id>/`.
     * Resolves to the install path, or throws with a readable message.
     * [onProgress] receives 0-100 and a phase ("downloading" | "extracting"),
     * matching the source's callback shape. Runs blocking network/file I/O -
     * callers must dispatch this off the main thread.
     */
    fun install(model: TtsModelDescriptor, onProgress: (percent: Int, phase: String) -> Unit): String {
        root.mkdirs()
        val finalDir = File(root, model.id)

        when (val source = model.source) {
            is TtsModelSource.Archive -> installArchive(model, source.url, finalDir, onProgress)
            is TtsModelSource.Files -> installLooseFiles(source, finalDir, onProgress)
        }

        if (!dirHasOnnx(finalDir)) {
            throw IllegalStateException("Install finished but ${model.id} is not in the expected location")
        }

        cachedPaths[model.id] = finalDir.absolutePath
        return finalDir.absolutePath
    }

    private fun installArchive(
        model: TtsModelDescriptor,
        url: String,
        finalDir: File,
        onProgress: (percent: Int, phase: String) -> Unit,
    ) {
        val archiveFile = File(root, "${model.id}.tar.bz2")
        try {
            downloadToFile(url, archiveFile) { bytesWritten, contentLength ->
                if (contentLength > 0) {
                    onProgress(((bytesWritten.toDouble() / contentLength) * 100).roundToInt(), "downloading")
                }
            }
            onProgress(100, "extracting")
            extractTarBz2(archiveFile, root)
            if (!finalDir.exists()) {
                throw IllegalStateException("Extraction finished but ${model.id} is not in the expected location")
            }
        } finally {
            if (archiveFile.exists()) archiveFile.delete()
        }
    }

    /**
     * Sizes vary a lot between files (model.onnx is ~64-114 MB, tokens.txt
     * is a few hundred bytes) - weight overall progress by each file's
     * actual byte share rather than by file count, matching the source's
     * own comment ("so the bar doesn't jump to ~90% the instant tokens.txt
     * finishes").
     */
    private fun installLooseFiles(
        source: TtsModelSource.Files,
        finalDir: File,
        onProgress: (percent: Int, phase: String) -> Unit,
    ) {
        finalDir.mkdirs()
        val fileTotals = mutableMapOf<String, Long>()
        var completedBytes = 0L

        for (file in source.files) {
            val dest = File(finalDir, file)
            downloadToFile("${source.baseUrl}$file", dest) { bytesWritten, contentLength ->
                if (contentLength > 0) {
                    fileTotals[file] = contentLength
                    val totalBytes = fileTotals.values.sum()
                    val percent = if (totalBytes > 0) {
                        (((completedBytes + bytesWritten).toDouble() / totalBytes) * 100).roundToInt()
                    } else {
                        0
                    }
                    onProgress(min(percent, 99), "downloading")
                }
            }
            completedBytes += fileTotals[file] ?: 0L
        }
        onProgress(100, "downloading")
    }

    private fun downloadToFile(
        url: String,
        dest: File,
        onProgress: (bytesWritten: Long, contentLength: Long) -> Unit,
    ) {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.instanceFollowRedirects = true
        connection.connectTimeout = 15_000
        connection.readTimeout = 15_000
        try {
            connection.connect()
            if (connection.responseCode != HttpURLConnection.HTTP_OK) {
                throw IllegalStateException("Download failed with HTTP ${connection.responseCode}")
            }
            val contentLength = connection.contentLengthLong
            var bytesWritten = 0L
            var lastReportAt = 0L
            connection.inputStream.use { input ->
                FileOutputStream(dest).use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val read = input.read(buffer)
                        if (read == -1) break
                        output.write(buffer, 0, read)
                        bytesWritten += read
                        val now = System.currentTimeMillis()
                        if (now - lastReportAt >= 400) {
                            onProgress(bytesWritten, contentLength)
                            lastReportAt = now
                        }
                    }
                }
            }
            onProgress(bytesWritten, contentLength)
        } finally {
            connection.disconnect()
        }
    }

    private fun extractTarBz2(archiveFile: File, targetDir: File) {
        BufferedInputStream(archiveFile.inputStream()).use { fileStream ->
            BZip2CompressorInputStream(fileStream).use { bzStream ->
                TarArchiveInputStream(bzStream).use { tarStream ->
                    var entry: TarArchiveEntry? = tarStream.nextEntry as TarArchiveEntry?
                    while (entry != null) {
                        val outFile = File(targetDir, entry.name)
                        if (entry.isDirectory) {
                            outFile.mkdirs()
                        } else {
                            outFile.parentFile?.mkdirs()
                            FileOutputStream(outFile).use { out -> tarStream.copyTo(out) }
                        }
                        entry = tarStream.nextEntry as TarArchiveEntry?
                    }
                }
            }
        }
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
