package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.stt.SttModelStatus
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing

/**
 * Direct port of src/ui/components/ModelCard.tsx, narrowed to the two
 * SttModelStatus states this migration can actually reach (Installed /
 * NotInstalled - no downloading/error/unsupported states exist without a
 * real HTTP install path, see Phase 9's SttModelStatus.kt).
 */
@Composable
fun ModelCard(
    status: SttModelStatus,
    label: String,
    sizeMb: Int,
    onInstall: () -> Unit,
) {
    val tint = if (status is SttModelStatus.Installed) AppColor.Live else AppColor.Warn

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
                text = if (status is SttModelStatus.Installed) "READY TO TRANSCRIBE" else "SETUP NEEDED",
                color = tint,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.4.sp,
                modifier = Modifier.weight(1f),
            )
        }

        androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 9.dp))

        Text(
            text = if (status is SttModelStatus.Installed) {
                "Speech recognition runs fully offline on this device."
            } else {
                "Offline speech recognition (~$sizeMb MB) isn't set up yet, so nothing can be transcribed. This needs internet once; after that the app works offline."
            },
            color = AppColor.TextMuted,
            fontSize = 11.5.sp,
            lineHeight = 17.sp,
        )

        if (status is SttModelStatus.NotInstalled) {
            androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 9.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .size(AppSizing.touchTarget)
                    .background(AppColor.Primary.copy(alpha = 0.10f), RoundedCornerShape(AppRadius.md))
                    .border(1.dp, AppColor.Primary.copy(alpha = 0.40f), RoundedCornerShape(AppRadius.md))
                    .clickable(onClick = onInstall),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "DOWNLOAD · $sizeMb MB",
                    color = AppColor.Primary,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Black,
                    letterSpacing = 1.2.sp,
                )
            }
        }
    }
}
