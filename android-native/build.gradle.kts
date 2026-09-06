// Top-level build file. Plugin versions are declared here (with apply false)
// and applied per-module in app/build.gradle.kts. Versions are pinned to what
// this machine already resolves and builds successfully for the frozen RN
// baseline (see MIGRATION_STATUS.md, Phase 0): AGP 8.12.0, Kotlin 2.1.20.
plugins {
    id("com.android.application") version "8.12.0" apply false
    id("org.jetbrains.kotlin.android") version "2.1.20" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20" apply false
}
