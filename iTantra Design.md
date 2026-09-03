# iTantra — UI/UX Design Specification

## 1. Design Direction

iTantra is a native mobile communication application for **low-bitrate, offline voice transmission**. The interface should take visual inspiration from the supplied reference image:

- Dark, premium mobile UI
- Rounded cards and large corner radii
- Strong visual hierarchy
- Circular progress/status indicators
- Compact pill controls
- Bright green and warm yellow accents
- Simple line icons
- Large, clean typography
- Information presented in modular cards
- Minimal visual noise

The reference should be treated as a **visual language**, not copied screen-for-screen. iTantra must remain a communication/voice-transmission product rather than looking like an education app.

### Core visual personality

**Modern + technical + operational + approachable**

The UI should feel reliable enough for an ISRO-oriented communication system while remaining simple enough to operate quickly in the field.

Do **not** make the interface look like:
- A generic AI dashboard
- A cyberpunk/hacker interface
- A military command console
- A cryptocurrency dashboard
- A glassmorphism template
- A conventional corporate enterprise application

---

# 2. Design Principles

### 2.1 Dark-first

The primary application UI is dark.

Use near-black surfaces rather than pure black everywhere so that cards remain distinguishable.

Recommended base:

- App background: `#0A0A0A`
- Primary surface: `#111111`
- Secondary surface: `#181818`
- Elevated surface: `#202020`
- Border/divider: `#2A2A2A`

The supplied reference uses a black interface surrounded by a neutral gray presentation background. The gray is part of the mockup presentation, **not the app background**.

### 2.2 Accent colors

Use two main accent colors.

**Signal Green**
- Primary: `#79C879`
- Strong: `#63B866`
- Soft: `#A5DFA0`

Use green for:
- Connected state
- Active transmission
- Successful operations
- Primary action
- Healthy system status
- Receiver ready state

**Signal Yellow**
- Primary: `#E8DC67`
- Strong: `#D7C94D`
- Soft: `#F1E99A`

Use yellow for:
- Important information
- Secondary emphasis
- Transmission activity
- Selected tabs
- Warnings that are not critical
- System/connection indicators

### 2.3 Status colors

Additional semantic colors may be used only when required:

- Critical/Error: `#E66B67`
- Warning: `#E5B85C`
- Information: `#72A9D8`
- Neutral: `#8A8A8A`

Do not turn the interface into a rainbow dashboard.

---

# 3. Typography

Use a clean modern sans-serif font.

Preferred:

- **Inter**
- **Roboto** as the native fallback

### Typography hierarchy

| Element | Size | Weight |
|---|---:|---|
| Screen title | 28–32px | 600 |
| Large metric | 36–44px | 700 |
| Section heading | 20–22px | 600 |
| Card title | 16–18px | 600 |
| Body | 14–16px | 400 |
| Supporting text | 12–14px | 400 |
| Caption | 11–12px | 500 |
| Status label | 11–12px | 600 |

Keep headings short.

Example:

`Voice Transmission`

instead of:

`Low-Bitrate Offline Voice Transmission Management`

---

# 4. Layout System

Design for mobile portrait screens first.

### Base spacing scale

Use an 8px-based spacing system:

- 4px — micro spacing
- 8px — tight spacing
- 12px — small spacing
- 16px — standard spacing
- 20px — section spacing
- 24px — card spacing
- 32px — major separation

### Screen padding

Recommended horizontal padding:

**20px**

Use 16px only for dense secondary screens.

### Corner radius

The supplied reference relies heavily on rounded geometry.

Recommended:

- Small controls: 12px
- Pills: 18–24px
- Cards: 20–28px
- Large feature cards: 28–32px
- Circular controls: 50%

Avoid sharp rectangular cards.

---

# 5. Component Language

## 5.1 Cards

Cards are one of the main visual structures.

Use:

- Dark elevated surface
- 20–28px radius
- Generous internal padding
- Minimal/no border
- Clear primary information
- Small supporting metadata

Example:

```text
┌──────────────────────────────┐
│  RECEIVER                    │
│                              │
│  Ready                       │
│  ● Connected                 │
│                              │
│  42 packets received         │
└──────────────────────────────┘
```

Cards should group related information rather than decorate the screen.

---

## 5.2 Pills

Use pill-shaped controls for:

- Connection status
- Language
- Transmission mode
- Time range
- Filters
- Small metadata

Example:

`● Connected`

`English`

`LOW BITRATE`

`Offline`

Selected pills may use Signal Yellow or Signal Green.

---

## 5.3 Circular indicators

The reference image uses circular progress indicators prominently.

