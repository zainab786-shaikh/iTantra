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

    testOptions {
        unitTests {
            // The pure-logic classes under test (frame codec, and from Level 2
            // the text codec) touch no Android APIs, but they live in the same
            // source set as classes that do. Returning defaults rather than
            // throwing keeps an incidental stub call from failing a test that
            // is not actually about Android.
            isReturnDefaultValues = true
        }
    }
}

// ---------------------------------------------------------------------------
// Sherpa-ONNX / ONNX Runtime — direct native integration (Phase 2).
//
// Deliberately NOT the react-native-sherpa-onnx TurboModule: this depends
// directly on the same underlying prebuilt AARs that wrapper resolves for
// the RN app (see node_modules/react-native-sherpa-onnx/android/prebuilt-
// versions.gradle and prebuilt-download.gradle), at the exact same pinned
// versions, so the native binaries are byte-for-byte what the RN baseline
// already ships and has proven working:
//   sherpa-onnx 1.12.34-2  (from third_party/sherpa-onnx-prebuilt/ANDROID_RELEASE_TAG)
//   onnxruntime 1.24.4-qnn2.43.1.260218-1 (prebuilt-versions.gradle default)
//
// Both AARs bundle native .so per ABI under jni/<abi>/ AND a classes.jar
// (com.k2fsa.sherpa.onnx.* for sherpa-onnx, ai.onnxruntime.* for onnxruntime)
// — the same Kotlin API surface react-native-sherpa-onnx's own
// SherpaOnnxSttHelper.kt calls into. AGP's automatic AAR handling was not
// used here on purpose: the upstream wrapper unpacks these AARs manually
// (custom configurations + a Gradle task) rather than a plain
// implementation(...) AAR dependency, which is a strong signal that plain
// AAR auto-merge does not reliably pick up the native libs from this
// particular Maven layout — so that same manual extraction is ported here
// rather than risking a silent UnsatisfiedLinkError at runtime.
// ---------------------------------------------------------------------------
val sherpaOnnxVersion = "1.12.34-2"
val onnxruntimeVersion = "1.24.4-qnn2.43.1.260218-1"
val requiredAbis = listOf("arm64-v8a", "armeabi-v7a", "x86", "x86_64")

val sherpaOnnxAar: Configuration by configurations.creating
val onnxruntimeAar: Configuration by configurations.creating

val sherpaOnnxClassesDir = layout.buildDirectory.dir("sherpa-onnx-classes")
val onnxruntimeClassesDir = layout.buildDirectory.dir("onnxruntime-classes")

val extractSherpaOnnxNative = tasks.register("extractSherpaOnnxNative") {
    inputs.files(sherpaOnnxAar)
    outputs.dir("src/main/jniLibs")
    doLast {
        val aar = sherpaOnnxAar.singleFile
        val extractDir = layout.buildDirectory.dir("sherpa-onnx-aar-extract").get().asFile
        extractDir.deleteRecursively()
        extractDir.mkdirs()
        project.copy { from(project.zipTree(aar)); into(extractDir) }
        requiredAbis.forEach { abi ->
            val jniDir = File(extractDir, "jni/$abi")
            if (jniDir.exists()) {
                project.copy { from(jniDir); into(File(project.file("src/main/jniLibs"), abi)) }
            }
        }
    }
}

val extractSherpaOnnxClasses = tasks.register("extractSherpaOnnxClasses") {
    inputs.files(sherpaOnnxAar)
    outputs.dir(sherpaOnnxClassesDir)
    doLast {
        val dir = sherpaOnnxClassesDir.get().asFile
        dir.mkdirs()
        dir.listFiles()?.filter { it.name.endsWith(".jar") }?.forEach { it.delete() }
        project.copy {
            from(project.zipTree(sherpaOnnxAar.singleFile))
            include("classes.jar")
            into(dir)
        }
    }
}

val extractOnnxruntimeNative = tasks.register("extractOnnxruntimeNative") {
    inputs.files(onnxruntimeAar)
    outputs.dir("src/main/jniLibs")
    doLast {
        val aar = onnxruntimeAar.singleFile
        val extractDir = layout.buildDirectory.dir("onnxruntime-aar-extract").get().asFile
        extractDir.deleteRecursively()
        extractDir.mkdirs()
        project.copy { from(project.zipTree(aar)); into(extractDir) }
        requiredAbis.forEach { abi ->
            val jniDir = File(extractDir, "jni/$abi")
            if (jniDir.exists()) {
                project.copy {
                    from(jniDir)
                    include("libonnxruntime.so", "libonnxruntime4j_jni.so")
                    into(File(project.file("src/main/jniLibs"), abi))
                }
            }
        }
    }
}

val extractOnnxruntimeClasses = tasks.register("extractOnnxruntimeClasses") {
    inputs.files(onnxruntimeAar)
    outputs.dir(onnxruntimeClassesDir)
    doLast {
        val dir = onnxruntimeClassesDir.get().asFile
        dir.mkdirs()
        dir.listFiles()?.filter { it.name.endsWith(".jar") }?.forEach { it.delete() }
        project.copy {
            from(project.zipTree(onnxruntimeAar.singleFile))
            include("classes.jar")
            into(dir)
        }
        val extracted = File(dir, "classes.jar")
        if (extracted.exists()) extracted.renameTo(File(dir, "onnxruntime-classes.jar"))
    }
}

tasks.matching { it.name == "preBuild" }.configureEach {
    dependsOn(extractSherpaOnnxNative, extractSherpaOnnxClasses, extractOnnxruntimeNative, extractOnnxruntimeClasses)
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
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.9.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")

    // Pure-Java tar+bzip2 extraction for the English NeMo CTC model's
    // .tar.bz2 release archive (SttModelManager.kt). The RN baseline's
    // react-native-sherpa-onnx/extraction module did this natively; this
    // migration has no equivalent native module, so a small, standard JVM
    // library replaces it rather than hand-rolling bzip2 decompression.
    implementation("org.apache.commons:commons-compress:1.26.0")

    implementation(platform("androidx.compose:compose-bom:2025.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")

    debugImplementation("androidx.compose.ui:ui-tooling")

    // JVM unit tests for the wire frame and (from Level 2) the text codec.
    // These guard the two claims the demo makes out loud - lossless, and
    // smaller - and run without a device.
    testImplementation("junit:junit:4.13.2")

    sherpaOnnxAar("com.xdcobra.sherpa:sherpa-onnx:$sherpaOnnxVersion@aar")
    onnxruntimeAar("com.xdcobra.sherpa:onnxruntime:$onnxruntimeVersion@aar")
    implementation(fileTree(sherpaOnnxClassesDir) { include("*.jar") }.builtBy(extractSherpaOnnxClasses))
    implementation(fileTree(onnxruntimeClassesDir) { include("*.jar") }.builtBy(extractOnnxruntimeClasses))
}
