package com.itantra.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
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

    val status = statusMeta(transcriptionState.status)
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
                            Text(
                                "${findLanguage(lastResult.language).label} · ${"%.1f".format(lastResult.durationMs / 1000)}s",
                                color = AppColor.TextFaint,
                                fontSize = 10.sp,
                                letterSpacing = 0.4.sp,
                            )
                        }
                        else -> Text(
                            "Hold the mic and speak. Pause briefly or release to send.",
                            color = AppColor.TextFaint,
                            fontSize = 13.sp,
                            lineHeight = 19.sp,
                            textAlign = TextAlign.Center,
                        )
                    }
                }
            }

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
