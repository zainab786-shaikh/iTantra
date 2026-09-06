# iTantra — Tactical Offline Speech-to-Packet Communication Engine

[![Kotlin](https://img.shields.io/badge/Kotlin-2.1.20-blue.svg)](https://kotlinlang.org/)
[![Jetpack Compose](https://img.shields.io/badge/Jetpack%20Compose-Material3-black.svg)](https://developer.android.com/jetpack/compose)
[![Android](https://img.shields.io/badge/Android-minSdk%2024-3DDC84.svg)](https://developer.android.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Offline STT](https://img.shields.io/badge/Offline%20STT-AI4Bharat%20IndicConformer-brightgreen.svg)]()
[![Offline TTS](https://img.shields.io/badge/Offline%20TTS-Sherpa--ONNX%20VITS-orange.svg)]()

**iTantra** is a high-performance, edge-first tactical voice communication engine built natively for Android in **Kotlin** with **Jetpack Compose**, calling directly into on-device **sherpa-onnx** ONNX Runtime decoders (no TurboModule bridge, no JavaScript runtime).

Designed for high-stress, low-connectivity, disaster-response, and tactical military/defense environments, iTantra records audio, performs on-device Voice Activity Detection (VAD), transcribes speech into text **completely offline**, categorizes priority based on multi-lingual keyword triggers, and wraps the utterance into a standardized, transport-agnostic data packet for broadcast over radio (LoRa, BLE, or tactical mesh networks).

The app runs a complete bidirectional tactical communication system:

* **Transmit (STT)** — Speech in ➔ On-Device VAD ➔ Offline STT ➔ Priority Banding ➔ `ITantraPacket` out.
* **Receive (TTS)** — Packets in ➔ Priority Filtering ➔ On-Device TTS Speech Synthesis ➔ Audio Spoken Aloud.

> **Note:** this project was originally prototyped in React Native (Expo). It has since been fully migrated to a native Kotlin/Jetpack Compose application — see [`MIGRATION_STATUS.md`](MIGRATION_STATUS.md) and [`MIGRATION_AUDIT.md`](MIGRATION_AUDIT.md) for the complete phase-by-phase migration record. React Native is no longer part of this codebase.

---

## 🚀 Key Features

* **100% Offline Speech-to-Text (STT)**:
  * Powered by **AI4Bharat IndicConformer ASR** (INT8 quantized) for Indian languages and **NVIDIA NeMo CTC Conformer** for English.
  * Completely offline: Zero cloud APIs, zero Firebase, zero external network dependencies.
  * Dedicated language-specific models eliminate language identification errors and script bleeding entirely, ensuring high accuracy in native scripts.
* **100% Offline Text-to-Speech (TTS)**:
  * Powered by **Sherpa-ONNX VITS** speech synthesis engines.
  * Piper high-fidelity voices for English, Hindi, and Malayalam.
  * Meta MMS (Massively Multilingual Speech) voices for Marathi, Gujarati, Kannada, Tamil, Telugu, Odia, and Bengali.
  * Automated background audio queue, instant speech playback of received transmissions, and dedicated replay controls.
* **Sub-Second Low Latency**:
  * Single-pass CTC decoders transcribing speech in **~500 ms** to sub-second timings on mobile CPUs.
  * Drastically faster than autoregressive decoders (like Whisper), which take 6+ seconds on the same hardware.
* **10 Supported Languages (Ordered by Operator Priority)**:
  1. **English (`en-IN`)** — *Default & Selected*
  2. **Hindi (`hi-IN`)** — हिन्दी
  3. **Marathi (`mr-IN`)** — मराठी
  4. **Gujarati (`gu-IN`)** — ગુજરાતી
  5. **Kannada (`kn-IN`)** — ಕನ್ನಡ
  6. **Malayalam (`ml-IN`)** — മലയാളം
  7. **Tamil (`ta-IN`)** — தமிழ்
  8. **Telugu (`te-IN`)** — తెలుగు
  9. **Odia (`or-IN`)** — ଓଡ଼ିଆ
  10. **Bengali (`bn-IN`)** — বাংলা
* **Intelligent Sentence Segmentation & VAD**:
  * Adaptive noise-floor energy + Zero Crossing Rate (ZCR) detector.
  * Hysteresis smoothing, configurable pause flush timing (600 ms, 750 ms, 1000 ms), and pre-speech audio buffering (ensuring leading syllables are never clipped).
* **Deterministic Priority Banding**:
  * Real-time automated priority tagging: `CRITICAL`, `HIGH`, `MEDIUM`, or `NORMAL`.
  * Transparent, rule-based multilingual keyword triggers across all 10 supported languages (detecting emergency, fire, attack, injury, or rescue terms in native scripts).
* **Field-Ready Dark Cockpit UI**:
  * Sleek, high-contrast tactical dark aesthetic with audio wave visualizer, live audio level monitor, response-pause control, and connection status indicators.

---

## 📊 Model Mapping (STT + TTS)

Every language is mapped to a dedicated speech-to-text decoder and text-to-speech voice:

| Order | Language | Code | STT Engine (Speech-to-Text) | TTS Engine (Text-to-Speech) |
| :---: | :--- | :---: | :--- | :--- |
| **1** | **English** (Default) | `en-IN` | NeMo CTC Conformer Medium (int8) | Piper VITS (`en_US-lessac`) |
| **2** | **Hindi** | `hi-IN` | AI4Bharat IndicConformer (int8) | Piper VITS (`hi_IN-pratham`) |
| **3** | **Marathi** | `mr-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`mar`) |
| **4** | **Gujarati** | `gu-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`guj`) |
| **5** | **Kannada** | `kn-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`kan`) |
| **6** | **Malayalam** | `ml-IN` | AI4Bharat IndicConformer (int8) | Piper VITS (`ml_IN-arjun`) |
| **7** | **Tamil** | `ta-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`tam`) |
| **8** | **Telugu** | `te-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`tel`) |
| **9** | **Odia** | `or-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`ory`) |
| **10** | **Bengali** | `bn-IN` | AI4Bharat IndicConformer (int8) | Meta MMS VITS (`ben`) |

---

## 🎯 Why AI4Bharat IndicConformer Solved the Accuracy Problem

Earlier iterations evaluated generic multilingual models (Whisper and Dolphin CTC), which exhibited fundamental architectural drawbacks:

1. **Whisper Byte-Level BPE Artifacts**: Whisper uses byte-level BPE tokenization. The underlying C++ runtime converts tokens to strings one at a time, causing multi-byte UTF-8 sequences (essential for Indic scripts) to collapse into empty tokens. Hindi words decoded as broken fragments (`[" ह","म","े","ं"," ","","","","र"]`).
2. **Dolphin Multilingual Script Confusion**: Dolphin CTC required the model to predict the language itself. Because Brahmic scripts share phonetic roots, it frequently transcribed the correct phonetics in the wrong script (e.g., transcribing Bengali words into Devanagari script).

### The AI4Bharat Solution:
* **Dedicated Native Language Vocabularies**: AI4Bharat IndicConformer models are trained specifically on authentic regional Indian speech corpora, with language-specific token sets.
* **Zero Script Bleeding**: When Marathi or Bengali is selected, the decoder is acoustically and lexically constrained to that language's script.
* **Quantized INT8 Efficiency**: At ~188 MB per language, these models run with 2 CPU threads on mobile hardware, delivering sub-second transcription with complete offline independence.

---

## 🛠 Architecture & Signal Flow

### 1. Transmit Path (Voice ➔ Packet)

```
[ Microphone Input ] (16 kHz mono int16 via android.media.AudioRecord)
        │
        ▼
[ AudioCaptureService ] ──► Frame Resampling & 512-sample Slicing
        │
        ▼
[ EnergyVad ] ──► Adaptive Energy + Zero-Crossing-Rate Detector
        │
        ▼
[ SentenceSegmenter ] ──► Pre-speech Padding + Dynamic Audio Buffering + Pause Flush
        │
        ▼
[ SttEngineProvider ] ──► Direct sherpa-onnx Kotlin API (com.k2fsa.sherpa.onnx)
        │                 • English: NVIDIA NeMo CTC (~158 MB)
        │                 • Indic: AI4Bharat IndicConformer (~188 MB each)
        │
        ▼
[ Post-Processing ] ──► Hallucination Rejection + Indic Script Repair
        │
        ▼
[ PacketFactory ] ──► Multi-lingual Keyword Priority Banding (CRITICAL / HIGH / MEDIUM / NORMAL)
        │             • Packages into UUID v4 ITantraPacket
        ▼
[ Transport Layer ] ──► Dispatched to Transport (MockTransport today; LoRa / BLE Mesh pluggable)
```

### 2. Receive Path (Packet ➔ Audio)

```
[ Transport Layer ] ──► onPacketReceived
        │
        ▼
[ ReceiverViewModel ] ──► Deduplication + Message State Management
        │
        ├──► [ ReceivedMessageLog ]   Priority-banded cockpit inbox
        ├──► [ CriticalAlertBanner ]  Raised for CRITICAL emergency alerts
        │
        ▼
[ TtsManager ] ──► TtsQueue ──► TtsEngine (sherpa-onnx VITS + android.media.MediaPlayer)
                                • Piper voices: English, Hindi, Malayalam
                                • MMS voices:   Marathi, Gujarati, Kannada, Tamil,
                                                Telugu, Odia, Bengali
```

Both modes are active and share a single transport instance: any message transmitted in Transmit mode can be immediately received and spoken aloud in Receive mode.

---

## 📦 Wire Contract (`ITantraPacket`)

Every finalized transmission is encapsulated in a standardized, transport-agnostic format:

```kotlin
enum class PacketPriority(val value: String) { NORMAL, MEDIUM, HIGH, CRITICAL }

data class ITantraPacket(
    val id: String,           // UUID v4
    val senderId: String,     // Unique hardware/device fingerprint
    val timestamp: Long,      // Epoch millisecond timestamp
    val language: String,     // BCP-47 language tag (e.g., "en-IN", "hi-IN", "mr-IN")
    val text: String,         // Final decoded transcript
    val priority: PacketPriority,
    val isCompressed: Boolean,
)
```

---

## 📁 Repository Structure

```
iTantra/
├── README.md                        # Master project documentation
├── iTantra Design.md                # Architecture decisions and design rationale
├── MIGRATION_AUDIT.md                # Code-level RN -> Kotlin migration audit
├── MIGRATION_STATUS.md               # Phase-by-phase migration execution log
└── android-native/                  # The native Kotlin/Compose Android application
    ├── settings.gradle.kts / build.gradle.kts / gradle.properties
    └── app/
        ├── build.gradle.kts          # Sherpa-onnx/ONNX Runtime native AAR integration
        └── src/main/
            ├── AndroidManifest.xml
            └── java/com/itantra/app/
                ├── MainActivity.kt              # Root Activity, Transmit/Receive switcher
                ├── config/                       # Languages, STT/TTS model registries, VAD config
                ├── audio/                        # AudioRecord capture, PCM framing
                ├── vad/                          # EnergyVad, SentenceSegmenter
                ├── stt/                          # SttEngine, SherpaSttBackend, script repair, filters
                ├── tts/                          # TtsEngine, TtsManager, TtsQueue, TtsModelManager
                ├── packet/                       # ITantraPacket, PriorityClassifier, PacketFactory
                ├── device/                       # Stable device fingerprinting
                ├── transport/                    # Transport interface + MockTransport loopback
                ├── receiver/                      # Received-message contracts
                ├── core/                          # Shared transcription/state types
                ├── viewmodel/                     # AppViewModel, TransmitterViewModel, ReceiverViewModel
                └── ui/
                    ├── screens/                   # TransmitterScreen, ReceiverScreen
                    ├── components/                # PttButton, WaveVisualizer, PacketLog, ModelCard,
                    │                               # ReceivedMessageLog, CriticalAlertBanner, ...
                    └── theme/                      # Tactical dark palette and design tokens
```

---

## ⚙️ Prerequisites & Environment Setup

### 1. Requirements
* **Java Development Kit**: **JDK 17** (e.g., Eclipse Temurin 17 or OpenJDK 17).
  > **Crucial:** Android Gradle Plugin requires JDK 17. Java 21 or Java 25 will cause build errors.
* **Android SDK**: Build-tools, platform-tools (`adb`), and Android SDK Platform 35/36.
* **macOS / Linux / Windows**

### 2. Environment Variables (example)
```bash
export JAVA_HOME="$HOME/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home"
export ANDROID_HOME="$HOME/Library/Android/sdk"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/tools:$PATH"
```

---

## 📥 Installation & Running

### 1. Clone the Repository
```bash
git clone https://github.com/zainab786-shaikh/iTantra.git
cd iTantra/android-native
```

### 2. Build the Debug APK
```bash
./gradlew assembleDebug
```

### 3. Install & Launch on a Connected Android Device
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.itantra.app/.MainActivity
```

No Node.js, npm, Metro bundler, or Expo tooling is required — this is a plain Gradle/Kotlin Android project.

---

## 🧠 Model Storage & Sideloading (Air-Gapped Deployment)

All speech models are saved directly inside the app's internal sandbox storage:

* **STT Models**: `/data/user/0/com.itantra.app/files/itantra-models/<model-id>/`
* **TTS Models**: `/data/user/0/com.itantra.app/files/itantra-tts-models/<model-id>/`

### Sideloading via ADB (Air-Gapped / Offline Deployment)
To manually push an AI4Bharat IndicConformer model onto the device without internet access:

```bash
PKG=com.itantra.app
DEST=files/itantra-models/indicconformer-hi          # Directory for Hindi model

adb shell run-as $PKG mkdir -p $DEST
adb push model.int8.onnx tokens.txt /data/local/tmp/
for f in model.int8.onnx tokens.txt; do
  adb shell "run-as $PKG cp /data/local/tmp/$f $DEST/$f"
done
adb shell rm -f /data/local/tmp/model.int8.onnx /data/local/tmp/tokens.txt
```

---

## ⚠️ Operational Notes

* **Single Microphone Ownership**: Android grants exclusive recording access to one app at a time. If an in-progress phone call, voice recorder, or assistant is active, the app clearly warns the operator instead of failing silently.
* **Single-Pass Decoding**: CTC models produce lowercase text without punctuation, providing maximum decoding speed (~500 ms) critical for tactical voice dispatch.
* **Mock Transport Loopback**: `MockTransport` acts as an in-app loopback interface for testing, allowing transmitted packets to immediately trigger receive-side processing and TTS playback on the same hardware.

---

## 🔌 Swapping the Transport Layer

The transmission layer implements the `Transport` interface (`android-native/app/src/main/java/com/itantra/app/transport/Transport.kt`):

```kotlin
interface Transport {
    val name: String
    suspend fun sendPacket(packet: ITantraPacket): Boolean
    fun isConnected(): Boolean
    fun onConnectionChange(listener: (Boolean) -> Unit): () -> Unit
    fun onPacketReceived(listener: (ITantraPacket) -> Unit): () -> Unit
}
```

To integrate custom hardware (e.g. LoRa SX1262 / SX1276 over UART/USB, BLE mesh, or ESP-NOW), implement the interface and pass it into `AppViewModel` in place of `MockTransport`.

---

## 🤝 Contributing & License

Contributions are welcome! Please create an issue or submit a Pull Request.

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

### Acknowledgments
* [AI4Bharat IndicConformer](https://github.com/AI4Bharat/IndicConformerASR) — State-of-the-art offline Indian language speech recognition models.
* [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — Embedded offline speech recognition and ONNX runtime, consumed here directly via its Kotlin API.
