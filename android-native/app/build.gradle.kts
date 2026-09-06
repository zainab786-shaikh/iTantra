plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.itantra.app"
    // Matches the frozen RN baseline recorded in MIGRATION_STATUS.md (Phase 0):
    // minSdk 24, targetSdk 34, compileSdk 36. Not chosen independently.
    compileSdk = 36

    defaultConfig {
        applicationId = "com.itantra.app"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    // Pinned below the newest releases deliberately: the latest core-ktx/
    // compose-bom lines now require compileSdk 37 + AGP 9.1.0+, and this
    // machine has only SDK platforms 35/36 installed and AGP 8.12.0 proven
    // working (see MIGRATION_STATUS.md, Phase 0). These versions are the
    // most recent ones still compiled against compileSdk 36.
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.activity:activity-compose:1.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.9.4")

    implementation(platform("androidx.compose:compose-bom:2025.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")

    debugImplementation("androidx.compose.ui:ui-tooling")
}
