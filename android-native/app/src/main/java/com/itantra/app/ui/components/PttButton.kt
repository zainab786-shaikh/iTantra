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
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.ui.theme.AppColor

private val SIZE = 132.dp

/**
 * Direct port of src/ui/components/PttButton.tsx.
 *
 * Press-and-hold rather than tap-to-toggle: matches radio muscle memory
 * and makes it impossible to leave the mic open by accident. Release
 * finalizes the utterance immediately. One ring pulses while active,
 * coupled to the live level.
 *
 * expo-haptics' impactAsync(Medium/Light) has no exact Compose equivalent;
 * HapticFeedbackType.LongPress/TextHandleMove are used on press/release as
 * the closest platform primitives, a documented judgment call in the same
 * spirit as Phase 3's AudioSource.MIC choice.
 */
@Composable
fun PttButton(
    active: Boolean,
    isSpeaking: Boolean,
    busy: Boolean,
    level: Float,
    onPressIn: () -> Unit,
    onPressOut: () -> Unit,
) {
    val haptics = LocalHapticFeedback.current
    var pressed by remember { mutableStateOf(false) }

    val press by animateFloatAsState(if (pressed) 1f else 0f, spring(stiffness = 320f), label = "press")
    val activeMix by animateFloatAsState(if (active) 1f else 0f, tween(280), label = "activeMix")
    val smoothLevel by animateFloatAsState(
        targetValue = level,
        animationSpec = spring(dampingRatio = Spring.DampingRatioNoBouncy, stiffness = 150f),
        label = "smoothLevel",
    )

    val transition = rememberInfiniteTransition(label = "ptt-ring")
    val ring by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(2400, easing = LinearEasing), RepeatMode.Restart),
        label = "ring",
    )

    val tint = if (isSpeaking) AppColor.Accent else AppColor.Primary
    val label = when {
        busy -> "PROCESSING"
        active -> "RELEASE TO SEND"
        else -> "HOLD TO SPEAK"
    }

    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(contentAlignment = Alignment.Center) {
            // Halo ring, pulsing outward while active.
            Box(
                modifier = Modifier
                    .size(SIZE)
                    .graphicsLayer {
                        val t = ring
                        alpha = (1 - t) * 0.45f * activeMix
                        val s = 1 + t * (0.7f + smoothLevel * 0.3f)
                        scaleX = s
                        scaleY = s
                    }
                    .border(1.5.dp, tint, CircleShape),
            )

            Box(
                modifier = Modifier
                    .size(SIZE)
                    .graphicsLayer {
                        val s = 1 - press * 0.06f + smoothLevel * 0.07f * activeMix
                        scaleX = s
                        scaleY = s
                    }
                    // Idle: solid Primary green. Active/speaking: Accent yellow.
                    .background(if (active) tint else AppColor.Primary, CircleShape)
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onPress = {
                                pressed = true
                                haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                onPressIn()
                                val released = tryAwaitRelease()
                                pressed = false
                                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                onPressOut()
                            }
                        )
                    },
                contentAlignment = Alignment.Center,
            ) {
                // Always white mic glyph — visible on both green and yellow backgrounds.
                MicGlyph(color = AppColor.Surface)
            }
        }

        Text(
            text = label,
            color = if (active) tint else AppColor.TextMuted,
            fontSize = 11.sp,
            letterSpacing = 2.4.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(top = 18.dp),
        )
    }
}

/** Mic pictogram drawn with plain Boxes, so the app carries no icon dependency - same as the source's view-based glyph. */
@Composable
private fun MicGlyph(color: androidx.compose.ui.graphics.Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(modifier = Modifier.size(17.dp, 28.dp).background(color, androidx.compose.foundation.shape.RoundedCornerShape(9.dp)))
        Box(
            modifier = Modifier
                .size(33.dp, 17.dp)
                .border(
                    2.5.dp,
                    color,
                    androidx.compose.foundation.shape.RoundedCornerShape(bottomStart = 17.dp, bottomEnd = 17.dp),
                ),
        )
        Box(modifier = Modifier.size(2.5.dp, 7.dp).background(color, androidx.compose.foundation.shape.RoundedCornerShape(2.dp)))
    }
}
