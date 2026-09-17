package com.itantra.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

private fun getITantraColorScheme(colors: AppColors) = darkColorScheme(
    primary = colors.Primary,
    onPrimary = colors.Void,
    secondary = colors.Accent,
    onSecondary = colors.Void,
    background = colors.Void,
    onBackground = colors.Text,
    surface = colors.Surface,
    onSurface = colors.Text,
    surfaceVariant = colors.SurfaceRaised,
    onSurfaceVariant = colors.TextMuted,
    error = colors.Danger,
    onError = colors.Void,
    outline = colors.Hairline,
    outlineVariant = colors.HairlineStrong,
)

// Typography scale per iTantra Design.md §3 (screen title 28-32/600, section
// heading 20-22/600, body 14-16/400, caption 11-12/500).
private val ITantraTypography = Typography().let { base ->
    base.copy(
        titleLarge = base.titleLarge.copy(fontSize = 28.sp, fontWeight = FontWeight.SemiBold),
        titleMedium = base.titleMedium.copy(fontSize = 20.sp, fontWeight = FontWeight.SemiBold),
        bodyLarge = base.bodyLarge.copy(fontSize = 16.sp, fontWeight = FontWeight.Normal),
        bodyMedium = base.bodyMedium.copy(fontSize = 14.sp, fontWeight = FontWeight.Normal),
        labelSmall = base.labelSmall.copy(fontSize = 11.sp, fontWeight = FontWeight.Medium),
    )
}

@Composable
fun ITantraTheme(isDarkTheme: Boolean = true, content: @Composable () -> Unit) {
    val colors = if (isDarkTheme) DarkAppColors else LightAppColors
    
    LaunchedEffect(colors) {
        AppColor.update(colors)
    }

    MaterialTheme(
        colorScheme = getITantraColorScheme(colors),
        typography = ITantraTypography,
        content = content,
    )
}
