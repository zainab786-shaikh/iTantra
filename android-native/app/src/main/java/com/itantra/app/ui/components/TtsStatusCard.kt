package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.findLanguage
import com.itantra.app.tts.TtsPlaybackPhase
import com.itantra.app.tts.TtsPlaybackState
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing
import kotlinx.coroutines.delay

/**
 * Direct port of src/ui/components/TtsStatusCard.tsx. Receiver's speech
 * status - the mirror of the transmitter's ModelCard, but describing
 * playback rather than decode.
 */
@Composable
fun TtsStatusCard(
    state: TtsPlaybackState,
    onInstallVoice: (String) -> Unit,
    installing: Boolean,
    installPercent: Int,
) {
    var dots by remember { mutableStateOf("") }
    LaunchedEffect(state.phase) {
        if (state.phase != TtsPlaybackPhase.LOADING_VOICE && state.phase != TtsPlaybackPhase.SPEAKING) {
            dots = ""
            return@LaunchedEffect
        }
        while (true) {
            delay(400)
            dots = if (dots.length >= 3) "" else "$dots."
        }
    }

    val lang = state.language?.let { findLanguage(it) }
    val missingVoice = state.phase == TtsPlaybackPhase.ERROR &&
        (state.error?.lowercase()?.contains("install the required language pack") == true)

    val tint = when {
        state.isCritical -> AppColor.Danger
        state.phase == TtsPlaybackPhase.SPEAKING -> AppColor.Live
        state.phase == TtsPlaybackPhase.ERROR -> AppColor.Danger
        state.phase == TtsPlaybackPhase.LOADING_VOICE -> AppColor.Info
        else -> AppColor.TextFaint
    }

    val title = when {
        state.isCritical -> "CRITICAL ALERT"
        state.phase == TtsPlaybackPhase.SPEAKING -> "SPEAKING"
        state.phase == TtsPlaybackPhase.LOADING_VOICE -> "PREPARING VOICE"
        state.phase == TtsPlaybackPhase.ERROR -> "SPEECH UNAVAILABLE"
        else -> "READY"
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, tint.copy(alpha = 0.27f), RoundedCornerShape(AppRadius.lg))
            .padding(13.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(7.dp).background(tint, CircleShape))
            androidx.compose.foundation.layout.Spacer(Modifier.size(8.dp, 0.dp))
            Text(title, color = tint, fontSize = 10.sp, fontWeight = FontWeight.Black, letterSpacing = 1.4.sp, modifier = Modifier.weight(1f))
            if (state.phase == TtsPlaybackPhase.SPEAKING || state.phase == TtsPlaybackPhase.LOADING_VOICE) {
                CircularProgressIndicator(modifier = Modifier.size(14.dp), color = tint, strokeWidth = 2.dp)
            }
        }

        androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 8.dp))

        when (state.phase) {
            TtsPlaybackPhase.IDLE -> {
                Text(
                    "Listening for incoming transmissions. Speech will play automatically.",
                    color = AppColor.Text,
                    fontSize = 14.sp,
                    lineHeight = 20.sp,
                )
            }
            TtsPlaybackPhase.LOADING_VOICE, TtsPlaybackPhase.SPEAKING -> {
                Text(state.text ?: "", color = AppColor.Text, fontSize = 14.sp, lineHeight = 20.sp, maxLines = 2)
                Text(
                    (lang?.label ?: state.language ?: "") +
                        (if (state.phase == TtsPlaybackPhase.LOADING_VOICE) " · preparing voice$dots" else ""),
                    color = AppColor.TextFaint,
                    fontSize = 10.5.sp,
                )
            }
            TtsPlaybackPhase.ERROR -> {
                Text(state.error ?: "", color = AppColor.TextMuted, fontSize = 12.sp, lineHeight = 17.sp)
                if (missingVoice && state.language != null) {
                    androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 2.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(AppSizing.touchTarget)
                            .background(AppColor.Primary.copy(alpha = 0.10f), RoundedCornerShape(AppRadius.md))
                            .border(1.dp, AppColor.Primary.copy(alpha = 0.40f), RoundedCornerShape(AppRadius.md))
                            .clickable(enabled = !installing) { onInstallVoice(state.language) },
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(
                            if (installing) "INSTALLING · $installPercent%" else "INSTALL ${lang?.label?.uppercase() ?: "VOICE"} PACK",
                            color = AppColor.Primary,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Black,
                            letterSpacing = 1.2.sp,
                        )
                    }
                }
            }
        }
    }
}
