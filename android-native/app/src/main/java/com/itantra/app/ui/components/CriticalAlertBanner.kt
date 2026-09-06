package com.itantra.app.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius

/**
 * Direct port of src/ui/components/CriticalAlertBanner.tsx. Full-width,
 * impossible-to-miss banner for a CRITICAL message. Only rendered while
 * one is active.
 */
@Composable
fun CriticalAlertBanner(text: String?, loading: Boolean) {
    val transition = rememberInfiniteTransition(label = "critical-pulse")
    val pulse by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(650, easing = LinearEasing), RepeatMode.Reverse),
        label = "pulse",
    )

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppColor.Danger.copy(alpha = 0.12f), RoundedCornerShape(AppRadius.lg))
            .border(1.5.dp, AppColor.Danger, RoundedCornerShape(AppRadius.lg))
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "⚠ CRITICAL ALERT",
            color = AppColor.Danger,
            fontSize = 13.sp,
            fontWeight = FontWeight.Black,
            letterSpacing = 2.sp,
            modifier = Modifier.graphicsLayer { alpha = 0.7f + pulse * 0.3f },
        )
        androidx.compose.foundation.layout.Spacer(Modifier.padding(3.dp))
        Text(
            text = if (loading) "Preparing alert audio…" else (text ?: ""),
            color = AppColor.Text,
            fontSize = 16.sp,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center,
            lineHeight = 22.sp,
            maxLines = 3,
        )
        androidx.compose.foundation.layout.Spacer(Modifier.padding(3.dp))
        Text(
            text = if (loading) "" else "🔊 PLAYING · cannot be interrupted",
            color = AppColor.Danger,
            fontSize = 10.sp,
            fontWeight = FontWeight.Black,
            letterSpacing = 1.sp,
        )
    }
}