Use circular indicators for:

- Connection quality
- Buffer level
- Packet progress
- Audio transmission state
- System readiness
- Storage/cache usage

Do not use circular indicators simply because they look good.

---

## 5.4 Primary buttons

Primary buttons should be large and easy to hit.

Recommended:

- Height: 52–58px
- Radius: 26–29px
- Strong accent background
- Dark text
- Semibold label

Examples:

`START TRANSMISSION`

`HOLD TO SPEAK`

`CONNECT DEVICE`

---

## 5.5 Icon buttons

Use simple outline icons.

Recommended icon size:

**20–24px**

Icon button container:

**40–48px**

Use icons for:
- Search
- Notifications
- Back
- Settings
- Microphone
- Speaker
- Devices
- History
- Language
- Connection
- More options

Avoid highly detailed illustrations inside functional controls.

---

# 6. Navigation

Use a compact bottom navigation bar.

Recommended five destinations:

1. **Home**
2. **Transmit**
3. **Devices**
4. **History**
5. **Settings**

The central **Transmit** item may be visually emphasized because it is the core action.

Alternative 4-item navigation may be used if implementation simplicity is required:

- Home
- Transmit
- History
- Settings

Do not create a large navigation drawer unless the feature count eventually requires it.

---

# 7. Home Screen

The Home screen should borrow the strongest visual idea from the reference image: a greeting/header followed by compact status indicators and feature cards.

### Header

Example:

```text
Good evening, Shreya

iTantra
Offline Communication
```

Top-right:

- Connection icon
- Notification icon
- Profile/system icon if needed

Keep the header compact.

### System status row

Use 2–3 compact circular/card indicators:

```text
┌──────────┐ ┌──────────┐ ┌──────────┐
│  ●       │ │  94%     │ │  32      │
│ Network  │ │ Buffer   │ │ Packets  │
└──────────┘ └──────────┘ └──────────┘
```

Possible values:

- Connection
- Audio buffer
- Packets
- Battery
- Device readiness

Only display metrics that are actually useful.

### Main action card

A large feature card should immediately expose transmission.

```text
┌─────────────────────────────────┐
│  VOICE TRANSMISSION             │
│                                 │
│  Ready to transmit              │
│                                 │
│  English • Low Bitrate          │
│                                 │
│       [ HOLD TO SPEAK ]         │
└─────────────────────────────────┘
```

Use Signal Green as the primary action accent.

### Recent activity

Use a compact list/card:

```text
Recent activity

● Message received       10:42 PM
● Transmission complete  10:38 PM
● Device connected       10:31 PM
```

---

# 8. Transmission Screen

This is the most important screen.

It must prioritize **one action** over everything else.

### Idle state

```text
Voice Transmission

English
Low Bitrate
Offline Ready

          ◉
      HOLD TO SPEAK

Receiver
● Connected
```

The microphone control should be the dominant element.

### Recording state

When the user presses and holds:

```text
Transmitting...

00:07

~~~~~~~~ waveform ~~~~~~~~

● Encoding
● Sending packets

Release to send
```

Use subtle waveform movement.

Do not use a large flashy audio visualizer.

### Transmission completion

```text
Transmission sent

7.4 sec
18 packets
English

[ SEND AGAIN ]
```

---

# 9. Critical Alert Override

iTantra supports priority handling and a **CRITICAL** alert override.

This state must be visually distinct without becoming visually chaotic.

### Critical state

```text
┌──────────────────────────────┐
│  CRITICAL                    │
│                              │
│  Priority transmission       │
│  received                    │
│                              │
│  Playing immediately         │
│                              │
│  [ ACKNOWLEDGE ]             │
└──────────────────────────────┘
```

Use the semantic critical color only here.

The normal green/yellow visual system should not compete with a critical alert.

Critical alerts should:
- Interrupt lower-priority playback when required
- Remain clearly identifiable
- Show source/device
- Show timestamp
- Provide acknowledgement
- Avoid unnecessary animation

---

# 10. Incoming Message / Receiver Screen

Incoming voice should appear as a compact message card.

```text
Incoming transmission

┌──────────────────────────────┐
│  Device A                    │
│  English                     │
│                              │
│  ▶  00:06                    │
│  ───────────────             │
│                              │
│  12 packets                  │
└──────────────────────────────┘
```

Controls:

- Play/Pause
- Replay
- Volume
- Acknowledge if required

The playback state should be obvious.

---

# 11. Devices Screen

The Devices screen should manage P2P communication endpoints.

### Device card

