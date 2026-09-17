package com.itantra.app.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.itantra.app.ui.theme.AppColor

@Composable
fun ThemeMenu(isDarkTheme: Boolean, onThemeToggle: (Boolean) -> Unit) {
    var expanded by remember { mutableStateOf(false) }

    Box {
        IconButton(
            onClick = { expanded = true },
            modifier = Modifier.background(AppColor.Surface.copy(alpha = 0.6f), CircleShape)
        ) {
            Icon(Icons.Rounded.Menu, contentDescription = "Menu", tint = AppColor.Text)
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.background(AppColor.SurfaceRaised)
        ) {
            DropdownMenuItem(
                text = { Text("Appearance", color = AppColor.TextFaint, fontSize = 12.sp, fontWeight = FontWeight.Bold) },
                onClick = { }
            )
            DropdownMenuItem(
                text = { Text(if (isDarkTheme) "○ Light" else "● Light", color = AppColor.Text) },
                onClick = {
                    onThemeToggle(false)
                    expanded = false
                }
            )
            DropdownMenuItem(
                text = { Text(if (isDarkTheme) "● Dark" else "○ Dark", color = AppColor.Text) },
                onClick = {
                    onThemeToggle(true)
                    expanded = false
                }
            )
        }
    }
}
