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
import com.itantra.app.ui.components.ReceivedMessageLog
import com.itantra.app.ui.components.TtsStatusCard
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.viewmodel.ReceiverViewModel
import kotlinx.coroutines.launch

/**
 * Direct port of src/screens/ReceiverScreen.tsx to Jetpack Compose. Mirrors
 * the transmitter's layout (identity/link at top, live state in the
 * middle, history below) so the two screens read as one app.
 */
@Composable
fun ReceiverScreen(receiver: ReceiverViewModel) {
    val messages by receiver.messages.collectAsState()
    val ttsState by receiver.ttsState.collectAsState()
    val connected by receiver.connected.collectAsState()
    val scope = rememberCoroutineScope()

    // Direct port of ReceiverScreen.tsx's local `installing`/`installPercent`
    // state - the source keeps this on the screen, not the controller, so
    // it is mirrored here rather than added to ReceiverViewModel.
    var installing by remember { mutableStateOf(false) }
    var installPercent by remember { mutableIntStateOf(0) }

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
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Image(
                        painter = painterResource(R.drawable.logo),
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.size(34.dp),
                    )
                    Column {
                        Text("iTantra", color = AppColor.Text, fontSize = 19.sp, fontWeight = FontWeight.Black)
                        Text("RECEIVE · OFFLINE", color = AppColor.TextFaint, fontSize = 8.5.sp, letterSpacing = 1.5.sp, fontWeight = FontWeight.Bold)
                    }
                }
                ConnectionBadge(connected = connected, label = receiver.transportName, compact = true)
            }

            if (criticalActive) {
                CriticalAlertBanner(text = ttsState.text, loading = ttsState.phase == TtsPlaybackPhase.LOADING_VOICE)
            }

            TtsStatusCard(
                state = ttsState,
                onInstallVoice = { languageCode ->
                    // Direct port of ReceiverScreen.tsx's handleInstallVoice():
                    // set installing/percent, await the install, and swallow
                    // failure here since TtsStatusCard already reflects it via
                    // ttsState.error on the next spoken attempt.
                    installing = true
                    installPercent = 0
                    scope.launch {
                        try {
                            receiver.installVoice(languageCode) { percent, _ -> installPercent = percent }
                        } catch (e: Exception) {
                            // Swallowed - see comment above.
                        } finally {
                            installing = false
                        }
                    }
                },
                installing = installing,
                installPercent = installPercent,
            )

            ReceivedMessageLog(
                messages = messages,
                onClear = { receiver.clearHistory() },
                onReplay = { receiver.replay(it) },
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
