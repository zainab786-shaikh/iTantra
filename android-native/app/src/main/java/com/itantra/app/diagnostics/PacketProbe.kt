package com.itantra.app.diagnostics

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.buildPacket

/**
 * TEMPORARY Phase 6 migration diagnostic. Not part of the final product
 * UI. Proves the packet layer builds a correctly-shaped iTantraPacket
 * with a real device-derived senderId, and that classifyPriority()
 * matches expectations across languages — same keyword lists as
 * src/core/packet/priority.ts, run here against representative samples
 * rather than a full corpus.
 */
@Composable
fun PacketProbeSection() {
    val context = LocalContext.current
    var output by remember { mutableStateOf("not run yet") }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 6 · PACKET LAYER PROBE (temporary diagnostic)")

        Button(onClick = {
            val senderId = DeviceId.getSenderId(context)

            val samples = listOf(
                "hello there, position secure" to "en-IN",
                "there is a fire, emergency" to "en-IN",
                "हमें तुरंत मदद चाहिए" to "hi-IN",
                "स्थिति सुरक्षित है" to "hi-IN",
                "confirm your status" to "en-IN",
            )

            val lines = samples.map { (text, lang) ->
                val packet = buildPacket(text = text, language = lang, senderId = senderId)
                "[${packet.priority.value}] \"${packet.text}\" (${packet.language}) " +
                    "id=${packet.id.take(8)}… isCompressed=${packet.isCompressed}"
            }

            output = "senderId=$senderId\n" + lines.joinToString("\n")
        }) {
            Text("BUILD SAMPLE PACKETS")
        }

        Text(output)
    }
}
