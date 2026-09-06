package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.PAUSE_PRESETS
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.AppSizing

/** Direct port of src/ui/components/TelemetryStrip.tsx. End-of-speech pause control. */
@Composable
fun TelemetryStrip(pauseMs: Int, onPauseChange: (Int) -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.lg))
            .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.lg))
            .padding(horizontal = 14.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text("RESPONSE PAUSE", color = AppColor.TextMuted, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PAUSE_PRESETS.forEach { preset ->
                val selected = preset.ms == pauseMs
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(AppSizing.touchTarget)
                        .background(
                            if (selected) AppColor.Accent.copy(alpha = 0.12f) else AppColor.Surface,
                            RoundedCornerShape(AppRadius.sm),
                        )
                        .border(
                            1.dp,
                            if (selected) AppColor.Accent.copy(alpha = 0.53f) else AppColor.Hairline,
                            RoundedCornerShape(AppRadius.sm),
                        )
                        .clickable { onPauseChange(preset.ms) },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        preset.label,
                        color = if (selected) AppColor.AccentStrong else AppColor.TextMuted,
                        fontSize = 11.sp,
                        fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
                    )
                }
            }
        }
    }
}
