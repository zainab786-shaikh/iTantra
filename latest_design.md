# iTantra --- Latest UI Design Specification

## 1. Purpose

This document is the authoritative UI specification for the next iTantra
UI update.

The goal is to replace the current UI with the finalized **dark + light
visual design** shown in the approved reference designs.

The redesign is **UI-only**.

### Non-negotiable rule

> **Do not change, remove, rewrite, optimize, or replace any
> backend/application functionality while implementing this design.**

All existing working behavior must continue to work exactly as it does
now.

This includes:

-   STT
-   TTS
-   VAD
-   sentence segmentation
-   language/model selection
-   packet creation
-   packet priority
-   critical alerts
-   TTS queue behavior
-   MockTransport / current transport behavior
-   UDP transport behavior if already implemented
-   link setup and connection state
-   simulated link-rate behavior
-   model loading and installation
-   offline operation
-   telemetry / decode information
-   existing state management
-   existing navigation logic

Claude must inspect the existing implementation and **reuse the current
state, callbacks, ViewModels, services, managers, and business logic**
rather than creating parallel implementations.

------------------------------------------------------------------------

# 2. Final Navigation Structure

The application has exactly **three primary destinations**:

1.  **Transmit**
2.  **Receive**
3.  **Link**

Use a persistent bottom navigation bar.

There must be no unnecessary:

-   Home screen
-   Devices screen
-   History screen
-   Settings screen
-   separate dashboard
-   decorative feature page

The three destinations must expose the functionality that already
exists.

### Bottom navigation

Use:

-   Microphone icon → Transmit
-   Message/chat icon → Receive
-   Link/connection icon → Link

The selected destination gets the appropriate active background/accent
treatment.

The navigation bar must remain visually compact and should not dominate
the screen.

------------------------------------------------------------------------

# 3. Global Visual Language

The UI should look like a purpose-built field communication application,
not a generic AI-generated dashboard.

The visual direction is:

-   modern
-   dark/light adaptive
-   field-oriented
-   technical but human
-   calm
-   highly readable
-   restrained
-   functional
-   distinctive

Avoid:

-   excessive glassmorphism
-   excessive gradients
-   glowing neon effects
-   generic SaaS dashboard layouts
-   excessive cards
-   excessive rounded pills
-   decorative statistics
-   fake metrics
-   unnecessary animations
-   dense information walls
-   "AI dashboard" styling

Use strong hierarchy, whitespace, thin borders, restrained color
accents, and typography to create the identity.

------------------------------------------------------------------------

# 4. Top-Left Branding

The application identity must be anchored in the **top-left corner**.

Every primary screen should begin with:

-   iTantra logo/name
-   a small contextual subtitle such as:
    -   `TRANSMIT · OFFLINE`
    -   `RECEIVE · OFFLINE`
    -   `LINK · SETUP`

The iTantra wordmark should be visually prominent without occupying
excessive vertical space.

The top-left area should feel intentional and consistent across all
three screens.

The connection/status indicator can remain toward the top-right.

------------------------------------------------------------------------

# 5. Theme System

Implement two application themes:

-   **Dark**
-   **Light**

The user must be able to switch between them using a visible **theme
toggle button**.

Do not create a separate Settings screen just for this.

The toggle can be placed in the top area/menu according to the existing
UI architecture, but it must be accessible from the main application UI.

### Theme requirements

The theme switch must:

-   update the complete UI
-   update backgrounds
-   update cards
-   update text
-   update borders
-   update controls
-   update navigation
-   update illustrations
-   preserve all application state
-   not restart or reset the speech/transport pipeline

The theme selection should persist across app launches if the existing
architecture allows persistence without changing unrelated behavior.

------------------------------------------------------------------------

# 6. Dark Theme

The approved dark visual direction uses:

### Base

-   Near-black / very dark green-black background
-   Dark elevated surfaces
-   Subtle green-tinted borders

### Accent

Primary accent:

-   muted natural green

Secondary accent:

-   warm yellow/olive

Critical:

-   red

The dark theme should feel closer to a field communication terminal than
a standard black mobile app.

Avoid pure black everywhere. Use several subtle surface levels to
establish hierarchy.

------------------------------------------------------------------------

# 7. Light Theme

The approved light design is the light counterpart of the dark design.

### Base

