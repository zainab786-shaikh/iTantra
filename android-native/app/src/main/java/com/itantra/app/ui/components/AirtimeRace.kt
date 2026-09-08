package com.itantra.app.ui.components

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.transport.FrameCost
import com.itantra.app.transport.airtimeMs
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import kotlinx.coroutines.launch

/**
 * Two bars racing across the screen: the same sentence transmitted
 * uncompressed, and as it was actually sent.
 *
 * A viewer has to interpret a byte count. They *feel* a race. Both are on
 * screen — the numbers above, the race here.
 *
 * Both bars carry the frame header, so this is like-for-like (see
 * [FrameCost]). The bar lengths are the real airtimes at the selected rate,
 * and the animation runs for exactly that long in real time; nothing is
 * scaled for effect.
 */
@Composable
fun AirtimeRace(
    cost: FrameCost?,
    bitsPerSecond: Int,
    enabled: Boolean,
    /** Changing this re-runs the animation, so the race can be replayed on camera. */
    runKey: Int,
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                "AIRTIME RACE",
                color = AppColor.TextMuted,
                fontSize = 11.sp,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                if (enabled) "SIMULATED LINK · $bitsPerSecond bps" else "THROTTLE OFF",
                color = if (enabled) AppColor.Warn else AppColor.TextFaint,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 0.6.sp,
            )
        }

        if (cost == null) {
            Text(
                "No message yet — transmit or receive one and it races here.",
                color = AppColor.TextFaint,
                fontSize = 11.5.sp,
            )
            return@Column
        }

        val rawMs = airtimeMs(cost.uncompressedFrameBytes, bitsPerSecond)
        val sentMs = airtimeMs(cost.sentFrameBytes, bitsPerSecond)
        // The slower of the two sets the full width, so the bars are directly
        // comparable rather than each being normalised to itself.
        val slowestMs = maxOf(rawMs, sentMs, 1L)

        val rawProgress = remember { Animatable(0f) }
        val sentProgress = remember { Animatable(0f) }

        LaunchedEffect(runKey, cost, bitsPerSecond, enabled) {
            rawProgress.snapTo(0f)
            sentProgress.snapTo(0f)
            if (!enabled) {
                // With no throttle there is no race to watch: both are
                // instant. Show the finished state rather than pretending.
                rawProgress.snapTo(1f)
                sentProgress.snapTo(1f)
                return@LaunchedEffect
            }
            launch {
                rawProgress.animateTo(1f, tween(rawMs.toInt().coerceAtLeast(1), easing = LinearEasing))
            }
            launch {
                sentProgress.animateTo(1f, tween(sentMs.toInt().coerceAtLeast(1), easing = LinearEasing))
            }
        }

        RaceBar(
            label = "UNCOMPRESSED",
            detail = "${cost.uncompressedFrameBytes} B",
            seconds = rawMs / 1000.0,
            fraction = rawProgress.value,
            widthShare = rawMs.toFloat() / slowestMs,
            color = AppColor.TextMuted,
        )
        RaceBar(
            label = cost.mode.name,
            detail = "${cost.sentFrameBytes} B",
            seconds = sentMs / 1000.0,
            fraction = sentProgress.value,
            widthShare = sentMs.toFloat() / slowestMs,
            color = modeColor(cost.mode),
        )

        val saved = rawMs - sentMs
        Text(
            when {
                !enabled -> "Enable the throttle to see the difference in time."
                saved > 0 -> "Arrives %.2f s sooner.".format(saved / 1000.0)
                saved < 0 -> "Arrives %.2f s later — the header costs more than this message saves."
                    .format(-saved / 1000.0)
                else -> "No difference for this message."
            },
            color = if (saved > 0) AppColor.Live else AppColor.TextFaint,
            fontSize = 11.5.sp,
        )
        Text(
            "Both bars include the ${cost.headerBytes} B frame header. " +
                "Airtime = bytes × 8 ÷ bitrate. This is a rate limiter, not a radio.",
            color = AppColor.TextFaint,
            fontSize = 10.sp,
            lineHeight = 14.sp,
        )
    }
}

@Composable
private fun RaceBar(
    label: String,
    detail: String,
    seconds: Double,
    fraction: Float,
    widthShare: Float,
    color: Color,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(label, color = color, fontSize = 10.sp, fontWeight = FontWeight.Black, letterSpacing = 0.8.sp)
            Text(
                "$detail · %.2f s".format(seconds),
                color = AppColor.TextFaint,
                fontSize = 10.sp,
            )
        }
        // A race runs left to right, so the bar must grow from the left edge -
        // hence CenterStart on the track and a width fraction on the fill.
        // The fill is scaled by this message's share of the SLOWEST airtime,
        // so a quick message is visibly short even once it has finished,
        // rather than each bar being normalised to its own duration.
        val fill = (widthShare.coerceIn(0f, 1f) * fraction).coerceIn(0f, 1f)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(14.dp)
                .clip(RoundedCornerShape(AppRadius.sm))
                .background(AppColor.Abyss),
            contentAlignment = Alignment.CenterStart,
        ) {
            if (fill > 0f) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(fill)
                        .fillMaxHeight()
                        .background(color.copy(alpha = 0.85f)),
                )
            }
        }
    }
}
