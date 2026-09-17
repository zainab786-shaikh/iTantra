package com.itantra.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.transport.DEFAULT_LINK_PORT
import com.itantra.app.transport.LINK_RATE_PRESETS
import com.itantra.app.transport.LinkControl
import com.itantra.app.transport.ThrottleControl
import com.itantra.app.ui.components.AirtimeRace
import com.itantra.app.ui.components.ConnectionBadge
import com.itantra.app.ui.components.ThemeMenu
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.ui.theme.AppSizing as Sizing

/**
 * Link screen — reference image phone 3.
 *
 * Structure:
 *   HEADER
 *   HERO illustration
 *   THIS DEVICE card
 *   PEER DEVICE card (with functional Connect button)
 *   LINK SETTINGS card
 */
@Composable
fun LinkScreen(
    link: LinkControl?,
    throttle: ThrottleControl,
    isDarkTheme: Boolean,
    onThemeToggle: (Boolean) -> Unit,
) {
    val illustrationRes = if (isDarkTheme) com.itantra.app.R.drawable.mountain_illustration_dark else com.itantra.app.R.drawable.mountain_illustration_light

    // LEFT scrollbar
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(AppColor.Void)
                .verticalScroll(rememberScrollState()),
        ) {
            CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
                Column(modifier = Modifier.fillMaxWidth()) {

                    // ── HEADER ──────────────────────────────────────────────
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .windowInsetsPadding(WindowInsets.statusBars)
                            .padding(horizontal = AppSizing.screenPadding, vertical = 8.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            ThemeMenu(isDarkTheme = isDarkTheme, onThemeToggle = onThemeToggle)
                            Column {
                                Text("iTantra", color = AppColor.Text, fontSize = 19.sp, fontWeight = FontWeight.Black)
                                Text(
                                    "DIRECT. SECURE. INDEPENDENT.",
                                    color = AppColor.TextFaint,
                                    fontSize = 8.sp,
                                    letterSpacing = 1.4.sp,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                )
                            }
                        }
                        if (link != null) {
                            val connected = link.lastHeardMs.collectAsState().value?.let { it < 45_000 } ?: false
                            ConnectionBadge(connected = connected, label = "Link Active", compact = true)
                        }
                    }

                    // ── HERO ILLUSTRATION ────────────────────────────────────
                    Image(
                        painter = androidx.compose.ui.res.painterResource(illustrationRes),
                        contentDescription = null,
                        contentScale = ContentScale.Crop,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(160.dp),
                    )

                    // ── CONTENT ──────────────────────────────────────────────
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = AppSizing.screenPadding)
                            .padding(top = 16.dp, bottom = 24.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
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
                            ThisDeviceCard(link)
                            PeerDeviceCard(link)
                        }

                        LinkSettingsCard(throttle)
                    }
                }
            }
        }
    }
}

@Composable
private fun ThisDeviceCard(link: LinkControl) {
    val localAddress by link.localAddress.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.lg))
            .padding(horizontal = 14.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Card title
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            // WiFi-style icon using concentric arcs approximation with boxes
            Box(
                modifier = Modifier
                    .size(16.dp)
                    .border(2.dp, AppColor.Primary, CircleShape),
            )
            Text("This device", color = AppColor.Text, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }

        InfoRow("Device ID", link.nodeLabel)
        InfoRow("IP address", localAddress)
        InfoRow("Port", DEFAULT_LINK_PORT.toString())

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Status", color = AppColor.TextFaint, fontSize = 12.sp)
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Box(modifier = Modifier.size(6.dp).background(AppColor.Live, CircleShape))
                Text("Listening", color = AppColor.Text, fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun PeerDeviceCard(link: LinkControl) {
    val stored by link.configuredHost.collectAsState()
    var draft by remember(stored) { mutableStateOf(stored) }
    val keyboard = LocalSoftwareKeyboardController.current

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.lg))
            .padding(horizontal = 14.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Box(
                modifier = Modifier
                    .size(16.dp)
                    .background(AppColor.TextFaint.copy(alpha = 0.3f), CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                Box(modifier = Modifier.size(6.dp).background(AppColor.TextFaint, CircleShape))
            }
            Text("Peer device", color = AppColor.Text, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }

        Text("Peer address", color = AppColor.TextFaint, fontSize = 11.sp)

        OutlinedTextField(
            value = draft,
            onValueChange = { draft = it },
            singleLine = true,
            placeholder = { Text("192.168.43.1", color = AppColor.TextFaint, fontSize = 14.sp) },
            trailingIcon = {
                if (draft.isNotEmpty()) {
                    Text(
                        "✕",
                        color = AppColor.TextFaint,
                        fontSize = 12.sp,
                        modifier = Modifier.clickable { draft = "" }.padding(8.dp),
                    )
                }
            },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { keyboard?.hide() }),
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

        // Connect button — calls existing setPeerHost
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(Sizing.touchTarget)
                .background(AppColor.Primary, RoundedCornerShape(AppRadius.md))
                .clickable {
                    link.setPeerHost(draft)
                    keyboard?.hide()
                },
            contentAlignment = Alignment.Center,
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("🔗", fontSize = 14.sp)
                Text(
                    "CONNECT",
                    color = AppColor.Surface,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 1.sp,
                )
            }
        }
    }
}

@Composable
private fun LinkSettingsCard(throttle: ThrottleControl) {
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
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(Icons.Rounded.Settings, contentDescription = null, tint = AppColor.TextMuted, modifier = Modifier.size(16.dp))
            Text("Link settings", color = AppColor.Text, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }

        Text("Simulated link rate", color = AppColor.TextMuted, fontSize = 11.sp)

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
            "A rate limiter, not a radio. 250 bps is LoRa at SF12, the slowest common long-range setting.",
            color = AppColor.TextFaint,
            fontSize = 10.sp,
            lineHeight = 14.sp,
        )

        Box(modifier = Modifier.fillMaxWidth().height(1.dp).background(AppColor.Hairline))

        AirtimeRace(cost = lastFrame, bitsPerSecond = bps, enabled = enabled, runKey = runKey)

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
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = AppColor.TextFaint, fontSize = 12.sp)
        Text(value, color = AppColor.Text, fontSize = 12.sp, maxLines = 1)
    }
}

@Composable
private fun RateButton(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .height(Sizing.touchTarget)
            .background(
                if (selected) AppColor.Primary.copy(alpha = 0.15f) else AppColor.Surface,
                RoundedCornerShape(AppRadius.sm),
            )
            .border(
                1.dp,
                if (selected) AppColor.Primary.copy(alpha = 0.5f) else AppColor.Hairline,
                RoundedCornerShape(AppRadius.sm),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = if (selected) AppColor.Primary else AppColor.TextMuted,
            fontSize = 12.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
        )
    }
}
