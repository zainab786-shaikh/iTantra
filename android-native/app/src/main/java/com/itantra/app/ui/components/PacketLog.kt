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
import com.itantra.app.ui.hexColor
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import java.text.SimpleDateFormat
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
                    Box(modifier = Modifier.width(3.dp).background(hexColor(PRIORITY_COLORS.getValue(entry.packet.priority))))
                    Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
                            Text(
                                entry.packet.priority.value,
                                color = hexColor(PRIORITY_COLORS.getValue(entry.packet.priority)),
                                fontSize = 9.sp,
                                fontWeight = FontWeight.Black,
                                letterSpacing = 1.sp,
                            )
                            Text(
                                findLanguage(entry.packet.language).short,
                                color = hexColor(findLanguage(entry.packet.language).accent),
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
                        Text(entry.packet.text, color = AppColor.Text, fontSize = 14.sp, lineHeight = 20.sp)
                        Text(
                            SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(entry.packet.timestamp)),
                            color = AppColor.TextFaint,
                            fontSize = 10.sp,
                        )
                    }
                }
            }
        }
    }
}
