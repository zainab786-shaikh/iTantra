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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.R
import com.itantra.app.tts.TtsPlaybackPhase
import com.itantra.app.ui.components.ConnectionBadge
import com.itantra.app.ui.components.CriticalAlertBanner
import com.itantra.app.ui.components.LanguageSelector
import com.itantra.app.ui.components.ReceivedMessageLog
import com.itantra.app.ui.components.TtsStatusCard
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.viewmodel.ReceiverViewModel
import kotlinx.coroutines.launch

/**
 * Direct port of src/screens/ReceiverScreen.tsx to Jetpack Compose.
 * Mirrors the transmitter's layout (identity/link at top, live state in the
 * middle, history below) so the two screens read as one app.
 */
@Composable
fun ReceiverScreen(
    receiver: ReceiverViewModel,
    /** Mode line under the wordmark, e.g. "RECEIVE · OFFLINE". */
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
    val messages by receiver.messages.collectAsState()
    val ttsState by receiver.ttsState.collectAsState()
    val connected by receiver.connected.collectAsState()
    val language by receiver.language.collectAsState()
    val scope = rememberCoroutineScope()

    // Keyed by language rather than a bare boolean: a voice can now be
    // installed from a failed message row, which is for whatever language
    // that message was in - not necessarily the one selected below.
    var installingLanguage by remember { mutableStateOf<String?>(null) }
    var installPercent by remember { mutableIntStateOf(0) }
    // Bumped after an install so the per-row voice status is re-read from
    // disk; without it the freshly downloaded voice still reads as missing.
    var voiceEpoch by remember { mutableIntStateOf(0) }

    /**
     * Download a voice, then speak the message that was waiting on it.
     *
     * Replaying automatically is the point: the operator pressed download
     * *because* they wanted to hear that transmission, and on a critical
     * message a second manual tap is a second too long.
     */
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
                // Surfaced by the row's own status on the next render.
            } finally {
                installingLanguage = null
            }
        }
    }

    // Mirrors TtsStatusCard's own precedence so the status it is handed is
    // for the same language it is labelling.
    val activeLangCode = when {
        installingLanguage != null -> installingLanguage!!
        ttsState.phase != TtsPlaybackPhase.IDLE && ttsState.language != null -> ttsState.language!!
        else -> language
    }
    val voiceStatus = remember(activeLangCode, voiceEpoch) { receiver.voiceStatus(activeLangCode) }

    val criticalActive = ttsState.isCritical &&
        (ttsState.phase == TtsPlaybackPhase.SPEAKING || ttsState.phase == TtsPlaybackPhase.LOADING_VOICE)

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
                ConnectionBadge(connected = connected, label = receiver.transportName, compact = true)
            }

            if (criticalActive) {
                CriticalAlertBanner(text = ttsState.text, loading = ttsState.phase == TtsPlaybackPhase.LOADING_VOICE)
            }

            TtsStatusCard(
                state = ttsState,
                selectedLanguage = language,
                voiceStatus = voiceStatus,
                onInstallVoice = { languageCode -> installVoice(languageCode, replayPacketId = null) },
                installing = installingLanguage != null,
                installPercent = installPercent,
                installingLanguage = installingLanguage,
            )

            LanguageSelector(
                value = language,
                onChange = { receiver.setLanguage(it) },
                disabled = installingLanguage != null,
            )

            ReceivedMessageLog(
                messages = messages,
                onClear = { receiver.clearHistory() },
                onReplay = { receiver.replay(it) },
                voiceStatusFor = { code -> voiceEpoch.let { receiver.voiceStatus(code) } },
                installingLanguage = installingLanguage,
                installPercent = installPercent,
                onInstallVoice = { code ->
                    // Replay the newest message that was waiting on this voice.
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
