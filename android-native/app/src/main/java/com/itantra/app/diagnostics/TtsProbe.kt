package com.itantra.app.diagnostics

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import com.itantra.app.packet.PacketPriority
import com.itantra.app.tts.INITIAL_TTS_PLAYBACK_STATE
import com.itantra.app.tts.TtsManager
import com.itantra.app.tts.TtsPlaybackState
import com.itantra.app.tts.TtsVoiceStatus
import java.io.File

/**
 * TEMPORARY Phase 8 migration diagnostic. Not part of the final product UI.
 *
 * Proves the ported TtsManager (queueing, priority interruption, audio
 * focus) drives the real sherpa-onnx VITS engine end-to-end on this device:
 * synthesize -> WAV -> MediaPlayer playback, exactly the same voice
 * (side-loaded Piper English) the RN app itself uses.
 */
@Composable
fun TtsProbeSection() {
    val context = LocalContext.current
    val ttsManager = remember {
        TtsManager(context, File(context.filesDir, "itantra-tts-models"))
    }

    var state by remember { mutableStateOf<TtsPlaybackState>(INITIAL_TTS_PLAYBACK_STATE) }
    var voiceStatusLine by remember { mutableStateOf("not checked yet") }

    DisposableEffect(ttsManager) {
        val unsubscribe = ttsManager.subscribe { state = it }
        onDispose { unsubscribe() }
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 8 · NATIVE TTS PROBE (temporary diagnostic)")

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                val enStatus = ttsManager.voiceStatus("en-IN")
                val hiStatus = ttsManager.voiceStatus("hi-IN")
                voiceStatusLine = "en-IN=${describe(enStatus)} hi-IN=${describe(hiStatus)}"
            }) {
                Text("CHECK VOICE STATUS")
            }
        }
        Text(voiceStatusLine)

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                ttsManager.speakText(
                    text = "Position secure, all clear on this side.",
                    language = "en-IN",
                    priority = PacketPriority.NORMAL,
                )
            }) {
                Text("SPEAK NORMAL")
            }

            Button(onClick = {
                ttsManager.speakText(
                    text = "There is a fire, emergency, evacuate now.",
                    language = "en-IN",
                    priority = PacketPriority.CRITICAL,
                )
            }) {
                Text("SPEAK CRITICAL")
            }
        }

        Button(onClick = {
            // Queue a long normal message, then a critical one shortly
            // after - while the normal message is still loading/speaking -
            // to exercise the interrupt-and-requeue path.
            ttsManager.speakText(
                text = "This is a long normal priority message that should be interrupted partway through by a critical alert arriving right behind it.",
                language = "en-IN",
                priority = PacketPriority.NORMAL,
            )
            ttsManager.speakText(
                text = "Critical alert, fire, evacuate immediately.",
                language = "en-IN",
                priority = PacketPriority.CRITICAL,
            )
        }) {
            Text("TEST CRITICAL INTERRUPTS NORMAL")
        }

        Text(
            "phase=${state.phase.value} requestId=${state.requestId?.take(8) ?: "null"} " +
                "priority=${state.priority?.value ?: "null"} isCritical=${state.isCritical}"
        )
        Text("text=${state.text ?: "null"}")
        Text("error=${state.error ?: "none"}")
    }
}

private fun describe(status: TtsVoiceStatus?): String = when (status) {
    null -> "no-voice-registered"
    is TtsVoiceStatus.Installed -> "installed"
    is TtsVoiceStatus.NotInstalled -> "not-installed"
    is TtsVoiceStatus.Downloading -> "downloading(${status.percent}%)"
    is TtsVoiceStatus.Error -> "error(${status.message})"
}
