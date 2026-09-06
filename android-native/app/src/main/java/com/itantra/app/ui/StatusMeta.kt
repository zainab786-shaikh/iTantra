package com.itantra.app.ui

import androidx.compose.ui.graphics.Color
import com.itantra.app.core.TransmitterStatus
import com.itantra.app.ui.theme.AppColor

/** Direct port of STATUS_META in src/ui/theme.ts. Status -> label + colour, shared by the badge and the halo. */
data class StatusMeta(val label: String, val color: Color)

fun statusMeta(status: TransmitterStatus): StatusMeta = when (status) {
    TransmitterStatus.IDLE -> StatusMeta("STANDBY", AppColor.TextMuted)
    TransmitterStatus.INITIALIZING -> StatusMeta("PREPARING", AppColor.Warn)
    TransmitterStatus.LISTENING -> StatusMeta("LISTENING", AppColor.Primary)
    TransmitterStatus.SPEAKING -> StatusMeta("SPEECH DETECTED", AppColor.Live)
    TransmitterStatus.TRANSCRIBING -> StatusMeta("PROCESSING", AppColor.Accent)
    TransmitterStatus.ERROR -> StatusMeta("ERROR", AppColor.Danger)
}

/** Parse the hex colour strings src/core/packet/priority.ts and config/languages.ts carry (e.g. "#E66B67"). */
fun hexColor(hex: String): Color = Color(android.graphics.Color.parseColor(hex))
