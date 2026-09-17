package com.itantra.app.ui.theme

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color

data class AppColors(
    val Void: Color,
    val Abyss: Color,
    val Surface: Color,
    val SurfaceRaised: Color,
    val Hairline: Color,
    val HairlineStrong: Color,
    val Text: Color,
    val TextMuted: Color,
    val TextFaint: Color,
    val Primary: Color,
    val PrimaryDim: Color,
    val Accent: Color,
    val AccentStrong: Color,
    val Live: Color,
    val Warn: Color,
    val Info: Color,
    val Danger: Color
)

val DarkAppColors = AppColors(
    Void = Color(0xFF0A0A0A),
    Abyss = Color(0xFF111111),
    Surface = Color(0xFF181818),
    SurfaceRaised = Color(0xFF202020),
    Hairline = Color(0x14F5F5F5), // rgba(245, 245, 245, 0.08)
    HairlineStrong = Color(0x29F5F5F5), // rgba(245, 245, 245, 0.16)
    Text = Color(0xFFF5F5F5),
    TextMuted = Color(0xFFB5B5B5),
    TextFaint = Color(0xFF777777),
    Primary = Color(0xFF79C879),
    PrimaryDim = Color(0xFF63B866),
    Accent = Color(0xFFE8DC67),
    AccentStrong = Color(0xFFD7C94D),
    Live = Color(0xFF79C879),
    Warn = Color(0xFFE5B85C),
    Info = Color(0xFF72A9D8),
    Danger = Color(0xFFE66B67)
)

val LightAppColors = AppColors(
    Void = Color(0xFFF6F5EF),
    Abyss = Color(0xFFEAE8DC),
    Surface = Color(0xFFFFFFFF),
    SurfaceRaised = Color(0xFFF9F9F9),
    Hairline = Color(0xFFD9DDD7),
    HairlineStrong = Color(0xFFC0C5BE),
    Text = Color(0xFF18221B),
    TextMuted = Color(0xFF667067),
    TextFaint = Color(0xFF8A938C),
    Primary = Color(0xFF2F6B3D),
    PrimaryDim = Color(0xFF65C978),
    Accent = Color(0xFFC8B83E),
    AccentStrong = Color(0xFFAFA030),
    Live = Color(0xFF65C978),
    Warn = Color(0xFFC8B83E),
    Info = Color(0xFF72A9D8),
    Danger = Color(0xFFE53935)
)

/**
 * State-backed colors that can be updated dynamically when the theme changes.
 */
object AppColor {
    var Void by mutableStateOf(DarkAppColors.Void)
    var Abyss by mutableStateOf(DarkAppColors.Abyss)
    var Surface by mutableStateOf(DarkAppColors.Surface)
    var SurfaceRaised by mutableStateOf(DarkAppColors.SurfaceRaised)
    var Hairline by mutableStateOf(DarkAppColors.Hairline)
    var HairlineStrong by mutableStateOf(DarkAppColors.HairlineStrong)
    var Text by mutableStateOf(DarkAppColors.Text)
    var TextMuted by mutableStateOf(DarkAppColors.TextMuted)
    var TextFaint by mutableStateOf(DarkAppColors.TextFaint)
    var Primary by mutableStateOf(DarkAppColors.Primary)
    var PrimaryDim by mutableStateOf(DarkAppColors.PrimaryDim)
    var Accent by mutableStateOf(DarkAppColors.Accent)
    var AccentStrong by mutableStateOf(DarkAppColors.AccentStrong)
    var Live by mutableStateOf(DarkAppColors.Live)
    var Warn by mutableStateOf(DarkAppColors.Warn)
    var Info by mutableStateOf(DarkAppColors.Info)
    var Danger by mutableStateOf(DarkAppColors.Danger)

    fun update(colors: AppColors) {
        Void = colors.Void
        Abyss = colors.Abyss
        Surface = colors.Surface
        SurfaceRaised = colors.SurfaceRaised
        Hairline = colors.Hairline
        HairlineStrong = colors.HairlineStrong
        Text = colors.Text
        TextMuted = colors.TextMuted
        TextFaint = colors.TextFaint
        Primary = colors.Primary
        PrimaryDim = colors.PrimaryDim
        Accent = colors.Accent
        AccentStrong = colors.AccentStrong
        Live = colors.Live
        Warn = colors.Warn
        Info = colors.Info
        Danger = colors.Danger
    }
}
