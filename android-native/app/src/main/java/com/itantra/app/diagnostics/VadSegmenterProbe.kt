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
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.itantra.app.audio.AudioCapture
import com.itantra.app.audio.AudioFrame
import com.itantra.app.config.DEFAULT_VAD_CONFIG
import com.itantra.app.vad.AudioSegment
import com.itantra.app.vad.EnergyVad
import com.itantra.app.vad.SentenceSegmenter
import java.io.File

private const val TAG = "VadSegmenterProbe"

/**
 * TEMPORARY Phase 4 migration diagnostic. Not part of the final product UI.
 * Wires the real AudioCapture (Phase 3) into the real EnergyVad +
 * SentenceSegmenter (Phase 4) exactly as the future transmitter controller
 * will, and proves on a real device that a real speech-start/segment
 * cycle actually fires — not just that the classes compile.
 *
 * Includes a "PLAY REFERENCE CLIP" button that plays the English model's
 * bundled 0.wav (already pushed to the device in Phase 2) through the
 * speaker while capture is running: real sound leaves the speaker and
 * real air-pressure waves re-enter through the microphone, so the VAD is
 * exercised by genuine acoustic speech rather than any injected/fabricated
 * signal. This is the same proof-of-life rigor used in Phase 2/3, adapted
 * to a phase that cannot simply be tapped to produce a transcript.
 */
@Composable
fun VadSegmenterProbeSection(
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    var running by remember { mutableStateOf(false) }
    var speaking by remember { mutableStateOf(false) }
    var segmentCount by remember { mutableStateOf(0) }
    var lastSegmentInfo by remember { mutableStateOf("none yet") }
    var status by remember { mutableStateOf("idle") }

    val vad = remember { EnergyVad() }
    val segmenter = remember {
        SentenceSegmenter(
            config = DEFAULT_VAD_CONFIG,
            onSpeechStart = { speaking = true },
            onSegment = { segment: AudioSegment ->
                speaking = false
                segmentCount += 1
                lastSegmentInfo = "durationMs=${segment.durationMs.toInt()} forced=${segment.forced} samples=${segment.samples.size}"
                Log.i(TAG, "segment #$segmentCount: $lastSegmentInfo")
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
        Text("PHASE 4 · VAD + SEGMENTER PROBE (temporary diagnostic)")

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
                    segmentCount = 0
                    speaking = false
                    lastSegmentInfo = "none yet"
                    status = try {
                        capture.start(context)
                        running = true
                        "capturing…"
                    } catch (e: Exception) {
                        "ERROR: ${e.message}"
                    }
                } else {
                    // Flush whatever utterance is in flight, same as
                    // stopPtt() does in the RN controller, so a segment
                    // mid-flight is not silently discarded.
                    segmenter.flush()
                    capture.stop()
                    running = false
                    speaking = false
                    status = "stopped"
                }
            },
        ) {
            Text(
                when {
                    !hasPermission -> "GRANT MICROPHONE PERMISSION"
                    running -> "STOP"
                    else -> "START VAD + SEGMENTER"
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

        Text("$status · speaking=$speaking · segments=$segmentCount")
        Text("last segment: $lastSegmentInfo")
    }

    DisposableEffect(Unit) {
        onDispose {
            capture.release()
            vad.dispose()
            player?.release()
        }
    }
}
