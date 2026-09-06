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
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.itantra.app.device.DeviceId
import com.itantra.app.packet.buildPacket
import com.itantra.app.receiver.ReceivedMessage
import com.itantra.app.receiver.ReceivedMessageState
import com.itantra.app.transport.MockTransport
import kotlinx.coroutines.launch

/**
 * TEMPORARY Phase 7 migration diagnostic. Not part of the final product UI.
 *
 * Proves a packet can travel transmit -> packet -> MockTransport -> receive
 * -> receive handling on this device, using the same loopback-after-delay
 * behaviour as src/core/transport/MockTransport.ts (no real networking).
 */
@Composable
fun TransportProbeSection() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val transport = remember { MockTransport(failureRate = 0.0) }

    var connected by remember { mutableStateOf(transport.isConnected()) }
    var sendLog by remember { mutableStateOf("not run yet") }
    var received by remember { mutableStateOf(listOf<ReceivedMessage>()) }

    DisposableEffect(transport) {
        val unsubConn = transport.onConnectionChange { connected = it }
        val unsubRecv = transport.onPacketReceived { packet ->
            received = listOf(
                ReceivedMessage(
                    packet = packet,
                    state = ReceivedMessageState.RECEIVED,
                    error = null,
                    receivedAt = System.currentTimeMillis(),
                )
            ) + received
        }
        onDispose {
            unsubConn()
            unsubRecv()
        }
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("PHASE 7 · MOCKTRANSPORT + RECEIVE PROBE (temporary diagnostic)")
        Text("connected=$connected")

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                val senderId = DeviceId.getSenderId(context)
                val packet = buildPacket(
                    text = "there is a fire, emergency",
                    language = "en-IN",
                    senderId = senderId,
                )
                sendLog = "sending id=${packet.id.take(8)}…"
                scope.launch {
                    val ok = transport.sendPacket(packet)
                    sendLog = "sendPacket -> $ok (id=${packet.id.take(8)}…, sent.size=${transport.sent.size})"
                }
            }) {
                Text("SEND SAMPLE PACKET")
            }

            Button(onClick = { transport.setConnected(!transport.isConnected()) }) {
                Text("TOGGLE CONNECTION")
            }
        }

        Text(sendLog)

        Text("received (${received.size}):")
        received.take(5).forEach { m ->
            Text(
                "[${m.state.value}] [${m.packet.priority.value}] \"${m.packet.text}\" " +
                    "(${m.packet.language}) id=${m.packet.id.take(8)}…"
            )
        }
    }
}
