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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itantra.app.config.LANGUAGES
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius

/**
 * Compact language dropdown — globe icon + language name + code.
 * Globe dot is AppColor.Primary (green). Dropdown lists all 10 supported languages.
 */
@Composable
fun LanguageSelector(
    value: String,
    onChange: (String) -> Unit,
    disabled: Boolean = false,
) {
    var expanded by remember { mutableStateOf(false) }
    val currentLang = LANGUAGES.find { it.code == value } ?: LANGUAGES.first()

    Box {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .graphicsLayer { alpha = if (disabled) 0.45f else 1f }
                .background(AppColor.Surface, RoundedCornerShape(AppRadius.md))
                .border(1.dp, AppColor.Hairline, RoundedCornerShape(AppRadius.md))
                .clickable(enabled = !disabled) { expanded = true }
                .padding(horizontal = 14.dp, vertical = 13.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                // Globe circle in AppColor.Primary (green)
                Box(
                    modifier = Modifier
                        .size(20.dp)
                        .border(2.dp, AppColor.Primary, CircleShape),
                    contentAlignment = Alignment.Center,
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .background(AppColor.Primary, CircleShape),
                    )
                }
                Text(
                    currentLang.native,
                    color = AppColor.Text,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    currentLang.code.uppercase(),
                    color = AppColor.TextFaint,
                    fontSize = 12.sp,
                )
            }
            Text("▼", color = AppColor.TextFaint, fontSize = 10.sp)
        }

        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(AppColor.SurfaceRaised),
        ) {
            LANGUAGES.forEach { lang ->
                DropdownMenuItem(
                    text = {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            Text(lang.native, color = AppColor.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                            Text(lang.code.uppercase(), color = AppColor.TextFaint, fontSize = 12.sp)
                        }
                    },
                    onClick = {
                        onChange(lang.code)
                        expanded = false
                    },
                )
            }
        }
    }
}
