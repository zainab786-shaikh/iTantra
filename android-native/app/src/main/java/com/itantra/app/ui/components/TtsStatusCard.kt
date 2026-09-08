package com.itantra.app.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.findLanguage
import com.itantra.app.config.resolveTtsModelForLanguage
import com.itantra.app.tts.TtsPlaybackPhase
import com.itantra.app.tts.TtsPlaybackState
import com.itantra.app.tts.TtsVoiceStatus
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing
import kotlinx.coroutines.delay

/**
 * Receiver's speech status card — mirrors the transmitter's ModelCard.
 * Displays live speech playback, missing voice setup indicators, and a one-tap
 * download button for offline TTS voice packs.
 */
@Composable
fun TtsStatusCard(
    state: TtsPlaybackState,
    selectedLanguage: String = "en-IN",
    voiceStatus: TtsVoiceStatus? = null,
    onInstallVoice: (String) -> Unit,
    installing: Boolean,
    installPercent: Int,
    /**
     * The language actually being downloaded, when one is.
     *
     * Needed because a download can now be started from a failed message row,
     * for whatever language that message was in - which is usually NOT the
     * selected one. Without this the card cheerfully reported "downloading
     * English, 64.1 MB" while fetching a 114 MB Odia voice.
     */
    installingLanguage: String? = null,
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

    // Target language, most specific first: whatever is being downloaded
    // right now, else the language of the active playback or error, else the
    // one the operator has selected.
    val activeLangCode = when {
        installingLanguage != null -> installingLanguage
        state.phase != TtsPlaybackPhase.IDLE && state.language != null -> state.language
        else -> selectedLanguage
    }

    val lang = findLanguage(activeLangCode)
    val modelDescriptor = resolveTtsModelForLanguage(activeLangCode)
    val approxMb = modelDescriptor?.approxMb?.let { "%.1f".format(it) } ?: "64.1"

    val isVoiceMissing = voiceStatus == null ||
        voiceStatus is TtsVoiceStatus.NotInstalled ||
        voiceStatus is TtsVoiceStatus.Error ||
        (state.phase == TtsPlaybackPhase.ERROR &&
            (state.error?.lowercase()?.contains("install") == true ||
             state.error?.lowercase()?.contains("unavailable") == true ||
             state.error?.lowercase()?.contains("voice") == true ||
             state.error?.lowercase()?.contains("model") == true))

    val isDownloading = installing || voiceStatus is TtsVoiceStatus.Downloading
    val currentPercent = if (installing) installPercent else if (voiceStatus is TtsVoiceStatus.Downloading) voiceStatus.percent else 0

    val barWidth by animateFloatAsState(
        targetValue = currentPercent / 100f,
        animationSpec = tween(220),
        label = "ttsModelProgress",
    )

    val tint = when {
        state.isCritical -> AppColor.Danger
        state.phase == TtsPlaybackPhase.SPEAKING -> AppColor.Live
        state.phase == TtsPlaybackPhase.LOADING_VOICE -> AppColor.Info
        isDownloading -> AppColor.Warn
        state.phase == TtsPlaybackPhase.ERROR -> AppColor.Danger
        isVoiceMissing -> AppColor.Warn
        else -> AppColor.Live
    }

    val title = when {
        state.isCritical -> "CRITICAL ALERT"
        state.phase == TtsPlaybackPhase.SPEAKING -> "SPEAKING"
        state.phase == TtsPlaybackPhase.LOADING_VOICE -> "PREPARING VOICE"
        isDownloading -> "DOWNLOADING VOICE PACK $currentPercent%"
        state.phase == TtsPlaybackPhase.ERROR -> "SPEECH UNAVAILABLE"
        isVoiceMissing -> "SETUP NEEDED"
        else -> "READY FOR SPEECH PLAYBACK"
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
            if (state.phase == TtsPlaybackPhase.SPEAKING || state.phase == TtsPlaybackPhase.LOADING_VOICE || isDownloading) {
                CircularProgressIndicator(modifier = Modifier.size(14.dp), color = tint, strokeWidth = 2.dp)
            }
        }

        androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 8.dp))

        when {
            isDownloading -> {
                Text(
                    "Downloading offline voice pack for ${lang.label} (~$approxMb MB). Keep the app open — this only happens once.",
                    color = AppColor.TextMuted,
                    fontSize = 11.5.sp,
                    lineHeight = 17.sp,
                )
                androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 9.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(4.dp)
                        .clip(RoundedCornerShape(2.dp))
                        .background(AppColor.Hairline),
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(barWidth)
                            .height(4.dp)
                            .background(AppColor.Primary, RoundedCornerShape(2.dp)),
                    )
                }
            }
            state.phase == TtsPlaybackPhase.LOADING_VOICE || state.phase == TtsPlaybackPhase.SPEAKING -> {
                Text(state.text ?: "", color = AppColor.Text, fontSize = 14.sp, lineHeight = 20.sp, maxLines = 2)
                Text(
                    lang.label + (if (state.phase == TtsPlaybackPhase.LOADING_VOICE) " · preparing voice$dots" else ""),
                    color = AppColor.TextFaint,
                    fontSize = 10.5.sp,
                )
            }
            state.phase == TtsPlaybackPhase.ERROR -> {
                Text(state.error ?: "Speech error", color = AppColor.TextMuted, fontSize = 12.sp, lineHeight = 17.sp)
                if (isVoiceMissing) {
                    androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 8.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(AppSizing.touchTarget)
                            .background(AppColor.Primary.copy(alpha = 0.10f), RoundedCornerShape(AppRadius.md))
                            .border(1.dp, AppColor.Primary.copy(alpha = 0.40f), RoundedCornerShape(AppRadius.md))
                            .clickable(enabled = !installing) { onInstallVoice(activeLangCode) },
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(
                            "DOWNLOAD ${lang.label.uppercase()} VOICE PACK · $approxMb MB",
                            color = AppColor.Primary,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Black,
                            letterSpacing = 1.2.sp,
                        )
                    }
                }
            }
            isVoiceMissing -> {
                Text(
                    "Offline TTS voice pack (~$approxMb MB) for ${lang.label} isn't set up yet, so incoming transmissions in ${lang.label} cannot be spoken. Download it once for offline playback.",
                    color = AppColor.TextMuted,
                    fontSize = 11.5.sp,
                    lineHeight = 17.sp,
                )
                androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 8.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(AppSizing.touchTarget)
                        .background(AppColor.Primary.copy(alpha = 0.10f), RoundedCornerShape(AppRadius.md))
                        .border(1.dp, AppColor.Primary.copy(alpha = 0.40f), RoundedCornerShape(AppRadius.md))
                        .clickable(enabled = !installing) { onInstallVoice(activeLangCode) },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        "DOWNLOAD ${lang.label.uppercase()} VOICE PACK · $approxMb MB",
                        color = AppColor.Primary,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 1.2.sp,
                    )
                }
            }
            else -> { // IDLE and Installed
                Text(
                    "Listening for incoming transmissions. ${lang.label} voice pack is ready for offline playback.",
                    color = AppColor.Text,
                    fontSize = 13.sp,
                    lineHeight = 19.sp,
                )
            }
        }
    }
}