-   warm off-white / ivory background
-   white or very lightly tinted surfaces
-   dark green typography
-   subtle warm-gray borders

### Accent

-   natural green for active/positive states
-   warm yellow for secondary/highlight states
-   red for critical states

The light theme should not look like a generic white Material app.

Maintain the same visual identity as the dark theme.

------------------------------------------------------------------------

# 8. Illustration Assets

Two illustration assets will be provided to the project:

``` text
mountain_illustration_dark.png
mountain_illustration_light.png
```

These are **official UI assets for the Transmit/Link visual
composition**.

Claude must not recreate, redraw, replace, recolor, or generate
alternative versions of these illustrations.

Use the appropriate asset based on the active theme:

``` text
Dark mode  → mountain_illustration_dark.png
Light mode → mountain_illustration_light.png
```

## Illustration composition

The illustration represents:

-   mountain landscape
-   forest
-   communication/radio tower
-   communication signal motif

It should appear as a **subtle background/hero illustration**, not as a
standalone card.

### Important

The illustration must remain behind the functional UI.

It must not:

-   reduce text readability
-   cover buttons
-   interfere with microphone controls
-   interfere with language selection
-   become interactive
-   receive click/touch events
-   affect STT/TTS behavior

Use appropriate opacity/fading if required to preserve readability, but
do not modify the actual image asset.

The image should be positioned toward the upper/hero portion of the
relevant screen, similar to the approved reference.

------------------------------------------------------------------------

# 9. Transmit Screen

The Transmit screen is the primary interaction screen.

Structure:

### Header

Top-left:

``` text
iTantra
TRANSMIT · OFFLINE
```

Top-right:

``` text
● Link Active
```

or the current actual connection state.

Do not hard-code the status.

Use the existing application state.

------------------------------------------------------------------------

## Hero / illustration

Place the relevant mountain illustration behind the upper content.

The illustration should provide visual identity while remaining
secondary to the microphone interaction.

------------------------------------------------------------------------

## Language selector

Provide the existing language selection functionality.

Display the currently selected language clearly.

Example:

``` text
◎  English   EN        ˅
```

The selector must continue using the existing language/model mapping.

Do not change:

-   language codes
-   model mappings
-   model loading
-   STT implementation

------------------------------------------------------------------------

## Microphone interaction

The microphone is the primary action.

Use the existing PTT / recording behavior.

The visual design should communicate:

``` text
HOLD TO SPEAK
```

and:

``` text
Speech will be converted to text
and transmitted offline
```

The microphone control must remain large and easy to operate.

Do not replace the current recording callbacks or audio pipeline.

------------------------------------------------------------------------

## Critical toggle

Retain the existing critical-message functionality.

Example:

``` text
Send as critical
Priority is set automatically
from what you say.
```

The control must be functional.

Do not create a decorative toggle that does not affect the existing
packet priority logic.

------------------------------------------------------------------------

## Last transmission

Show the existing latest transmission state when available.

Example information:

-   message text
-   time
-   language
-   decode latency
-   byte count

If no message exists, use the existing empty state.

Do not invent sample messages.

------------------------------------------------------------------------

# 10. Receive Screen

The Receive screen should prioritize readability and message scanning.

Header:

``` text
iTantra
RECEIVE · OFFLINE
```

Connection status remains visible.

------------------------------------------------------------------------

## Received / Sent navigation

If the existing implementation already exposes Received/Sent state,
retain it.

Do not add fake tabs if the underlying functionality does not exist.

------------------------------------------------------------------------

## Message filters

Existing message priority filtering can be represented as:

-   All
-   Normal
-   Critical

Only show controls that are connected to actual application state.

No decorative filter buttons.

------------------------------------------------------------------------

## Message cards

Messages should be displayed as compact, readable communication records.

Each record can contain:

-   priority
-   language
-   timestamp
-   message text
-   byte information
-   compression ratio if already available
-   sender/device identifier if already available
-   replay action if already implemented

### Critical messages

Critical messages must remain visually distinct.

Use the existing critical-alert behavior.

Do not weaken:

-   priority
-   interruption behavior
-   TTS queue behavior
-   non-interruptibility

Critical alerts should use the red visual treatment shown in the
approved design.

------------------------------------------------------------------------

# 11. Link Screen

The Link screen contains actual connection/link information.

Header:

``` text
iTantra
LINK · SETUP
```

