package com.itantra.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.R
import com.itantra.app.config.findLanguage
import com.itantra.app.core.ModelReadiness
import com.itantra.app.core.TransmitterStatus
import com.itantra.app.ui.components.ConnectionBadge
import com.itantra.app.ui.components.LanguageSelector
import com.itantra.app.ui.components.ModelCard
import com.itantra.app.ui.components.PacketLog
import com.itantra.app.ui.components.PttButton
import com.itantra.app.ui.components.TelemetryStrip
import com.itantra.app.ui.components.ThemeMenu
import com.itantra.app.ui.components.WaveVisualizer
import com.itantra.app.ui.statusMeta
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.viewmodel.TransmitterViewModel

/**
 * Transmit screen — reference image phone 1.
 *
 * Structure:
 *   HEADER (logo + status + hamburger)
 *   HERO  (mountain illustration, fixed height)
 *   LANGUAGE DROPDOWN
 *   MIC AREA (waveform + PTT + status)
 *   SEND AS CRITICAL (compact row)
 *   LAST TRANSMISSION (telemetry)
 *   PACKET LOG (below fold)
 */
@Composable
fun TransmitterScreen(
    transmitter: TransmitterViewModel,
    hasMicPermission: Boolean,
    onRequestMicPermission: () -> Unit,
    linkNote: String,
    simNote: String?,
    isDarkTheme: Boolean,
    onThemeToggle: (Boolean) -> Unit,
) {
    val context = LocalContext.current

    val transcriptionState by transmitter.transcriptionState.collectAsState()
    val isActive by transmitter.isActive.collectAsState()
    val language by transmitter.language.collectAsState()
    val pauseMs by transmitter.pauseMs.collectAsState()
    val log by transmitter.log.collectAsState()
    val senderId by transmitter.senderId.collectAsState()
    val connected by transmitter.connected.collectAsState()
    val level by transmitter.level.collectAsState()
    val modelStatus by transmitter.modelStatus.collectAsState()
    val sendAsCritical by transmitter.sendAsCritical.collectAsState()
    val sttReadiness by transmitter.sttReadiness.collectAsState()
    val modelLoading = sttReadiness == ModelReadiness.LOADING

    val status = if (modelLoading && transcriptionState.status == TransmitterStatus.IDLE) {
        statusMeta(TransmitterStatus.INITIALIZING).copy(label = "LOADING SPEECH MODEL")
    } else {
        statusMeta(transcriptionState.status)
    }
    val busy = transcriptionState.status == TransmitterStatus.TRANSCRIBING
    val activeModel = transmitter.activeModel

    val illustrationRes = if (isDarkTheme) R.drawable.mountain_illustration_dark else R.drawable.mountain_illustration_light

    // LEFT scrollbar: RTL wrapper on scroll column, LTR on content
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
                        // Left: hamburger + logo
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            ThemeMenu(isDarkTheme = isDarkTheme, onThemeToggle = onThemeToggle)
                            Column {
                                Text("iTantra", color = AppColor.Text, fontSize = 19.sp, fontWeight = FontWeight.Black)
                                Text(
                                    linkNote,
                                    color = AppColor.TextFaint,
                                    fontSize = 8.sp,
                                    letterSpacing = 1.4.sp,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                )
                                if (simNote != null) {
                                    Text(
                                        simNote,
                                        color = AppColor.Warn,
                                        fontSize = 8.sp,
                                        letterSpacing = 1.sp,
                                        fontWeight = FontWeight.Bold,
                                        maxLines = 1,
                                    )
                                }
                            }
                        }
                        // Right: connection badge
                        ConnectionBadge(connected = connected, label = transmitter.transportName, compact = true)
                    }

                    // ── HERO ILLUSTRATION ────────────────────────────────────
                    Image(
                        painter = painterResource(illustrationRes),
                        contentDescription = null,
                        contentScale = ContentScale.Crop,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(160.dp),
                    )

                    // ── CONTENT AREA ─────────────────────────────────────────
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = AppSizing.screenPadding)
                            .padding(top = 16.dp, bottom = 24.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {

                        // Language dropdown — immediately below hero
                        LanguageSelector(
                            value = language,
                            onChange = { transmitter.setLanguage(it) },
                            disabled = isActive,
                        )

                        // ── MIC AREA ─────────────────────────────────────────
                        Column(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            WaveVisualizer(level = level, isSpeaking = transcriptionState.isSpeaking, active = isActive)

                            Box(modifier = Modifier.padding(vertical = 4.dp), contentAlignment = Alignment.Center) {
                                PttButton(
                                    active = isActive,
                                    isSpeaking = transcriptionState.isSpeaking,
                                    busy = busy,
                                    level = level,
                                    onPressIn = {
                                        if (!hasMicPermission) onRequestMicPermission()
                                        else transmitter.startPtt(context)
                                    },
                                    onPressOut = { transmitter.stopPtt() },
                                )
                            }

                            // Status text below mic — from real STT state only
                            val stateText = when {
                                modelLoading -> "Loading speech model…"
                                busy -> "Processing"
                                transcriptionState.isSpeaking -> "Speech detected"
                                isActive -> "Listening"
                                else -> null
                            }
                            if (stateText != null) {
                                Text(
                                    stateText,
                                    color = AppColor.TextMuted,
                                    fontSize = 12.sp,
                                    textAlign = TextAlign.Center,
                                )
                            }

                            // Last transcription result
                            val error = transcriptionState.error
                            val lastResult = transcriptionState.lastResult
                            when {
                                error != null -> Text(
                                    error,
                                    color = AppColor.Danger,
                                    fontSize = 13.sp,
                                    textAlign = TextAlign.Center,
                                    lineHeight = 19.sp,
                                )
                                lastResult != null -> Column(
                                    horizontalAlignment = Alignment.CenterHorizontally,
                                    verticalArrangement = Arrangement.spacedBy(4.dp),
                                ) {
                                    Text(
                                        lastResult.text,
                                        color = AppColor.Text,
                                        fontSize = 15.sp,
                                        lineHeight = 22.sp,
                                        textAlign = TextAlign.Center,
                                        fontWeight = FontWeight.Medium,
                                    )
                                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                                        Text(
                                            "${findLanguage(lastResult.language).label} · ${"%.1f".format(lastResult.durationMs / 1000)}s",
                                            color = AppColor.TextFaint,
                                            fontSize = 10.sp,
                                        )
                                        Text(
                                            "${lastResult.latencyMs} ms decode",
                                            color = if (lastResult.latencyMs < 1000) AppColor.Live else AppColor.Warn,
                                            fontSize = 10.sp,
                                            fontWeight = FontWeight.Bold,
                                        )
                                    }
                                }
                                else -> Text(
                                    if (modelLoading) "Speech model loading… hold the mic and speak, your audio will be sent once it is ready."
                                    else "Hold the mic and speak. Pause briefly or release to send.",
                                    color = AppColor.TextFaint,
                                    fontSize = 12.sp,
                                    lineHeight = 18.sp,
                                    textAlign = TextAlign.Center,
                                )
                            }
                        }

                        // ── SEND AS CRITICAL ─────────────────────────────────
                        CriticalToggle(
                            enabled = sendAsCritical,
                            onToggle = { transmitter.setSendAsCritical(!sendAsCritical) },
                        )

                        // ── MODEL STATUS ─────────────────────────────────────
                        ModelCard(
                            status = modelStatus,
                            label = activeModel.label,
                            sizeMb = activeModel.approxMb,
                            onInstall = { transmitter.installModel() },
                        )

                        // ── LAST TRANSMISSION ────────────────────────────────
                        TelemetryStrip(
                            latest = log.firstOrNull(),
                            pauseMs = pauseMs,
                            onPauseChange = { transmitter.setPauseMs(it) },
                        )

                        // ── PACKET LOG ───────────────────────────────────────
                        PacketLog(entries = log, onClear = { transmitter.clearLog() })
                    }
                }
            }
        }
    }
}

