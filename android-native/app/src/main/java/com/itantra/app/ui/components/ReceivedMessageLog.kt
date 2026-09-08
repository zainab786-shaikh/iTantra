package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.findLanguage
import com.itantra.app.packet.PRIORITY_COLORS
import com.itantra.app.packet.PacketPriority
import com.itantra.app.receiver.ReceivedMessage
import com.itantra.app.receiver.ReceivedMessageState
import com.itantra.app.ui.hexColor
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import java.text.DateFormat
import java.util.Date
import java.util.Locale

private fun stateLabel(state: ReceivedMessageState): String = when (state) {
    ReceivedMessageState.RECEIVED -> "RECEIVED"
    ReceivedMessageState.QUEUED -> "QUEUED"
    ReceivedMessageState.SPEAKING -> "SPEAKING"
    ReceivedMessageState.SPOKEN -> "SPOKEN"
    ReceivedMessageState.ERROR -> "ERROR"
}

/**
 * Direct port of src/ui/components/ReceivedMessageLog.tsx. Rolling feed of
 * received messages, newest first - the receiver's mirror of the
 * transmitter's PacketLog.
 */
@Composable
fun ReceivedMessageLog(
    messages: List<ReceivedMessage>,
    onClear: () -> Unit,
    onReplay: (String) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 2.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text("RECEIVED", color = AppColor.TextFaint, fontSize = 10.sp, fontWeight = FontWeight.Black, letterSpacing = 1.8.sp)
            if (messages.isNotEmpty()) {
                Text(
                    "${messages.size} · CLEAR",
                    color = AppColor.TextFaint,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.sp,
                    modifier = Modifier.clickable(onClick = onClear),
                )
            } else {
                Text("EMPTY", color = AppColor.TextFaint, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            }
        }

        if (messages.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.md))
                    .padding(vertical = 26.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text("No messages yet — received transmissions appear here.", color = AppColor.TextFaint, fontSize = 12.sp)
            }
        } else {
            messages.forEach { m ->
                val critical = m.packet.priority == PacketPriority.CRITICAL
                val speaking = m.state == ReceivedMessageState.SPEAKING

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(AppRadius.md))
                        .background(if (critical) AppColor.Danger.copy(alpha = 0.08f) else AppColor.Surface)
                        .border(
                            1.dp,
                            when {
                                critical -> AppColor.Danger.copy(alpha = 0.4f)
                                speaking -> AppColor.Live.copy(alpha = 0.4f)
                                else -> androidx.compose.ui.graphics.Color.Transparent
                            },
                            RoundedCornerShape(AppRadius.md),
                        ),
                ) {
                    Box(modifier = Modifier.width(3.dp).background(hexColor(PRIORITY_COLORS.getValue(m.packet.priority))))
                    Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                            Text(
                                m.packet.priority.value,
                                color = hexColor(PRIORITY_COLORS.getValue(m.packet.priority)),
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 1.sp,
                            )
                            Text(
                                findLanguage(m.textLanguage).short,
                                color = hexColor(findLanguage(m.textLanguage).accent),
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.6.sp,
                            )
                            Box(modifier = Modifier.weight(1f))
                            Text(
                                (if (speaking) "🔊 " else "") + stateLabel(m.state),
                                color = when {
                                    speaking -> AppColor.Live
                                    m.state == ReceivedMessageState.ERROR -> AppColor.Danger
                                    else -> AppColor.TextFaint
                                },
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.6.sp,
                            )
                        }

                        Text(m.text, color = AppColor.Text, fontSize = 14.sp, lineHeight = 20.sp)

                        // The receiver never sees the original text - only
                        // the payload that arrived. So both figures here are
                        // measured locally: the bytes actually received, and
                        // the UTF-8 size of what they reconstructed into.
                        // Nothing is taken on trust from the far end.
                        // Keyed on there being decoded text, NOT on the row's
                        // state. ERROR is also how a TTS playback failure is
                        // reported - a missing voice pack, say - and in that
                        // case the bytes arrived and decoded perfectly well.
                        // Hiding the readout there would understate what the
                        // link actually achieved.
                        if (m.text.isNotEmpty()) {
                            val payload = m.packet.payload.size
                            val rebuilt = m.text.toByteArray(Charsets.UTF_8).size
                            val ratio = compressionRatio(rebuilt, payload)
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                            ) {
                                Text(
                                    "$payload B → $rebuilt B",
                                    color = AppColor.TextMuted,
                                    fontSize = 10.5.sp,
                                    fontWeight = FontWeight.SemiBold,
                                )
                                if (ratio != null) {
                                    Text(
                                        formatRatio(ratio),
                                        color = modeColor(m.packet.mode),
                                        fontSize = 10.5.sp,
                                        fontWeight = FontWeight.Black,
                                    )
                                }
                                ModeChip(m.packet.mode)

                                // Only PHRASE crosses languages, and when it
                                // does this is the whole point: an id went
                                // over the link and came out as different
                                // words in a different script.
                                if (m.textLanguage != m.packet.language) {
                                    Text(
                                        "${findLanguage(m.packet.language).short} → " +
                                            findLanguage(m.textLanguage).short,
                                        color = AppColor.Primary,
                                        fontSize = 10.sp,
                                        fontWeight = FontWeight.Black,
                                        letterSpacing = 0.6.sp,
                                    )
                                }
                            }
                        }

                        if (m.state == ReceivedMessageState.ERROR && m.error != null) {
                            Text(m.error, color = AppColor.Danger, fontSize = 11.5.sp, lineHeight = 16.sp)
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                // Locale-aware, matching the source's
                                // Date.toLocaleTimeString().
                                DateFormat.getTimeInstance(DateFormat.MEDIUM, Locale.getDefault())
                                    .format(Date(m.packet.timestamp)) + " · ${m.packet.senderId}",
                                color = AppColor.TextFaint,
                                fontSize = 10.sp,
                            )
                            Text(
                                "REPLAY",
                                color = AppColor.Primary,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.8.sp,
                                modifier = Modifier.clickable { onReplay(m.packet.id) },
                            )
                        }
                    }
                }
            }
        }
    }
}