```text
DEV-014

● Connected

Signal       Strong
Packets      128
Last seen    10:41 PM
```

Possible states:

- Connected
- Connecting
- Disconnected
- Unavailable
- Error

Use green only for genuinely connected/healthy states.

### Device actions

- Connect
- Disconnect
- Test connection
- View details

---

# 12. Language Selection

iTantra supports **10 Indian languages**.

Language selection should be simple and fast.

Use a grid/list of language cards rather than a complicated selector.

Example:

```text
Select language

┌─────────────┐ ┌─────────────┐
│ हिंदी       │ │ English     │
└─────────────┘ └─────────────┘

┌─────────────┐ ┌─────────────┐
│ বাংলা       │ │ తెలుగు      │
└─────────────┘ └─────────────┘
```

Selected language:

- Signal Yellow highlight
- Small check icon
- High contrast text

Do not use flags to represent Indian languages.

---

# 13. History Screen

History should be list-oriented and compact.

Each transmission row:

```text
● Received
English
DEV-014
7.2 sec                         10:42 PM
```

Use status icons instead of large decorative graphics.

Filters:

- All
- Sent
- Received
- Critical

Optional date filtering can be added later.

---

# 14. Settings Screen

Group settings into clear sections.

### Audio

- Speaker output
- Microphone
- Playback volume
- Audio test

### Communication

- Device identity
- Connection settings
- Packet settings
- Transmission mode

### Language

- Interface language
- Voice language

### System

- Storage
- Logs
- Diagnostics
- About iTantra

Avoid exposing technical settings unless the target user actually needs them.

---

# 15. Status & Feedback

Feedback must be immediate and understandable.

### Success

Use:
- Green indicator
- Short confirmation
- Minimal animation

Example:

`Transmission sent`

### Warning

Use:
- Yellow indicator
- Clear explanation

Example:

`Connection unstable`

### Error

Use:
- Red indicator
- Cause + action

Example:

`Transmission failed`
`Receiver unavailable`

Do not display vague messages such as:

`Something went wrong.`

---

# 16. Loading States

Avoid generic full-screen spinners.

Prefer:

- Skeleton cards
- Small inline spinners
- Progress indicators
- State text

Example:

`Connecting to receiver...`

rather than a blank screen with a spinner.

---

# 17. Empty States

Empty states should explain what the user can do next.

Example:

```text
No transmissions yet

Your sent and received voice
messages will appear here.

[ START TRANSMISSION ]
```

Do not use oversized illustrations.

---

# 18. Offline-First UX

Offline operation is a core product characteristic, not an error state.

The UI should explicitly communicate:

```text
● Offline Ready
```

rather than implying that offline means failure.

Distinguish:

- **Offline + operational**
- **Disconnected**
- **Device unavailable**
- **Transmission failed**

These states must never look identical.

---

# 19. Audio / TTS Feedback

Because iTantra uses offline TTS and packet-driven playback, the UI should expose useful playback state without exposing implementation complexity.

User-facing states:

- Preparing
- Encoding
- Sending
- Receiving
- Buffering
- Playing
- Complete
- Failed

Do not show internal model names, queues, threads, or technical implementation details in the main UI.

---

# 20. Animation

Animation should communicate state.

Recommended:

- Button press feedback
- Circular progress movement
- Small waveform during transmission
- Connection status transition
- Card/list insertion
- Critical alert entrance

Animation should be:

- Fast
- Subtle
- Functional

Avoid:
- Excessive floating elements
- Parallax
- Neon glow
- Constant background motion
- Large 3D animations
- Decorative particle effects

---

# 21. Shadows and Elevation

Use very subtle elevation.

Dark surfaces should primarily be separated through:

1. Tone difference
2. Spacing
3. Corner radius

Do not rely on heavy shadows.

Example:

```text
Background  #0A0A0A
Card        #151515
Elevated    #1D1D1D
```

---

# 22. Iconography

Use one consistent icon family.

Preferred style:

- Rounded outline icons
- 1.5–2px stroke
- Minimal detail
- Consistent visual weight

Do not mix filled, outlined, 3D, and highly detailed icon styles.

---

# 23. Accessibility

Minimum requirements:

- High contrast text
- Minimum 44px touch targets
- Do not communicate state through color alone
- Clear labels for icons
- Large enough primary transmission control
- Avoid tiny technical metadata
- Support dynamic text sizing where practical

Critical information must always have a text label or icon in addition to color.

---

# 24. Responsive Behavior

Primary target:

**Android mobile portrait**

Design around:

- 360px width minimum
- 390–430px common width
- Safe-area handling
- Variable status/navigation bar heights