/**
 * Compact "Send as critical" row with a toggle switch on the right.
 *
 * Reference: a simple horizontal row, not a large card.
 */
@Composable
private fun CriticalToggle(enabled: Boolean, onToggle: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                if (enabled) AppColor.Danger.copy(alpha = 0.10f) else AppColor.Surface,
                RoundedCornerShape(AppRadius.md),
            )
            .border(
                1.dp,
                if (enabled) AppColor.Danger.copy(alpha = 0.45f) else AppColor.Hairline,
                RoundedCornerShape(AppRadius.md),
            )
            .clickable(onClick = onToggle)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            modifier = Modifier.weight(1f),
        ) {
            // Critical shield icon using boxes
            Box(
                modifier = Modifier
                    .size(18.dp)
                    .background(
                        if (enabled) AppColor.Danger else AppColor.TextFaint.copy(alpha = 0.3f),
                        RoundedCornerShape(4.dp),
                    ),
                contentAlignment = Alignment.Center,
            ) {
                Text("!", color = AppColor.Surface, fontSize = 10.sp, fontWeight = FontWeight.Black)
            }
            Column {
                Text(
                    "Send as critical",
                    color = if (enabled) AppColor.Danger else AppColor.TextMuted,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    if (enabled) "Every transmission is marked CRITICAL." else "Priority is set automatically from what you say.",
                    color = AppColor.TextFaint,
                    fontSize = 10.sp,
                    lineHeight = 14.sp,
                )
            }
        }
        Switch(
            checked = enabled,
            onCheckedChange = { onToggle() },
            colors = SwitchDefaults.colors(
                checkedThumbColor = AppColor.Surface,
                checkedTrackColor = AppColor.Danger,
                uncheckedThumbColor = AppColor.Surface,
                uncheckedTrackColor = AppColor.Hairline,
            ),
        )
    }
}
