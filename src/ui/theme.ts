/**
 * Dark-first design tokens.
 *
 * The palette is built around a near-black "deep space" ground so the mic
 * visualizer and priority colours are the only saturated things on screen —
 * this is an operational tool, and colour has to mean something.
 */
export const theme = {
  color: {
    /** Page ground, darkest to lightest. */
    void: '#05060B',
    abyss: '#0A0D16',
    surface: '#111623',
    surfaceRaised: '#161C2C',
    hairline: 'rgba(148, 163, 184, 0.14)',
    hairlineStrong: 'rgba(148, 163, 184, 0.26)',

    text: '#F1F5F9',
    textMuted: '#94A3B8',
    textFaint: '#64748B',

    /** Brand cyan-teal, used for the live/active state. */
    primary: '#22D3EE',
    primaryDim: '#0E7490',
    /** Violet accent for the idle halo and gradients. */
    accent: '#818CF8',
    /** Speech-detected state. */
    live: '#34D399',
    warn: '#FBBF24',
    danger: '#F87171',
  },
  radius: { sm: 8, md: 14, lg: 20, xl: 28, pill: 999 },
  space: (n: number) => n * 4,
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
  INITIALIZING: { label: 'LOADING MODEL', color: theme.color.warn },
  LISTENING: { label: 'LISTENING', color: theme.color.primary },
  SPEAKING: { label: 'SPEECH DETECTED', color: theme.color.live },
  TRANSCRIBING: { label: 'DECODING', color: theme.color.accent },
  ERROR: { label: 'FAULT', color: theme.color.danger },
};
