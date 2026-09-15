package com.itantra.app.stt

import com.itantra.app.config.SttModelDescriptor
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlin.math.roundToInt

private const val STT_RELEASE_BASE = "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"

/**
 * Direct port of the download+extract half of src/core/stt/ModelManager.ts's
 * install() (the resolvePath()/status() half already existed as
 * checkSttModelStatus() in SttModelStatus.kt, since the source uses the
 * exact same on-disk directory for both a hand-side-loaded model and a
 * downloaded one - "install" just means "put a valid model in that
 * directory", by whatever means).
 *
 * Two shapes, matching the source's own branch on `model.downloadFiles`
 * exactly:
 *  - Indic languages: loose files (tokens.txt + model.int8.onnx),
 *    downloaded directly into `<modelsRoot>/<model.id>/`.
 *  - English (no downloadFiles): a single `.tar.bz2` release archive from
 *    k2-fsa/sherpa-onnx, downloaded then extracted. The archive's own
 *    top-level directory is already named after the model (the source's
 *    own comment notes this), so extracting into modelsRoot yields
 *    exactly `<modelsRoot>/<model.id>/`.
 *
 * The source's own downloader (`@dr.pogodin/react-native-fs`) and
 * extractor (`react-native-sherpa-onnx/extraction`) are RN native modules
 * with no equivalent here; this uses plain `HttpURLConnection` streaming
 * (no third-party HTTP client needed for a simple GET-to-file) and Apache
 * Commons Compress for tar+bzip2 (the one small library addition this fix
 * needs, replacing what was previously a native module call).
 */
class SttModelManager(private val modelsRoot: File) {

    /**
     * Download (and, for archive-shaped models, extract) the decoder into
     * `modelsRoot/<model.id>/`. Resolves to the install path, or throws
     * with a readable message. [onProgress] receives 0-100 and a phase
     * ("downloading" | "extracting"), matching the source's callback
     * shape exactly. Runs blocking network/file I/O - callers must
     * dispatch this off the main thread.
     */
    fun install(model: SttModelDescriptor, onProgress: (percent: Int, phase: String) -> Unit): String {
        modelsRoot.mkdirs()
        val finalDir = File(modelsRoot, model.id)

        val files = model.downloadFiles
        if (!files.isNullOrEmpty()) {
            finalDir.mkdirs()
            val totalFiles = files.size
            for ((index, file) in files.withIndex()) {
                val dest = File(finalDir, file.filename)
                // Resume support: a file that already downloaded fully in a
                // previous attempt is not re-fetched, matching the source's
                // own `if (await fs.exists(dest)) { ... continue }` check.
                if (dest.exists() && dest.length() > 0) continue
                downloadToFile(file.url, dest) { bytesWritten, contentLength ->
                    if (contentLength > 0) {
                        val filePct = bytesWritten.toDouble() / contentLength
                        val totalPct = (((index + filePct) / totalFiles) * 100).roundToInt()
                        onProgress(totalPct, "downloading")
                    }
                }
            }
            onProgress(100, "downloading")
            return finalDir.absolutePath
        }

        val archiveFile = File(modelsRoot, "${model.id}.tar.bz2")
        try {
            downloadToFile("$STT_RELEASE_BASE${model.id}.tar.bz2", archiveFile) { bytesWritten, contentLength ->
                if (contentLength > 0) {
                    onProgress(((bytesWritten.toDouble() / contentLength) * 100).roundToInt(), "downloading")
                }
            }
            onProgress(100, "extracting")
            // Only what SttEngine loads. The release archive also carries the
            // ~150 MB full-precision model.onnx and test WAVs; bzip2-decoding
            // them took minutes on slower phones and left the install stuck at
            // "100% extracting" (and the archive undeleted) while it ran.
            extractTarBz2(archiveFile, modelsRoot, setOf("${model.id}/model.int8.onnx", "${model.id}/tokens.txt"))
            if (!finalDir.exists()) {
                throw IllegalStateException("Extraction finished but ${model.id} is not in the expected location")
            }
            return finalDir.absolutePath
        } finally {
            // The archive is ~150 MB and useless once extracted - same
            // cleanup the source performs in its own `finally` block.
            if (archiveFile.exists()) archiveFile.delete()
        }
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
            // Throttle progress callbacks to roughly every 400ms, matching
            // the source's `progressInterval: 400` - without this a large
            // transfer would call back on every ~64KB chunk.
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

    /** Extracts only the entries named in [needed], stopping as soon as all of them are written. */
    private fun extractTarBz2(archiveFile: File, targetDir: File, needed: Set<String>) {
        val remaining = needed.toMutableSet()
        BufferedInputStream(archiveFile.inputStream()).use { fileStream ->
            BZip2CompressorInputStream(fileStream).use { bzStream ->
                TarArchiveInputStream(bzStream).use { tarStream ->
                    var entry: TarArchiveEntry? = tarStream.nextEntry as TarArchiveEntry?
                    while (entry != null && remaining.isNotEmpty()) {
                        if (!entry.isDirectory && entry.name in remaining) {
                            val outFile = File(targetDir, entry.name)
                            outFile.parentFile?.mkdirs()
                            FileOutputStream(outFile).use { out -> tarStream.copyTo(out) }
                            remaining.remove(entry.name)
                        }
                        if (remaining.isNotEmpty()) entry = tarStream.nextEntry as TarArchiveEntry?
                    }
                }
            }
        }
    }
}
