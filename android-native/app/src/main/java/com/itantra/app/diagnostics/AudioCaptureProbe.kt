package com.itantra.app.diagnostics

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

/**
 * TEMPORARY Phase 3 migration diagnostic. Not part of the final product UI
 * (the real capture control is the PTT button, Phase 10). Proves
 * AudioCapture (android.media.AudioRecord) genuinely records real
 * microphone audio end to end: permission -> start -> real PCM frames ->
 * stop -> release, with no crash — mirroring the same kind of proof-of-life
 * this migration used for Phase 2's STT probe.
 *
 * Does not touch VAD, segmentation, or STT — this only proves capture
 * itself, per the phase's explicit scope.
 *
 * @param hasPermission current RECORD_AUDIO grant state, owned by
 *   MainActivity (which owns the ActivityResultLauncher that can actually
 *   show the system permission dialog).
 * @param onRequestPermission invoked when the operator presses start
 *   without permission yet granted — mirrors the RN controller's own
 *   on-demand permission request in startPtt(), rather than asking
 *   proactively at app launch.
 */
@Composable
fun AudioCaptureProbeSection(
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    var capturing by remember { mutableStateOf(false) }
    var frameCount by remember { mutableStateOf(0) }
    var lastRms by remember { mutableStateOf(0f) }
    var status by remember { mutableStateOf("idle") }

    val capture = remember {
        AudioCapture(onFrame = { frame: AudioFrame ->
            frameCount += 1
            lastRms = frame.rms
        })
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 3 · AUDIO CAPTURE PROBE (temporary diagnostic)")

        if (!hasPermission) {
            Text("RECORD_AUDIO not granted yet.")
        }

        Button(
            onClick = {
                if (!hasPermission) {
                    onRequestPermission()
                    return@Button
                }
                if (!capturing) {
                    frameCount = 0
                    lastRms = 0f
                    status = try {
                        capture.start(context)
                        capturing = true
                        "capturing…"
                    } catch (e: Exception) {
                        "ERROR: ${e.message}"
                    }
                } else {
                    capture.stop()
                    capturing = false
                    status = "stopped"
                }
            },
        ) {
            Text(
                when {
                    !hasPermission -> "GRANT MICROPHONE PERMISSION"
                    capturing -> "STOP CAPTURE"
                    else -> "START CAPTURE"
                }
            )
        }

        Text("$status · frames=$frameCount · lastRms=${"%.4f".format(lastRms)}")
    }

    DisposableEffect(Unit) {
        onDispose {
            capture.release()
        }
    }
}
