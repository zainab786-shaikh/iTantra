/**
 * Dark-first design tokens, per iTantra Design.md.
 *
 * Near-black surfaces (not pure black) so cards stay distinguishable from the
 * page ground. Two restrained accents carry all meaning — Signal Green for
 * connected/active/healthy, Signal Yellow for selection/secondary emphasis —
 * plus a small semantic set (critical/warning/info) used only where the doc
 * calls for it. Key names are kept stable from the previous palette so call
 * sites didn't need a mechanical rename; only the underlying values moved.
 */
export const theme = {
  color: {
    /** Page ground, darkest to lightest. */
    void: '#0A0A0A',
    abyss: '#111111',
    surface: '#181818',
    surfaceRaised: '#202020',
    hairline: 'rgba(245, 245, 245, 0.08)',
    hairlineStrong: 'rgba(245, 245, 245, 0.16)',

    text: '#F5F5F5',
    textMuted: '#B5B5B5',
    textFaint: '#777777',

    /** Signal Green — connected, active transmission, primary action. */
    primary: '#79C879',
    primaryDim: '#63B866',
    /** Signal Yellow — selection / secondary emphasis. */
    accent: '#E8DC67',
    accentStrong: '#D7C94D',
    /** Speech-detected / healthy state — same family as primary. */
    live: '#79C879',
    /** Non-critical warning. */
    warn: '#E5B85C',
    /** Informational, non-warning (e.g. "preparing voice"). */
    info: '#72A9D8',
    /** Critical/error only. */
    danger: '#E66B67',
  },
  radius: { sm: 12, md: 16, lg: 24, xl: 28, pill: 24 },
  space: (n: number) => n * 4,
  sizing: { touchTarget: 44, buttonHeight: 56, icon: 24, screenPadding: 20 },
  font: {
    /** Tabular-ish stack for readouts that must not jitter as digits change. */
    mono: 'monospace' as const,
  },
} as const;

/** Status -> label + colour, shared by the badge and the halo. */
export const STATUS_META: Record<
  string,
  { label: string; color: string }
> = {
  IDLE: { label: 'STANDBY', color: theme.color.textMuted },
  INITIALIZING: { label: 'PREPARING', color: theme.color.warn },
  LISTENING: { label: 'LISTENING', color: theme.color.primary },
  SPEAKING: { label: 'SPEECH DETECTED', color: theme.color.live },
  TRANSCRIBING: { label: 'PROCESSING', color: theme.color.accent },
  ERROR: { label: 'ERROR', color: theme.color.danger },
};
