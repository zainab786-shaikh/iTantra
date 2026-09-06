package com.itantra.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * The product is dark-first by design (iTantra Design.md §2.1), not
 * dark-mode-as-a-preference, so unlike a typical Material app this scheme
 * has no light variant and does not branch on system theme.
 */
private val ITantraColorScheme = darkColorScheme(
    primary = AppColor.Primary,
    onPrimary = AppColor.Void,
    secondary = AppColor.Accent,
    onSecondary = AppColor.Void,
    background = AppColor.Void,
    onBackground = AppColor.Text,
    surface = AppColor.Surface,
    onSurface = AppColor.Text,
    surfaceVariant = AppColor.SurfaceRaised,
    onSurfaceVariant = AppColor.TextMuted,
    error = AppColor.Danger,
    onError = AppColor.Void,
    outline = AppColor.Hairline,
    outlineVariant = AppColor.HairlineStrong,
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
fun ITantraTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = ITantraColorScheme,
        typography = ITantraTypography,
        content = content,
    )
}