Cards should adapt horizontally rather than forcing horizontal scrolling.

Do not build the primary interface around desktop layouts.

---

# 25. Screen Visual Hierarchy

Every screen should follow approximately:

```text
HEADER
   ↓
PRIMARY STATUS / CONTEXT
   ↓
MAIN ACTION
   ↓
SECONDARY INFORMATION
   ↓
RECENT ACTIVITY / DETAILS
```

The user should understand the current state within 2–3 seconds.

---

# 26. Reference Image Translation

The supplied reference provides the following visual patterns to retain:

| Reference Pattern | iTantra Application |
|---|---|
| Circular progress rings | Connection / buffer / packet status |
| Large dark cards | Transmission / receiver cards |
| Green accent | Healthy / connected / active |
| Yellow accent | Selection / secondary emphasis |
| Rounded pills | Language / mode / status |
| Large numerical metrics | Useful system metrics |
| Compact bottom navigation | Main app navigation |
| Large primary cards | Main communication actions |
| Minimal line icons | Navigation and controls |
| Large rounded corners | Overall component language |

Do **not** directly retain the reference's:
- Education/course terminology
- Course illustrations
- Learning cards
- Productivity dashboard semantics
- Arbitrary metrics
- Decorative statistics

---

# 27. Recommended Home Screen Structure

Final visual structure:

```text
┌───────────────────────────────┐
│ Good evening, Shreya     ◯ 🔔 │
│ iTantra                       │
│                               │
│ ┌────────┐ ┌────────┐ ┌─────┐ │
│ │  ●     │ │  94%   │ │ 18  │ │
│ │ Network│ │ Buffer │ │ Pkt │ │
│ └────────┘ └────────┘ └─────┘ │
│                               │
│ Transmission                  │
│ ┌───────────────────────────┐ │
│ │ Voice Transmission        │ │
│ │                           │ │
│ │ English • Low Bitrate     │ │
│ │                           │ │
│ │       HOLD TO SPEAK       │ │
│ └───────────────────────────┘ │
│                               │
│ Recent Activity               │
│ ┌───────────────────────────┐ │
│ │ ● Received     10:42 PM   │ │
│ │ ● Sent         10:38 PM   │ │
│ │ ● Connected    10:31 PM   │ │
│ └───────────────────────────┘ │
│                               │
│ Home  Transmit  Devices  ... │
└───────────────────────────────┘
```

This is a structural guide, not a literal implementation.

---

# 28. Do / Don't

## DO

- Use dark surfaces
- Use green and yellow strategically
- Use large rounded cards
- Keep screens visually clean
- Make transmission the dominant action
- Make connection state immediately visible
- Use compact status indicators
- Use consistent spacing
- Prioritize readability
- Make critical states unmistakable
- Keep technical information secondary

## DON'T

- Don't copy the reference image literally
- Don't use gradients everywhere
- Don't use neon cyberpunk styling
- Don't make every metric a glowing number
- Don't overuse yellow
- Don't use glassmorphism
- Don't use excessive borders
- Don't overload the home screen
- Don't hide connection status
- Don't make the UI look like an AI product
- Don't expose engineering implementation details to normal users

---

# 29. Design Tokens

Use these values as the initial implementation tokens.

```text
COLORS

background        #0A0A0A
surface           #111111
surface-2         #181818
surface-3         #202020
border            #2A2A2A

primary-green     #79C879
green-strong      #63B866
green-soft        #A5DFA0

primary-yellow    #E8DC67
yellow-strong     #D7C94D
yellow-soft       #F1E99A

critical          #E66B67
warning           #E5B85C
info              #72A9D8

text-primary      #F5F5F5
text-secondary    #B5B5B5
text-muted        #777777


SPACING

xs                4
sm                8
md                12
base              16
lg                20
xl                24
xxl               32


RADIUS

control           12
pill              24
card              24
large-card        30
circle            999


SIZING

touch-target      44
button-height     56
icon              24
screen-padding    20
```

---

# 30. Implementation Rule

This document is the **source of truth for the iTantra UI design**.

When implementing screens:

1. Follow these colors and tokens first.
2. Preserve the dark rounded-card visual language.
3. Keep green/yellow accents restrained.
4. Prioritize the communication workflow over decoration.
5. Keep the primary action obvious.
6. Do not introduce a different visual style on individual screens.
7. New components should reuse the established card, pill, button, typography, spacing, and icon rules.
8. If a new requirement conflicts with the visual system, extend the existing system rather than creating an unrelated component style.

The final application should feel like one coherent product, not a collection of individually designed screens.
