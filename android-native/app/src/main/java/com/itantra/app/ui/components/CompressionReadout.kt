package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.codec.CodecMode
import com.itantra.app.ui.theme.AppColor

/**
 * The byte readout — what makes this a compression demo rather than a chat
 * app.
 *
 * Every value rendered here is measured at runtime from the actual packet in
 * hand: the payload's real length, the source text's real UTF-8 length, and
 * the frame header's real size. Nothing is a target, an estimate, or a
 * constant copied out of a design document.
 */

/** How a mode is coloured, so a PHRASE hit is visible across a room. */
fun modeColor(mode: CodecMode): Color = when (mode) {
    CodecMode.RAW -> AppColor.TextMuted
    CodecMode.PACK7 -> AppColor.Accent
    CodecMode.PHRASE -> AppColor.Primary
}

/**
 * Ratio of source size to payload size, or null when there is nothing to
 * divide by.
 *
 * An empty utterance encodes to an empty payload, and "∞×" on screen would
 * be a worse answer than no answer.
 */
fun compressionRatio(originalBytes: Int, payloadBytes: Int): Double? =
    if (payloadBytes <= 0 || originalBytes <= 0) null
    else originalBytes.toDouble() / payloadBytes

/** Large ratios do not need two decimals; small ones do. */
fun formatRatio(ratio: Double): String =
    if (ratio >= 10.0) "%.1f×".format(ratio) else "%.2f×".format(ratio)

/** A small mode chip: RAW / PACK7 / PHRASE. */
@Composable
fun ModeChip(mode: CodecMode, fontSize: Int = 9) {
    val color = modeColor(mode)
    Text(
        mode.name,
        color = color,
        fontSize = fontSize.sp,
        fontWeight = FontWeight.Black,
        letterSpacing = 0.8.sp,
        modifier = Modifier
            .background(color.copy(alpha = 0.12f), RoundedCornerShape(4.dp))
            .padding(horizontal = 5.dp, vertical = 2.dp),
    )
}

/**
 * The transmitter's headline readout: `105 B → 38 B   2.76×   PACK7`.
 *
 * [originalBytes] is the UTF-8 size of what was said; [payloadBytes] is what
 * actually went on the wire.
 */
@Composable
fun CompressionReadout(
    originalBytes: Int,
    payloadBytes: Int,
    mode: CodecMode,
) {
    val ratio = compressionRatio(originalBytes, payloadBytes)
    Row(
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "$originalBytes B",
            color = AppColor.TextMuted,
            fontSize = 15.sp,
            fontWeight = FontWeight.Medium,
        )
        Text("→", color = AppColor.TextFaint, fontSize = 14.sp)
        Text(
            "$payloadBytes B",
            color = AppColor.Text,
            fontSize = 19.sp,
            fontWeight = FontWeight.Black,
        )
        if (ratio != null) {
            Text(
                formatRatio(ratio),
                color = modeColor(mode),
                fontSize = 15.sp,
                fontWeight = FontWeight.Black,
            )
        }
        ModeChip(mode, fontSize = 10)
    }
}
