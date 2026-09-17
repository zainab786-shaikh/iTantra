package com.itantra.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.R
import com.itantra.app.tts.TtsPlaybackPhase
import com.itantra.app.ui.components.ConnectionBadge
import com.itantra.app.ui.components.CriticalAlertBanner
import com.itantra.app.ui.components.LanguageSelector
import com.itantra.app.ui.components.ReceivedMessageLog
import com.itantra.app.ui.components.ThemeMenu
import com.itantra.app.ui.components.TtsStatusCard
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.viewmodel.ReceiverViewModel
import kotlinx.coroutines.launch

/**
 * Receive screen — reference image phone 2.
 *
 * Structure:
 *   HEADER (logo + status + hamburger)
 *   HERO  (mountain illustration)
 *   TTS / critical alert
 *   LANGUAGE DROPDOWN
 *   MESSAGE LOG
 */
@Composable
fun ReceiverScreen(
    receiver: ReceiverViewModel,
    linkNote: String,
    simNote: String?,
    isDarkTheme: Boolean,
    onThemeToggle: (Boolean) -> Unit,
) {
    val messages by receiver.messages.collectAsState()
    val ttsState by receiver.ttsState.collectAsState()
    val voiceReadiness by receiver.voiceReadiness.collectAsState()
    val connected by receiver.connected.collectAsState()
    val language by receiver.language.collectAsState()
    val scope = rememberCoroutineScope()

    var installingLanguage by remember { mutableStateOf<String?>(null) }
    var installPercent by remember { mutableIntStateOf(0) }
    var voiceEpoch by remember { mutableIntStateOf(0) }

    fun installVoice(languageCode: String, replayPacketId: String?) {
        if (installingLanguage != null) return
        installingLanguage = languageCode
        installPercent = 0
        scope.launch {
            try {
                receiver.installVoice(languageCode) { percent, _ -> installPercent = percent }
                voiceEpoch++
                replayPacketId?.let { receiver.replay(it) }
            } catch (e: Exception) {
                // surfaced in the row's own status
            } finally {
                installingLanguage = null
            }
        }
    }

    val activeLangCode = when {
        installingLanguage != null -> installingLanguage!!
        ttsState.phase != TtsPlaybackPhase.IDLE && ttsState.language != null -> ttsState.language!!
        else -> language
    }
    val voiceStatus = remember(activeLangCode, voiceEpoch) { receiver.voiceStatus(activeLangCode) }

    val criticalActive = ttsState.isCritical &&
        (ttsState.phase == TtsPlaybackPhase.SPEAKING || ttsState.phase == TtsPlaybackPhase.LOADING_VOICE)

    val illustrationRes = if (isDarkTheme) R.drawable.mountain_illustration_dark else R.drawable.mountain_illustration_light

    // LEFT scrollbar: RTL on scroll container, LTR on content
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
                        ConnectionBadge(connected = connected, label = receiver.transportName, compact = true)
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

                    // ── CONTENT ──────────────────────────────────────────────
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = AppSizing.screenPadding)
                            .padding(top = 16.dp, bottom = 24.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        // Critical alert banner (only when active)
                        if (criticalActive) {
                            CriticalAlertBanner(
                                text = ttsState.text,
                                loading = ttsState.phase == TtsPlaybackPhase.LOADING_VOICE,
                            )
                        }

                        // TTS status / voice install
                        TtsStatusCard(
                            state = ttsState,
                            selectedLanguage = language,
                            voiceStatus = voiceStatus,
                            voiceLoading = voiceReadiness == com.itantra.app.core.ModelReadiness.LOADING,
                            onInstallVoice = { languageCode -> installVoice(languageCode, replayPacketId = null) },
                            installing = installingLanguage != null,
                            installPercent = installPercent,
                            installingLanguage = installingLanguage,
                        )

                        // Language dropdown — same position as Transmit screen
                        LanguageSelector(
                            value = language,
                            onChange = { receiver.setLanguage(it) },
                            disabled = installingLanguage != null,
                        )

                        // Message log
                        ReceivedMessageLog(
                            messages = messages,
                            onClear = { receiver.clearHistory() },
                            onReplay = { receiver.replay(it) },
                            voiceStatusFor = { code -> voiceEpoch.let { receiver.voiceStatus(code) } },
                            installingLanguage = installingLanguage,
                            installPercent = installPercent,
                            onInstallVoice = { code ->
                                val waiting = messages.firstOrNull { it.textLanguage == code }
                                installVoice(code, replayPacketId = waiting?.packet?.id)
                            },
                        )

                        Text(
                            "Messages play in the language they were sent — no translation.",
                            color = AppColor.TextFaint,
                            fontSize = 10.sp,
                            textAlign = TextAlign.Center,
                            lineHeight = 15.sp,
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 4.dp),
                        )
                    }
                }
            }
        }
    }
}
