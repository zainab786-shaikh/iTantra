package com.itantra.app.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius

/**
 * Direct port of src/ui/components/ConnectionBadge.tsx.
 *
 * Link-state pill with a breathing dot. The dot animates only while
 * connected - a static dot on a dropped link is a clearer signal than a
 * pulsing one.
 */
@Composable
fun ConnectionBadge(
    connected: Boolean,
    label: String,
    compact: Boolean = false,
) {
    val color = if (connected) AppColor.Live else AppColor.Danger

    val transition = rememberInfiniteTransition(label = "connection-pulse")
    val pulse by if (connected) {
        transition.animateFloat(
            initialValue = 0f,
            targetValue = 1f,
            animationSpec = infiniteRepeatable(
                animation = tween(1500, easing = LinearEasing),
                repeatMode = RepeatMode.Reverse,
            ),
            label = "pulse",
        )
    } else {
        androidx.compose.runtime.remember { androidx.compose.runtime.mutableFloatStateOf(0f) }
    }

    Row(
        modifier = Modifier
            .background(AppColor.Surface, RoundedCornerShape(AppRadius.pill))
            .border(1.dp, color.copy(alpha = 0.27f), RoundedCornerShape(AppRadius.pill))
            .padding(horizontal = 12.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier.size(10.dp),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                modifier = Modifier
                    .size(16.dp)
                    .graphicsLayer {
                        alpha = 0.4f * (0.45f + pulse * 0.55f)
                        val s = 0.85f + pulse * 0.35f
                        scaleX = s
                        scaleY = s
                    }
                    .background(color, CircleShape)
            )
            Box(modifier = Modifier.size(7.dp).background(color, CircleShape))
        }

        androidx.compose.foundation.layout.Spacer(Modifier.width(8.dp))

        Text(
            text = if (connected) "LINK ACTIVE" else "LINK DOWN",
            color = color,
            fontSize = 10.sp,
            fontWeight = FontWeight.Black,
            letterSpacing = 1.3.sp,
        )

        if (!compact) {
            androidx.compose.foundation.layout.Spacer(Modifier.width(8.dp))
            Box(
                modifier = Modifier
                    .width(1.dp)
                    .height(11.dp)
                    .background(AppColor.HairlineStrong)
            )
            androidx.compose.foundation.layout.Spacer(Modifier.width(8.dp))
            Text(
                text = label,
                color = AppColor.TextFaint,
                fontSize = 10.sp,
                maxLines = 1,
            )
        }
    }
}
