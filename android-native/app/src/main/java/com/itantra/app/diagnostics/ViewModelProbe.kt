package com.itantra.app.diagnostics

import android.media.MediaPlayer
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.itantra.app.viewmodel.AppViewModel
import java.io.File

/**
 * TEMPORARY Phase 9 migration diagnostic. Not part of the final product UI
 * (Phase 10 builds the real Transmit/Receive screens against these same
 * ViewModels). Proves the ported TransmitterViewModel/ReceiverViewModel,
 * sharing one AppViewModel-owned MockTransport, are wired together
 * correctly: starting the transmitter and playing a real reference clip
 * through the speaker (the same speaker-to-mic acoustic loopback technique
 * Phases 4/5 used) should flow mic -> VAD -> segmenter -> STT -> packet ->
 * MockTransport -> the *same* shared transport's receive side -> TtsManager,
 * all without any diagnostic code gluing the pieces together itself - that
 * gluing is entirely inside the ViewModels ported this phase.
 */
@Composable
fun ViewModelProbeSection(
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
) {
    val context = LocalContext.current
    val appViewModel: AppViewModel = viewModel()
    val transmitter = appViewModel.transmitter
    val receiver = appViewModel.receiver

    val txState by transmitter.transcriptionState.collectAsState()
    val txActive by transmitter.isActive.collectAsState()
    val txLog by transmitter.log.collectAsState()
    val rxMessages by receiver.messages.collectAsState()
    val rxTtsState by receiver.ttsState.collectAsState()

    var player by remember { mutableStateOf<MediaPlayer?>(null) }
    var playStatus by remember { mutableStateOf("idle") }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 9 · VIEWMODEL WIRING PROBE (temporary diagnostic)")

        if (!hasPermission) {
            Text("RECORD_AUDIO not granted yet.")
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                if (!hasPermission) {
                    onRequestPermission()
                } else if (!txActive) {
                    transmitter.startPtt(context)
                } else {
                    transmitter.stopPtt()
                }
            }) {
                Text(
                    when {
                        !hasPermission -> "GRANT MICROPHONE PERMISSION"
                        txActive -> "STOP PTT"
                        else -> "START PTT"
                    }
                )
            }

            Button(
                enabled = txActive,
                onClick = {
                    val wav = File(
                        context.filesDir,
                        "itantra-models/sherpa-onnx-nemo-ctc-en-conformer-medium/0.wav",
                    )
                    if (!wav.exists()) {
                        playStatus = "reference clip missing at ${wav.absolutePath}"
                        return@Button
                    }
                    try {
                        player?.release()
                        val mp = MediaPlayer()
                        mp.setDataSource(wav.absolutePath)
                        mp.prepare()
                        mp.start()
                        player = mp
                        playStatus = "playing reference clip…"
                    } catch (e: Exception) {
                        playStatus = "PLAYBACK ERROR: ${e.message}"
                    }
                },
            ) {
                Text("PLAY REFERENCE CLIP (0.wav)")
            }
        }

        Text(playStatus)
        Text("--- transmitter ---")
        Text("status=${txState.status.value} engine=${txState.engine} error=${txState.error ?: "none"}")
        Text("lastResult=${txState.lastResult?.text ?: "none yet"}")
        Text("log entries=${txLog.size}" + (txLog.firstOrNull()?.let { " latest=[${it.packet.priority.value}] \"${it.packet.text}\" delivered=${it.delivered}" } ?: ""))

        Text("--- receiver (shared transport) ---")
        Text("ttsPhase=${rxTtsState.phase.value} ttsText=${rxTtsState.text ?: "none"}")
        Text("received=${rxMessages.size}" + (rxMessages.firstOrNull()?.let { " latest=[${it.state.value}] \"${it.packet.text}\"" } ?: ""))
    }

    DisposableEffect(Unit) {
        onDispose {
            player?.release()
        }
    }
}
