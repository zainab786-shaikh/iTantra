package com.itantra.app.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.text.style.TextAlign
import com.itantra.app.transport.LINK_RATE_PRESETS
import com.itantra.app.transport.LinkControl
import com.itantra.app.transport.PeerSource
import com.itantra.app.transport.ThrottleControl
import com.itantra.app.ui.components.AirtimeRace
import com.itantra.app.ui.theme.AppSizing as Sizing
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing

/**
 * Link setup: who this device is, where it is, and where its peer is.
 *
 * Deliberately plain. It is operator configuration, not part of the
 * transmit/receive cockpit, and on the day it needs to be usable in about
 * fifteen seconds with a camera pointed at it.
 */
@Composable
fun LinkScreen(link: LinkControl?, throttle: ThrottleControl) {
    Box(modifier = Modifier.fillMaxSize().background(AppColor.Void)) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(WindowInsets.statusBars)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = AppSizing.screenPadding)
                .padding(top = 8.dp, bottom = 96.dp),
            verticalArrangement = Arrangement.spacedBy(18.dp),
        ) {
            Column {
                Text("iTantra", color = AppColor.Text, fontSize = 19.sp, fontWeight = FontWeight.Black)
                Text(
                    "LINK · SETUP",
                    color = AppColor.TextFaint,
                    fontSize = 8.5.sp,
                    letterSpacing = 1.5.sp,
                    fontWeight = FontWeight.Bold,
                )
            }

            if (link == null) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.md))
                        .padding(vertical = 26.dp, horizontal = 16.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "Running on the in-app loopback — there is no peer to configure.",
                        color = AppColor.TextFaint,
                        fontSize = 12.sp,
                    )
                }
            } else {
                LinkStatusCard(link)
                PeerField(link)

                Text(
                    "Enter the other device's address, then press Done. Once the two " +
                        "have exchanged a single frame, each one keeps using the address " +
                        "it actually heard from — so only one side needs to be filled in.",
                    color = AppColor.TextFaint,
                    fontSize = 11.sp,
                    lineHeight = 17.sp,
                )
            }

            // The throttle wraps every transport, loopback included, so this
            // section renders regardless of whether there is a peer.
            ThrottleSection(throttle)
        }
    }
}

@Composable
private fun LinkStatusCard(link: LinkControl) {
    val localAddress by link.localAddress.collectAsState()
    val peerAddress by link.peerAddress.collectAsState()
    val peerSource by link.peerSource.collectAsState()
    val lastHeard by link.lastHeardMs.collectAsState()

    // lastHeardMs is republished on the transport's own tick, so this figure
    // refreshes without a second timer here.
    val live = (lastHeard ?: Long.MAX_VALUE) < 45_000
    val statusColor = if (live) AppColor.Live else AppColor.Danger

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.lg))
            .padding(horizontal = 14.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Box(modifier = Modifier.size(6.dp).background(statusColor, CircleShape))
            Text(
                if (live) "PEER LIVE" else "NO PEER",
                color = statusColor,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.8.sp,
            )
        }

        InfoRow("THIS DEVICE", link.nodeLabel)
        InfoRow("THIS ADDRESS", localAddress)
        InfoRow("PEER", peerAddress ?: "—")
        InfoRow(
            "PEER FOUND BY",
            when (peerSource) {
                PeerSource.NONE -> "not yet — broadcasting"
                PeerSource.CONFIGURED -> "address entered below"
                PeerSource.LEARNED -> "heard from directly"
            },
        )
        InfoRow(
            "LAST HEARD",
            lastHeard?.let { "${(it / 1000)}s ago" } ?: "never",
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            color = AppColor.TextFaint,
            fontSize = 9.sp,
            fontWeight = FontWeight.Black,
            letterSpacing = 1.1.sp,
        )
        Text(value, color = AppColor.TextMuted, fontSize = 12.sp, maxLines = 1)
    }
}

@Composable
private fun PeerField(link: LinkControl) {
    val stored by link.configuredHost.collectAsState()
    var draft by remember(stored) { mutableStateOf(stored) }
    val keyboard = LocalSoftwareKeyboardController.current

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            "PEER ADDRESS",
            color = AppColor.TextMuted,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
        )
        OutlinedTextField(
            value = draft,
            onValueChange = { draft = it },
            singleLine = true,
            placeholder = { Text("192.168.43.1", color = AppColor.TextFaint, fontSize = 14.sp) },
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Uri,
                imeAction = ImeAction.Done,
            ),
            keyboardActions = KeyboardActions(onDone = {
                link.setPeerHost(draft)
                keyboard?.hide()
            }),
            colors = OutlinedTextFieldDefaults.colors(
                focusedTextColor = AppColor.Text,
                unfocusedTextColor = AppColor.Text,
                focusedBorderColor = AppColor.Primary,
                unfocusedBorderColor = AppColor.Hairline,
                cursorColor = AppColor.Primary,
                focusedContainerColor = AppColor.Surface,
                unfocusedContainerColor = AppColor.Surface,
            ),
            shape = RoundedCornerShape(AppRadius.sm),
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/**
 * Simulated link rate, and the race it makes visible.
 *
 * Labelled on screen as a rate limiter rather than a radio. That is a
 * non-negotiable of the demo brief and it belongs in the UI, not only in the
 * narration - a viewer should not have to take anyone's word for what they
 * are being shown.
 */
@Composable
private fun ThrottleSection(throttle: ThrottleControl) {
    val enabled by throttle.enabled.collectAsState()
    val bps by throttle.bitsPerSecond.collectAsState()
    val lastFrame by throttle.lastFrame.collectAsState()
    var runKey by remember { mutableIntStateOf(0) }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.lg))
            .padding(horizontal = 14.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            "SIMULATED LINK RATE",
            color = AppColor.TextMuted,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
        )

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            RateButton("Off", !enabled) { throttle.setEnabled(false) }
            LINK_RATE_PRESETS.forEach { (label, rate) ->
                RateButton(label, enabled && bps == rate) {
                    throttle.setEnabled(true)
                    throttle.setRate(rate)
                }
            }
        }

        Text(
            "A rate limiter, not a radio. 250 bps is LoRa at SF12, the slowest " +
                "common long-range setting. Airtime is arithmetic — bytes × 8 ÷ bitrate.",
            color = AppColor.TextFaint,
            fontSize = 10.sp,
            lineHeight = 14.sp,
        )

        Box(modifier = Modifier.fillMaxWidth().height(1.dp).background(AppColor.Hairline))

        AirtimeRace(
            cost = lastFrame,
            bitsPerSecond = bps,
            enabled = enabled,
            runKey = runKey,
        )

        if (lastFrame != null) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(Sizing.touchTarget)
                    .background(AppColor.Primary.copy(alpha = 0.12f), RoundedCornerShape(AppRadius.sm))
                    .border(1.dp, AppColor.Primary.copy(alpha = 0.5f), RoundedCornerShape(AppRadius.sm))
                    .clickable { runKey++ },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    "RUN RACE AGAIN",
                    color = AppColor.Primary,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 1.sp,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}

@Composable
private fun RateButton(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .height(Sizing.touchTarget)
            .background(
                if (selected) AppColor.Accent.copy(alpha = 0.12f) else AppColor.Surface,
                RoundedCornerShape(AppRadius.sm),
            )
            .border(
                1.dp,
                if (selected) AppColor.Accent.copy(alpha = 0.53f) else AppColor.Hairline,
                RoundedCornerShape(AppRadius.sm),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = if (selected) AppColor.AccentStrong else AppColor.TextMuted,
            fontSize = 11.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
        )
    }
}