------------------------------------------------------------------------

## This device

Show the existing device information.

Examples:

-   Device ID
-   IP address
-   Port
-   current status

Do not generate fake values.

------------------------------------------------------------------------

## Peer device

Show the existing peer configuration.

The existing peer-address input must remain functional.

If the current implementation has a Connect/Done action, keep it.

Do not replace it with a decorative button.

------------------------------------------------------------------------

## Simulated link rate

Retain the existing simulated link-rate functionality if it is currently
implemented.

Examples:

-   Off
-   250 bps
-   1 kbps
-   5 kbps

The controls must update the actual existing rate limiter/state.

Do not present these as real radio capabilities if the implementation is
a simulation/rate limiter.

------------------------------------------------------------------------

## Airtime race

If the existing Airtime Race functionality is implemented, retain it.

Present:

-   uncompressed size
-   transmitted size
-   calculated airtime
-   current selected rate

The displayed values must come from existing application logic.

Never hard-code demonstration numbers.

------------------------------------------------------------------------

# 12. Functional UI Rule

Every visible interactive element must have a real purpose.

Before adding a button, ask:

> What existing function does this button call?

If there is no answer, do not add the button.

Examples of valid controls:

-   Transmit
-   Receive
-   Link
-   language selector
-   microphone/PTT
-   critical toggle
-   replay
-   connection controls
-   link-rate controls
-   theme toggle

Examples of invalid controls:

-   decorative "Start"
-   fake "Optimize"
-   fake "Scan"
-   fake "Connect" with no implementation
-   fake statistics
-   placeholder actions
-   unnecessary settings
-   unused menu items

------------------------------------------------------------------------

# 13. No Fake Data

The redesigned UI must not introduce fake:

-   messages
-   device IDs
-   IP addresses
-   timestamps
-   latency
-   compression ratios
-   model status
-   connection status
-   airtime values
-   telemetry

If the backend/application state has no value, show an appropriate empty
state.

Empty space is preferable to fake information.

------------------------------------------------------------------------

# 14. Backend Preservation

This redesign must not modify the following implementation layers unless
absolutely required only to connect existing state to a redesigned UI:

``` text
STT
TTS
VAD
Sentence segmentation
Audio capture
Model management
Packet schema
Packet priority
Transport
Receiver pipeline
TTS queue
Critical alert behavior
Offline model handling
Native/JNI boundary
Native engine
```

Do not:

-   change model mappings
-   change model files
-   change inference code
-   change VAD thresholds
-   change segmentation thresholds
-   change packet schema
-   change transport behavior
-   replace MockTransport
-   introduce cloud APIs
-   introduce React Native
-   introduce Flutter
-   rewrite native processing code

------------------------------------------------------------------------

# 15. Existing Architecture Must Be Reused

The UI should consume existing application state.

Preferred flow:

``` text
Existing backend/application logic
            ↓
Existing ViewModel / state
            ↓
New Compose UI
```

Not:

``` text
New UI
  ↓
new duplicate backend logic
```

Do not create a second implementation of any existing functionality
simply to make the UI easier to build.

------------------------------------------------------------------------

# 16. Responsive Layout

The design must work on real Android phones.

Requirements:

-   no horizontal clipping
-   no text overflow
-   no controls hidden behind navigation
-   no content underneath system bars
-   proper scrolling on smaller displays
-   bottom navigation remains accessible
-   large microphone control remains usable
-   language selector remains usable
-   message cards remain readable

The screenshots are visual references, not fixed pixel coordinates.

Use responsive Compose layouts.

------------------------------------------------------------------------

# 17. Typography

Use a clean modern sans-serif typography system.

Hierarchy:

### Product name

Large and strong.

### Screen context

Small, uppercase, letter-spaced.

### Section labels

Medium/small uppercase with tracking.

### Main message

Readable and relatively large.

### Metadata

Smaller and lower contrast.

Do not overuse bold text.

Typography should create hierarchy instead of relying on excessive cards
and borders.

------------------------------------------------------------------------

# 18. Icons

Use simple line icons.

Icons must communicate function clearly.

Preferred visual characteristics:

-   thin/medium stroke
-   minimal
-   consistent sizing
-   no decorative icon collections

Do not add icons purely to fill empty space.

------------------------------------------------------------------------

# 19. Cards and Containers

