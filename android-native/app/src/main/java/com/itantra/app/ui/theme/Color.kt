package com.itantra.app.ui.theme

import androidx.compose.ui.graphics.Color

/**
 * Direct port of the design tokens in src/ui/theme.ts. Values are copied
 * verbatim, not re-derived or adjusted — see MIGRATION_AUDIT.md §H (UI
 * architecture) and iTantra Design.md §29 (Design Tokens) for the source of
 * truth these mirror.
 */
object AppColor {
    // Page ground, darkest to lightest.
    val Void = Color(0xFF0A0A0A)
    val Abyss = Color(0xFF111111)
    val Surface = Color(0xFF181818)
    val SurfaceRaised = Color(0xFF202020)
    val Hairline = Color(0x14F5F5F5) // rgba(245, 245, 245, 0.08)
    val HairlineStrong = Color(0x29F5F5F5) // rgba(245, 245, 245, 0.16)

    val Text = Color(0xFFF5F5F5)
    val TextMuted = Color(0xFFB5B5B5)
    val TextFaint = Color(0xFF777777)

    // Signal Green — connected, active transmission, primary action.
    val Primary = Color(0xFF79C879)
    val PrimaryDim = Color(0xFF63B866)

    // Signal Yellow — selection / secondary emphasis.
    val Accent = Color(0xFFE8DC67)
    val AccentStrong = Color(0xFFD7C94D)

    // Speech-detected / healthy state — same family as primary.
    val Live = Color(0xFF79C879)

    // Non-critical warning.
    val Warn = Color(0xFFE5B85C)

    // Informational, non-warning (e.g. "preparing voice").
    val Info = Color(0xFF72A9D8)

    // Critical/error only.
    val Danger = Color(0xFFE66B67)
}
