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

/**
 * The colour the payload figure is drawn in.
 *
 * This used to be `modeColor(CodecMode)` — RAW / PACK7 / PHRASE each had their
 * own hue so a PHRASE hit was visible across a room. Those modes are retired
 * (`packet §1.4`), and the concept that replaces them is the **tier**, which
 * lives inside the native payload and is not readable at this layer until the
 * native receive pipeline lands in Phase 11. One accent until then, rather
 * than a chip asserting something this build cannot know.
 */
private val payloadColor: Color get() = AppColor.Accent

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

/**
 * The transmitter's headline readout: `105 B → 38 B   2.76×`.
 *
 * [originalBytes] is the UTF-8 size of what was said (M-01); [payloadBytes] is
 * what actually went on the wire. Both are sender-side figures now —
 * `packet §1.3` took `originalBytes` off the link, so only the sender can
 * state it, and it does (see `packet.BuiltPacket`).
 *
 * Note for Phase 13: this is *not* M-05. `contract §6.1` requires the quoted
 * compression ratio to be M-01 ÷ M-04 — the **complete** ITantraPacket
 * including the outer frame and the AEAD tag — because "quoting M-01 ÷ M-02
 * overstates the achieved compression by excluding the tag and outer frame,
 * which together can exceed the payload on short messages". This readout is
 * the payload-level figure and must be labelled as such wherever it is
 * reported as a measurement.
 */
@Composable
fun CompressionReadout(
    originalBytes: Int,
    payloadBytes: Int,
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
                color = payloadColor,
                fontSize = 15.sp,
                fontWeight = FontWeight.Black,
            )
        }
    }
}
