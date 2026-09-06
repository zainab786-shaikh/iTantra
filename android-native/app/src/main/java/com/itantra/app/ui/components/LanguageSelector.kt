package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.LANGUAGES
import com.itantra.app.config.Language
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius

/**
 * Direct port of src/ui/components/LanguageSelector.tsx.
 *
 * Horizontal language rail. Each chip carries the language's own script.
 * The selected chip uses Signal Yellow (theme.color.accent), the design
 * system's one selection colour - not each language's own accent hue.
 */
@Composable
fun LanguageSelector(
    value: String,
    onChange: (String) -> Unit,
    disabled: Boolean = false,
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 2.dp, vertical = 0.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                "DECODE LANGUAGE",
                color = AppColor.TextFaint,
                fontSize = 10.sp,
                fontWeight = FontWeight.Black,
                letterSpacing = 1.8.sp,
            )
            Text(
                "${LANGUAGES.size} available",
                color = AppColor.TextFaint,
                fontSize = 10.sp,
            )
        }

        androidx.compose.foundation.layout.Spacer(Modifier.size(0.dp, 10.dp))

        LazyRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            contentPadding = PaddingValues(end = 8.dp),
        ) {
            items(LANGUAGES) { lang ->
                LanguageChip(
                    lang = lang,
                    selected = lang.code == value,
                    disabled = disabled,
                    onClick = { onChange(lang.code) },
                )
            }
        }
    }
}

@Composable
private fun LanguageChip(
    lang: Language,
    selected: Boolean,
    disabled: Boolean,
    onClick: () -> Unit,
) {
    val borderColor = if (selected) AppColor.Accent.copy(alpha = 0.53f) else AppColor.Hairline
    val backgroundColor = if (selected) AppColor.Accent.copy(alpha = 0.12f) else AppColor.Surface

    Row(
        modifier = Modifier
            .graphicsLayer { alpha = if (disabled) 0.45f else 1f }
            .background(backgroundColor, RoundedCornerShape(AppRadius.md))
            .border(1.dp, borderColor, RoundedCornerShape(AppRadius.md))
            .clickable(enabled = !disabled, onClick = onClick)
            .padding(horizontal = 13.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp),
    ) {
        if (selected) {
            Box(modifier = Modifier.size(8.dp).background(AppColor.Accent, CircleShape))
        }
        Column {
            Text(
                text = lang.native,
                color = if (selected) AppColor.Text else AppColor.TextMuted,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = lang.short,
                color = if (selected) AppColor.AccentStrong else AppColor.TextFaint,
                fontSize = 9.sp,
                letterSpacing = 0.8.sp,
            )
        }
    }
}
