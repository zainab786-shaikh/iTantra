package com.itantra.app.diagnostics

import android.util.Log
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.itantra.app.stt.SttEngine
import com.k2fsa.sherpa.onnx.WaveReader
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * TEMPORARY Phase 2 migration diagnostic. Not part of the final product UI
 * (see MIGRATION_STATUS.md Phase 2 / Phase 10) — proves the direct
 * sherpa-onnx integration actually decodes real PCM into a real transcript
 * on-device, for each modelId/language pair pushed to
 * <filesDir>/itantra-models/<modelId>/ (model.int8.onnx, tokens.txt, and a
 * reference .wav — the exact side-load layout src/core/stt/ModelManager.ts
 * already uses).
 */
data class SttProbeTarget(
    val label: String,
    val modelId: String,
    val referenceWav: String,
    val expectedText: String,
)

val STT_PROBE_TARGETS = listOf(
    SttProbeTarget(
        label = "ENGLISH · NeMo CTC Medium",
        modelId = "sherpa-onnx-nemo-ctc-en-conformer-medium",
        referenceWav = "0.wav",
        expectedText = "AFTER EARLY NIGHTFALL THE YELLOW LAMPS WOULD LIGHT UP HERE AND " +
            "THERE THE SQUALID QUARTER OF THE BROTHELS",
    ),
    SttProbeTarget(
        label = "HINDI · AI4Bharat IndicConformer",
        modelId = "indicconformer-hi",
        referenceWav = "reference.wav",
        expectedText = "",
    ),
)

private const val TAG = "SttProbe"

@Composable
fun SttProbeSection() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var results by remember { mutableStateOf(emptyMap<String, String>()) }
    var running by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 2 · STT PROBE (temporary diagnostic)")

        for (target in STT_PROBE_TARGETS) {
            Button(
                onClick = {
                    running = target.modelId
                    scope.launch {
                        val outcome = withContext(Dispatchers.Default) { runProbe(context.filesDir, target) }
                        results = results + (target.modelId to outcome)
                        running = null
                    }
                },
                enabled = running == null,
            ) {
                Text(if (running == target.modelId) "RUNNING…" else "RUN ${target.label}")
            }
            results[target.modelId]?.let { Text(it) }
        }
    }
}

private fun runProbe(filesDir: File, target: SttProbeTarget): String {
    val modelDir = File(filesDir, "itantra-models/${target.modelId}")
    val wavFile = File(modelDir, target.referenceWav)

    if (!modelDir.exists()) {
        return "NOT INSTALLED — ${modelDir.absolutePath} missing"
    }
    if (!wavFile.exists()) {
        return "NO REFERENCE WAV — ${wavFile.absolutePath} missing"
    }

    val engine = SttEngine()
    return try {
        val loadStart = System.currentTimeMillis()
        engine.load(modelDir.absolutePath)
        val loadMs = System.currentTimeMillis() - loadStart

        val wave = WaveReader.readWave(wavFile.absolutePath)
        val decodeStart = System.currentTimeMillis()
        val text = engine.transcribe(wave.samples, wave.sampleRate)
        val decodeMs = System.currentTimeMillis() - decodeStart

        Log.i(TAG, "[${target.modelId}] load=${loadMs}ms decode=${decodeMs}ms text=\"$text\"")
        val matchNote = if (target.expectedText.isNotEmpty()) {
            if (text.trim().equals(target.expectedText, ignoreCase = true)) "EXACT MATCH" else "text differs from reference"
        } else {
            "no reference transcript to compare"
        }
        "OK · load ${loadMs}ms · decode ${decodeMs}ms · $matchNote\n\"$text\""
    } catch (e: Exception) {
        Log.e(TAG, "[${target.modelId}] failed", e)
        "ERROR: ${e.message ?: e.javaClass.simpleName}"
    } finally {
        engine.release()
    }
}