Cards should be used only when they establish meaningful grouping.

Use:

-   subtle borders
-   modest corner radius
-   restrained elevation
-   clear internal spacing

Avoid putting every piece of information into its own card.

The UI should have breathing room.

------------------------------------------------------------------------

# 20. Animations

Animations should be minimal and functional.

Allowed:

-   microphone recording state
-   subtle link status change
-   theme transition
-   TTS/playing indicator
-   existing state transitions

Avoid:

-   large entrance animations
-   excessive bouncing
-   decorative particles
-   constant pulsing
-   distracting background animation

The communication task takes priority over visual effects.

------------------------------------------------------------------------

# 21. Theme Toggle

Add a clear theme toggle.

Recommended conceptual behavior:

``` text
☀ Light
☾ Dark
```

or an equivalent compact icon control.

The control must actually switch the application's theme.

It must not:

-   restart the application
-   reset language
-   stop recording unexpectedly
-   clear messages
-   disconnect the link
-   reset TTS state

------------------------------------------------------------------------

# 22. Accessibility and Field Usability

The UI should be usable in real field conditions.

Ensure:

-   adequate touch target sizes
-   sufficient contrast
-   readable message text
-   clear critical state
-   clear recording state
-   clear link state
-   no reliance on color alone for critical information
-   important actions remain obvious

The microphone should be easy to find immediately.

------------------------------------------------------------------------

# 23. Implementation Checklist

Before considering the redesign complete, verify:

### Visual

-   [ ] Dark theme matches approved direction
-   [ ] Light theme matches approved direction
-   [ ] iTantra identity is top-left
-   [ ] Mountain illustration is used correctly
-   [ ] Correct illustration changes with theme
-   [ ] Bottom navigation has exactly three destinations
-   [ ] UI is not a generic AI dashboard
-   [ ] Layout works on actual Android phone
-   [ ] No clipping/overflow

### Functionality

-   [ ] STT still works
-   [ ] TTS still works
-   [ ] VAD still works
-   [ ] Sentence segmentation still works
-   [ ] All 10 languages remain selectable
-   [ ] Existing model mappings remain unchanged
-   [ ] PTT remains functional
-   [ ] Critical priority remains functional
-   [ ] Critical TTS behavior remains unchanged
-   [ ] Receive/replay remains functional where already implemented
-   [ ] Link setup remains functional
-   [ ] Existing link-rate simulation remains functional
-   [ ] Existing transport behavior remains unchanged
-   [ ] Offline operation remains unchanged
-   [ ] Theme toggle works

### Data integrity

-   [ ] No fake messages
-   [ ] No fake device information
-   [ ] No hard-coded connection status
-   [ ] No hard-coded performance metrics
-   [ ] No hard-coded telemetry
-   [ ] Empty states are used when real data is unavailable

### Regression

-   [ ] Existing backend code was not rewritten
-   [ ] Existing speech pipeline was not changed
-   [ ] Existing model configuration was not changed
-   [ ] Existing transport implementation was not changed
-   [ ] APK builds successfully
-   [ ] APK installs successfully
-   [ ] App launches successfully
-   [ ] UI tested on a physical Android device

------------------------------------------------------------------------

# 24. Final Instruction to the Implementer

Treat this document as a **UI redesign specification, not a request to
redesign the application architecture**.

The objective is:

> **Make iTantra look substantially more polished, distinctive, and
> intentional while keeping the existing application behavior exactly
> the same.**

Reuse the existing functionality.

Change the presentation layer.

Do not invent functionality.

Do not remove functionality.

Do not alter the speech, packet, transport, model, or native processing
layers.

The approved visual direction consists of:

``` text
iTantra
├── Transmit
│   ├── Top-left branding
│   ├── Status
│   ├── Mountain illustration
│   ├── Language
│   ├── Microphone/PTT
│   ├── Critical control
│   └── Last transmission
│
├── Receive
│   ├── Top-left branding
│   ├── Status
│   ├── Received/Sent where supported
│   ├── Filters where supported
│   └── Message records
│
└── Link
    ├── Top-left branding
    ├── Device information
    ├── Peer information
    ├── Link controls
    └── Airtime/link information where supported
```

with:

``` text
Dark mode  → mountain_illustration_dark.png
Light mode → mountain_illustration_light.png
```

and a functional theme toggle available from the main UI.
