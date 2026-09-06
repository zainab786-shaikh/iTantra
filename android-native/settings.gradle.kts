pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // Same source the RN app's react-native-sherpa-onnx dependency already
        // uses for the proven sherpa-onnx v1.12.34-2 / onnxruntime
        // v1.24.4-qnn2.43.1.260218-1 AARs (see MIGRATION_STATUS.md Phase 2).
        // Restricted to this one group, same as the RN wrapper's own
        // build.gradle, so it can never shadow any other dependency.
        exclusiveContent {
            forRepository { maven { url = uri("https://xdcobra.github.io/maven") } }
            filter { includeGroup("com.xdcobra.sherpa") }
        }
    }
}

rootProject.name = "iTantra"
include(":app")
