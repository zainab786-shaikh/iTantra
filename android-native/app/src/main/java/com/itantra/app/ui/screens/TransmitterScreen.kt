package com.itantra.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
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
import com.itantra.app.ui.components.WaveVisualizer
import com.itantra.app.ui.statusMeta
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.viewmodel.TransmitterViewModel

/**
 * Direct port of src/screens/TransmitterScreen.tsx to Jetpack Compose.
 *
 * Layout follows the operator's attention during a transmission: identity
 * and link state at the top, the live visualizer and status in the
 * middle, the PTT control under the thumb, and the log below the fold -
 * same structure, same content, same colours as the source.
 */
@Composable
fun TransmitterScreen(
    transmitter: TransmitterViewModel,
    hasMicPermission: Boolean,
    onRequestMicPermission: () -> Unit,
    /** Mode line under the wordmark, e.g. "TRANSMIT · OFFLINE". */
    linkNote: String,
    /**
     * The simulated link rate, or null when the throttle is off.
     *
     * On its own line and in the warning colour, deliberately. It has to be
     * visible whenever the throttle is on - the connection badge cannot
     * carry it, because both screens render the badge compact, which hides
     * its label - and appending it to [linkNote] made that line long enough
     * to either crush the badge or clip the rate itself. A separate line
     * also reads as what it is: a caveat, not a spec.
     */
    simNote: String?,
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
    // Phase 14.2: while the speech model loads in the background, say so rather than STANDBY.
    val modelLoading = sttReadiness == ModelReadiness.LOADING

    val status = if (modelLoading && transcriptionState.status == TransmitterStatus.IDLE) {
        statusMeta(TransmitterStatus.INITIALIZING).copy(label = "LOADING SPEECH MODEL")
    } else {
        statusMeta(transcriptionState.status)
    }
    val busy = transcriptionState.status == TransmitterStatus.TRANSCRIBING
    val activeModel = transmitter.activeModel

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
            // Identity + link
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // Weighted so the identity block yields to the badge instead of
                // starving it: the mode line grew when it took on the
                // simulated-rate suffix, and an unweighted Row squeezed
                // "LINK ACTIVE" down to one letter per line.
                Row(
                    modifier = Modifier.weight(1f, fill = false),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Image(
                        painter = painterResource(R.drawable.logo),
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.size(34.dp),
                    )
                    Column {
                        Text("iTantra", color = AppColor.Text, fontSize = 19.sp, fontWeight = FontWeight.Black)
                        Text(
                            linkNote,
                            color = AppColor.TextFaint,
                            fontSize = 8.5.sp,
                            letterSpacing = 1.5.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1,
                        )
                        if (simNote != null) {
                            Text(
                                simNote,
                                color = AppColor.Warn,
                                fontSize = 8.5.sp,
                                letterSpacing = 1.sp,
                                fontWeight = FontWeight.Bold,
                                maxLines = 1,
                            )
                        }
                    }
                }
                ConnectionBadge(connected = connected, label = transmitter.transportName, compact = true)
            }

            // Sender row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(AppColor.Surface, RoundedCornerShape(AppRadius.md))
                    .padding(horizontal = 12.dp, vertical = 9.dp),
            ) {
                Column {
                    Text("DEVICE", color = AppColor.TextFaint, fontSize = 8.sp, fontWeight = FontWeight.Black, letterSpacing = 1.1.sp)
                    Text(senderId, color = AppColor.TextMuted, fontSize = 9.5.sp, maxLines = 1)
                }
            }

            // Live stage
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(AppColor.Surface, RoundedCornerShape(AppRadius.xl))
                    .padding(horizontal = 16.dp, vertical = 18.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                    Box(modifier = Modifier.size(6.dp).background(status.color, CircleShape))
                    Text(status.label, color = status.color, fontSize = 10.sp, fontWeight = FontWeight.Black, letterSpacing = 1.8.sp)
                }

                WaveVisualizer(level = level, isSpeaking = transcriptionState.isSpeaking, active = isActive)

                Box(
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                    contentAlignment = Alignment.Center,
                ) {
                    val error = transcriptionState.error
                    val lastResult = transcriptionState.lastResult
                    when {
                        error != null -> Text(error, color = AppColor.Danger, fontSize = 13.sp, textAlign = TextAlign.Center, lineHeight = 19.sp)
                        lastResult != null -> Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(lastResult.text, color = AppColor.Text, fontSize = 17.sp, lineHeight = 25.sp, textAlign = TextAlign.Center, fontWeight = FontWeight.Medium)
                            // Decode latency belongs on screen, not just in a
                            // README: "offline STT in well under a second" is
                            // a claim the demo makes out loud, and this is the
                            // measurement behind it - wall-clock from
                            // end-of-speech to decoded text, for the utterance
                            // being shown.
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Text(
                                    "${findLanguage(lastResult.language).label} · " +
                                        "${"%.1f".format(lastResult.durationMs / 1000)}s audio",
                                    color = AppColor.TextFaint,
                                    fontSize = 10.sp,
                                    letterSpacing = 0.4.sp,
                                )
                                Text(
                                    "${lastResult.latencyMs} ms decode",
                                    color = if (lastResult.latencyMs < 1000) AppColor.Live else AppColor.Warn,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Black,
                                    letterSpacing = 0.4.sp,
                                )
                            }
                        }
                        else -> Text(
                            if (modelLoading) {
                                "Speech model loading… You can hold the mic now: what you say is kept and sent once it is ready."
                            } else {
                                "Hold the mic and speak. Pause briefly or release to send."
                            },
                            color = AppColor.TextFaint,
                            fontSize = 13.sp,
                            lineHeight = 19.sp,
                            textAlign = TextAlign.Center,
                        )
                    }
                }
            }

            // Priority override. Placed directly above the PTT because it
            // changes what pressing the PTT does, and an operator should not
            // have to remember a setting that lives elsewhere on the screen.
            CriticalToggle(
                enabled = sendAsCritical,
                onToggle = { transmitter.setSendAsCritical(!sendAsCritical) },
            )

            // Control
            Box(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
                PttButton(
                    active = isActive,
                    isSpeaking = transcriptionState.isSpeaking,
                    busy = busy,
                    level = level,
                    onPressIn = {
                        if (!hasMicPermission) {
                            onRequestMicPermission()
                        } else {
                            transmitter.startPtt(context)
                        }
                    },
                    onPressOut = { transmitter.stopPtt() },
                )
            }

            ModelCard(
                status = modelStatus,
                label = activeModel.label,
                sizeMb = activeModel.approxMb,
                onInstall = { transmitter.installModel() },
            )

            TelemetryStrip(
                latest = log.firstOrNull(),
                pauseMs = pauseMs,
                onPauseChange = { transmitter.setPauseMs(it) },
            )

            LanguageSelector(value = language, onChange = { transmitter.setLanguage(it) }, disabled = isActive)

            PacketLog(entries = log, onClear = { transmitter.clearLog() })
        }
    }
}

