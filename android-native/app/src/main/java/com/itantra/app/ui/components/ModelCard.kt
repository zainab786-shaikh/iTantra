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
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.stt.SttModelStatus
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing

/**
 * Direct port of src/ui/components/ModelCard.tsx. Install state of offline
 * speech recognition, and the button that fetches it - the most
 * consequential control on the screen: until it's installed the app
 * cannot transcribe at all.
 *
 * The source's `unsupported` state is not represented - it only ever
 * triggered in Expo Go / an unlinked RN build, which has no equivalent in
 * a native Kotlin build (the sherpa-onnx module is always linked).
 */
@Composable
fun ModelCard(
    status: SttModelStatus,
    label: String,
    sizeMb: Int,
    onInstall: () -> Unit,
) {
    val downloading = status is SttModelStatus.Downloading
    val percent = if (status is SttModelStatus.Downloading) status.percent else 0

    val barWidth by animateFloatAsState(
        targetValue = percent / 100f,
        animationSpec = tween(220),
        label = "modelCardProgress",
    )

    val tint = when (status) {
        is SttModelStatus.Installed -> AppColor.Live
        is SttModelStatus.Error -> AppColor.Danger
        else -> AppColor.Warn
    }

    val title = when (status) {
        is SttModelStatus.Installed -> "READY TO TRANSCRIBE"
        is SttModelStatus.Downloading ->
            "${if (status.phase == "extracting") "PREPARING" else "DOWNLOADING"} ${status.percent}%"
        is SttModelStatus.Error -> "SETUP ERROR"
        is SttModelStatus.NotInstalled -> "SETUP NEEDED"
    }

    val body = when (status) {
        is SttModelStatus.Installed -> "Speech recognition runs fully offline on this device."
        is SttModelStatus.Downloading -> "Preparing offline speech recognition. Keep the app open — this only happens once."
        is SttModelStatus.Error -> status.message
        is SttModelStatus.NotInstalled ->
            "Offline speech recognition (~$sizeMb MB) isn't set up yet, so nothing can be transcribed. This needs internet once; after that the app works offline."
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
            Text(
                text = title,
                color = tint,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.4.sp,
                modifier = Modifier.weight(1f),
            )
            if (downloading) {
                CircularProgressIndicator(modifier = Modifier.size(14.dp), color = tint, strokeWidth = 2.dp)
            }
        }

        androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 9.dp))

        Text(text = body, color = AppColor.TextMuted, fontSize = 11.5.sp, lineHeight = 17.sp)

        if (downloading) {
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

        if (status is SttModelStatus.NotInstalled || status is SttModelStatus.Error) {
            androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 2.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(AppSizing.touchTarget)
                    .background(AppColor.Primary.copy(alpha = 0.10f), RoundedCornerShape(AppRadius.md))
                    .border(1.dp, AppColor.Primary.copy(alpha = 0.40f), RoundedCornerShape(AppRadius.md))
                    .clickable(onClick = onInstall),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = if (status is SttModelStatus.Error) "RETRY DOWNLOAD" else "DOWNLOAD · $sizeMb MB",
                    color = AppColor.Primary,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 1.2.sp,
                )
            }
        }
    }
}
