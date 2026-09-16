package com.itantra.app.core

/**
 * Phase 14.2: whether a speech model is resident and usable, for the UI's readiness state.
 * Loading happens in the background; nothing waits for READY. A request made while LOADING
 * is served once the load finishes, and one made after UNAVAILABLE falls back to the normal
 * lazy load.
 */
enum class ModelReadiness {
    /** Nothing loaded yet and no warm-up running. */
    NOT_LOADED,
    LOADING,
    READY,
    /** Not installed, or the warm-up failed: the model is loaded lazily when first needed. */
    UNAVAILABLE,
}
