package com.itantra.app.diagnostics

import android.media.MediaPlayer
import android.util.Log
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.itantra.app.audio.AudioCapture
import com.itantra.app.audio.AudioFrame
import com.itantra.app.config.DEFAULT_VAD_CONFIG
import com.itantra.app.stt.NonSpeechFilter
import com.itantra.app.stt.SttEngineProvider
import com.itantra.app.vad.AudioSegment
import com.itantra.app.vad.EnergyVad
import com.itantra.app.vad.SentenceSegmenter
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

private const val TAG = "SttPipelineProbe"

/**
 * TEMPORARY Phase 5 migration diagnostic. Not part of the final product
 * UI (Phase 9/10 own the real transmitter controller and UI). This is the
 * first point in the migration where the complete pipeline runs
 * end-to-end on a real device: AudioRecord -> EnergyVad ->
 * SentenceSegmenter -> SttEngineProvider -> IndicScriptRepair (inside
 * SherpaSttBackend) -> NonSpeechFilter -> text. Every stage is the same
 * class Phases 2-5 already verified individually; this only proves they
 * compose correctly with real audio.
 *
 * Fixed to English (en-IN) for this diagnostic: that is the language
 * whose model is already pushed to the device from Phase 2's
 * verification, and this phase's job is to prove the *pipeline*, not to
 * re-verify per-language model mappings (already done in Phase 2).
 */
@Composable
fun SttPipelineProbeSection(
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var running by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("idle") }
    var lastResult by remember { mutableStateOf("none yet") }
    var engineStatus by remember { mutableStateOf("not prepared") }

    val sttProvider = remember {
        SttEngineProvider(File(context.filesDir, "itantra-models"))
    }
    val vad = remember { EnergyVad() }
    val segmenter = remember {
        SentenceSegmenter(
            config = DEFAULT_VAD_CONFIG,
            onSpeechStart = {},
            onSegment = { segment: AudioSegment ->
                scope.launch {
                    val outcome = withContext(Dispatchers.Default) {
                        runSegmentThroughStt(sttProvider, segment)
                    }
                    lastResult = outcome
                    Log.i(TAG, outcome)
                }
            },
        )
    }
    val capture = remember {
        AudioCapture(onFrame = { frame: AudioFrame ->
            val probability = vad.process(frame.samples)
            segmenter.push(frame, probability)
        })
    }
    var player by remember { mutableStateOf<MediaPlayer?>(null) }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 5 · COMPLETE STT PIPELINE PROBE (temporary diagnostic)")

        if (!hasPermission) {
            Text("RECORD_AUDIO not granted yet.")
        }

        Button(
            onClick = {
                if (!hasPermission) {
                    onRequestPermission()
                    return@Button
                }
                if (!running) {
                    vad.reset()
                    segmenter.reset()
                    lastResult = "none yet"
                    status = try {
                        capture.start(context)
                        running = true
                        "capturing…"
                    } catch (e: Exception) {
                        "ERROR: ${e.message}"
                    }
                    // Mirrors useTransmitterController.ts: prepare() is called
                    // proactively (there, on language change and again right
                    // after startPtt()) so the decoder is warm before the
                    // first segment arrives, rather than left at whatever
                    // SttEngineProvider defaults to (the placeholder) until
                    // something happens to call prepare() first. Warmed in
                    // the background, same as the source, so it does not
                    // block the mic from opening.
                    scope.launch {
                        val result = withContext(Dispatchers.Default) {
                            sttProvider.prepare("en-IN")
                        }
                        engineStatus = "engine=${result.kind}" +
                            (result.reason?.let { " reason=$it" } ?: "")
                    }
                } else {
                    segmenter.flush()
                    capture.stop()
                    running = false
                    status = "stopped"
                }
            },
        ) {
            Text(
                when {
                    !hasPermission -> "GRANT MICROPHONE PERMISSION"
                    running -> "STOP"
                    else -> "START FULL PIPELINE (EN)"
                }
            )
        }

        Button(
            enabled = running,
            onClick = {
                val wav = File(
                    context.filesDir,
                    "itantra-models/sherpa-onnx-nemo-ctc-en-conformer-medium/0.wav",
                )
                if (!wav.exists()) {
                    status = "reference clip missing at ${wav.absolutePath} (pushed in Phase 2)"
                    return@Button
                }
                try {
                    player?.release()
                    val mp = MediaPlayer()
                    mp.setDataSource(wav.absolutePath)
                    mp.prepare()
                    mp.start()
                    player = mp
                    status = "playing reference clip…"
                } catch (e: Exception) {
                    status = "PLAYBACK ERROR: ${e.message}"
                }
            },
        ) {
            Text("PLAY REFERENCE CLIP (0.wav)")
        }

        Text(status)
        Text(engineStatus)
        Text("last result: $lastResult")
    }

    DisposableEffect(Unit) {
        onDispose {
            capture.release()
            vad.dispose()
            sttProvider.dispose()
            player?.release()
        }
    }
}

/**
 * The exact post-processing order the RN transmitter controller applies
 * in useTransmitterController.ts's handleSegment(): STT decode (which
 * internally applies script repair) first, then the non-speech-artifact
 * filter on the result, before the text would ever become a packet.
 */
private fun runSegmentThroughStt(provider: SttEngineProvider, segment: AudioSegment): String {
    val languageCode = "en-IN"
    return try {
        val result = provider.transcribe(segment.samples, languageCode)
        if (result.text.isEmpty() || NonSpeechFilter.isNonSpeechArtifact(result.text)) {
            "segment durationMs=${segment.durationMs.toInt()} forced=${segment.forced} -> " +
                "discarded as non-speech (raw=\"${result.rawText}\")"
        } else {
            "segment durationMs=${segment.durationMs.toInt()} forced=${segment.forced} " +
                "engine=${provider.status.kind} -> \"${result.text}\""
        }
    } catch (e: Exception) {
        "ERROR transcribing segment: ${e.message}"
    }
}
