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
import com.itantra.app.core.LogEntry
import com.itantra.app.packet.PRIORITY_COLORS
import com.itantra.app.transport.PacketCodec
import com.itantra.app.ui.hexColor
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import java.text.DateFormat
import java.util.Date
import java.util.Locale

/**
 * Direct port of src/ui/components/PacketLog.tsx. Rolling feed of sent
 * messages, newest first.
 */
@Composable
fun PacketLog(entries: List<LogEntry>, onClear: () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 2.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text("SENT", color = AppColor.TextFaint, fontSize = 10.sp, fontWeight = FontWeight.Black, letterSpacing = 1.8.sp)
            if (entries.isNotEmpty()) {
                Text(
                    "${entries.size} · CLEAR",
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

        if (entries.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.md))
                    .padding(vertical = 26.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text("No messages yet — hold the mic to transmit.", color = AppColor.TextFaint, fontSize = 12.sp)
            }
        } else {
            entries.forEach { entry ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(AppRadius.md))
                        .background(AppColor.Surface),
                ) {
                    Box(modifier = Modifier.width(3.dp).background(hexColor(PRIORITY_COLORS.getValue(entry.priority))))
                    Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                            Text(
                                entry.priority.value,
                                color = hexColor(PRIORITY_COLORS.getValue(entry.priority)),
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 1.sp,
                            )
                            Text(
                                findLanguage(entry.language).short,
                                color = hexColor(findLanguage(entry.language).accent),
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.6.sp,
                            )
                            Box(modifier = Modifier.weight(1f))
                            Text(
                                if (entry.delivered) "SENT" else "FAILED",
                                color = if (entry.delivered) AppColor.Live else AppColor.Danger,
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 0.6.sp,
                            )
                        }
                        Text(entry.text, color = AppColor.Text, fontSize = 14.sp, lineHeight = 20.sp)

                        // What this transmission actually cost. Read off the
                        // packet in hand, not estimated: the payload's real
                        // length, the source's real UTF-8 length, and the
                        // frame header's real size.
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            // Read off the log entry, not the packet:
                            // `packet §1.3` moved these sender-side facts off
                            // the wire and onto this side.
                            val original = entry.originalBytes
                            val payload = entry.packet.payload.size
                            val ratio = compressionRatio(original, payload)
                            Text(
                                "$original B → $payload B",
                                color = AppColor.TextMuted,
                                fontSize = 10.5.sp,
                                fontWeight = FontWeight.SemiBold,
                            )
                            if (ratio != null) {
                                Text(
                                    formatRatio(ratio),
                                    color = AppColor.Accent,
                                    fontSize = 10.5.sp,
                                    fontWeight = FontWeight.Black,
                                )
                            }
                            // The RAW / PACK7 / PHRASE chip stood here. Those
                            // modes are retired (`packet §1.4`); the tier that
                            // replaces them is inside the native payload and is
                            // not readable at this layer (`packet §1.1`).
                            // Phase 11 reinstates it as a tier chip.
                            // Stated rather than hidden: the frame carries a
                            // header, and on a 2-byte payload that is most of
                            // what goes out.
                            Text(
                                "+${PacketCodec.HEADER_BYTES} B hdr",
                                color = AppColor.TextFaint,
                                fontSize = 10.sp,
                            )
                            // The round-trip tick stood here. It decoded the
                            // payload back through the Kotlin codec that wrote
                            // it; that codec is no longer the producer, so the
                            // check would assert nothing. Phase 11 can restate
                            // it against the native decoder.
                        }

                        Text(
                            // Locale-aware, matching the source's
                            // Date.toLocaleTimeString() (device/locale
                            // 12h-vs-24h convention), not a fixed format.
                            DateFormat.getTimeInstance(DateFormat.MEDIUM, Locale.getDefault())
                                .format(Date(entry.packet.localTimestamp)) +
                                " · ${entry.latencyMs} ms decode",
                            color = AppColor.TextFaint,
                            fontSize = 10.sp,
                        )
                    }
                }
            }
        }
    }
}
