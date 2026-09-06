package com.itantra.app.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.dp
import com.itantra.app.ui.theme.AppColor
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin

private const val BAR_COUNT = 27
private val BAR_WIDTH = 4.dp
private val MAX_HEIGHT = 64.dp
private val MIN_HEIGHT = 4.dp

/**
 * Direct port of src/ui/components/WaveVisualizer.tsx. Symmetric bar
 * spectrum. Each bar mixes a fixed bell envelope (middle tallest), the
 * live level, and a per-bar travelling wave so the display stays alive
 * during steady tones.
 */
@Composable
fun WaveVisualizer(level: Float, isSpeaking: Boolean, active: Boolean) {
    val transition = rememberInfiniteTransition(label = "wave-clock")
    val clock by transition.animateFloat(
        initialValue = 0f,
        targetValue = (PI * 2).toFloat(),
        animationSpec = infiniteRepeatable(tween(2200, easing = LinearEasing)),
        label = "clock",
    )

    val speakingMix by animateFloatAsState(if (isSpeaking) 1f else 0f, tween(260), label = "speakingMix")
    val activeMix by animateFloatAsState(if (active) 1f else 0f, tween(320), label = "activeMix")
    val smoothed by animateFloatAsState(
        targetValue = level,
        animationSpec = spring(dampingRatio = Spring.DampingRatioNoBouncy, stiffness = 170f),
        label = "smoothedLevel",
    )

    Row(
        modifier = Modifier.fillMaxWidth().height(MAX_HEIGHT),
        horizontalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val centre = (BAR_COUNT - 1) / 2f
        for (index in 0 until BAR_COUNT) {
            val distance = abs(index - centre) / centre
            val envelope = cos((distance * PI) / 2).pow(1.6).toFloat()
            val phase = index * 0.45f

            val wobble = 0.62f + 0.38f * sin(clock * 2 + phase)
            val idleBreath = 0.06f + 0.05f * sin(clock + phase * 0.6f)

            val driven = smoothed * envelope * wobble * activeMix
            val resting = idleBreath * envelope * (0.25f + 0.35f * activeMix)
            val amplitude = max(driven, resting)

            val barHeight = MIN_HEIGHT + (MAX_HEIGHT - MIN_HEIGHT) * amplitude
            val color = lerp(AppColor.Primary, AppColor.Accent, speakingMix)
            val opacity = 0.3f + 0.6f * min(1f, amplitude * 2.2f + 0.25f)

            androidx.compose.foundation.layout.Box(
                modifier = Modifier
                    .width(BAR_WIDTH)
                    .height(barHeight)
                    .background(color.copy(alpha = opacity), RoundedCornerShape(BAR_WIDTH / 2)),
            )
        }
    }
}