/**
 * Latching "send as critical" control.
 *
 * Deliberately loud when armed: it overrides the automatic classifier for
 * every subsequent transmission, and a mode that silently escalates traffic
 * is worse than one that is impossible to miss.
 */
@Composable
private fun CriticalToggle(enabled: Boolean, onToggle: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                if (enabled) AppColor.Danger.copy(alpha = 0.16f) else AppColor.Surface,
                RoundedCornerShape(AppRadius.md),
            )
            .border(
                1.dp,
                if (enabled) AppColor.Danger.copy(alpha = 0.65f) else AppColor.Hairline,
                RoundedCornerShape(AppRadius.md),
            )
            .clickable(onClick = onToggle)
            .padding(horizontal = 14.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Box(
            modifier = Modifier
                .size(16.dp)
                .background(
                    if (enabled) AppColor.Danger else androidx.compose.ui.graphics.Color.Transparent,
                    RoundedCornerShape(4.dp),
                )
                .border(
                    1.5.dp,
                    if (enabled) AppColor.Danger else AppColor.TextFaint,
                    RoundedCornerShape(4.dp),
                ),
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                "SEND AS CRITICAL",
                color = if (enabled) AppColor.Danger else AppColor.TextMuted,
                fontSize = 11.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.2.sp,
            )
            Text(
                if (enabled) {
                    "Every transmission is marked CRITICAL until switched off."
                } else {
                    "Priority is set automatically from what you say."
                },
                color = AppColor.TextFaint,
                fontSize = 10.5.sp,
                lineHeight = 14.sp,
            )
        }
        if (enabled) {
            Text(
                "ARMED",
                color = AppColor.Danger,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.sp,
            )
        }
    }
}
