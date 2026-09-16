# iTantra — Tactical Offline Speech-to-Packet Communication Engine

[![Kotlin](https://img.shields.io/badge/Kotlin-2.1.20-blue.svg)](https://kotlinlang.org/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![Jetpack Compose](https://img.shields.io/badge/Jetpack%20Compose-Material3-black.svg)](https://developer.android.com/jetpack/compose)
[![Android](https://img.shields.io/badge/Android-minSdk%2024-3DDC84.svg)](https://developer.android.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Offline STT](https://img.shields.io/badge/Offline%20STT-AI4Bharat%20IndicConformer-brightgreen.svg)]()
[![Offline TTS](https://img.shields.io/badge/Offline%20TTS-Sherpa--ONNX%20VITS-orange.svg)]()

**iTantra** is a high-performance, edge-first tactical voice communication engine built natively for Android in **Kotlin** and **C++17** with **Jetpack Compose**, calling directly into on-device **sherpa-onnx** ONNX Runtime decoders and a custom native C++ cryptographic/compression core via JNI.

Designed for high-stress, low-connectivity, disaster-response, and tactical military/defense environments, iTantra records audio, performs on-device Voice Activity Detection (VAD), transcribes speech into text **completely offline**, categorizes priority based on multi-lingual keyword triggers, compresses and encrypts payloads in C++, and wraps the utterance into a standardized, transport-agnostic data packet for broadcast over wireless UDP networks or radio hardware (LoRa, BLE Mesh).

The app runs a complete bidirectional tactical communication system:

* **Transmit (STT)** — Speech in ➔ On-Device VAD ➔ Offline STT ➔ C++ Native Codec & Encryption ➔ Priority Banding ➔ `ITantraPacket` out over UDP/Radio.
* **Receive (TTS)** — Packets in ➔ C++ Native Decryption & Codec ➔ Priority Filtering ➔ On-Device TTS Speech Synthesis ➔ Audio Spoken Aloud.

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
* **Native C++ Engine & Monocypher Encryption (`libitantra-native.so`)**:
  * Core pipeline powered by a zero-allocation C++17 engine linked via JNI (`NativeEngine` / `NativeBridge`).
  * End-to-end AEAD encryption, key derivation, and replay protection backed by Monocypher.
  * Tier-1 and Tier-2 phrase/subword compression codecs delivering up to 10x payload size reduction over the air.
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
* **Wireless P2P UDP Transport**:
  * Infrastructure-less peer-to-peer communication over local Wi-Fi or Hotspot (`UdpTransport` on port `47821`).
  * Auto-learning socket address discovery and keepalives.
  * Simulated rate-limiter (250 bps LoRa SF12 simulation) for tactical airtime analysis.
* **Intelligent Sentence Segmentation & VAD**:
  * Adaptive noise-floor energy + Zero Crossing Rate (ZCR) detector.
  * Hysteresis smoothing, configurable pause flush timing (600 ms, 750 ms, 1000 ms), and pre-speech audio buffering.
* **Field-Ready Dark Cockpit UI**:
  * Sleek, high-contrast tactical dark aesthetic with audio wave visualizer, live audio level monitor, response-pause control, link setup, and connection status indicators.

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
[ NativeEngine (C++) ] ──► Phrase / Context Normalization + Compression Codec + AEAD Encryption
        │
        ▼
[ PacketFactory ] ──► Multilingual Keyword Priority Banding (CRITICAL / HIGH / MEDIUM / NORMAL)
        │             • Packages into UUID v4 ITantraPacket
        ▼
[ Transport Layer ] ──► UdpTransport (Port 47821 over Wi-Fi/Hotspot; LoRa / BLE Mesh pluggable)
```

### 2. Receive Path (Packet ➔ Audio)

```
[ UdpTransport / Radio ] ──► onPacketReceived
        │
        ▼
[ NativeEngine (C++) ] ──► AEAD Decryption + Replay Verification + Decompression Codec
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
    val text: String,         // Decoded transcript
    val priority: PacketPriority,
    val isCompressed: Boolean,
    val payload: ByteArray,   // Encrypted/compressed binary payload
)
```

---

## 📁 Repository Structure

```
iTantra/
├── README.md                        # Master project documentation
├── native/                          # Core C++17 engine library
│   ├── CMakeLists.txt               # Host & native C++ build configuration
│   ├── src/                         # AEAD crypto, phrase codecs, context managers
│   └── test/                        # Conformance & golden vector test suites
└── android-native/                  # The native Kotlin/Compose Android application
    ├── settings.gradle.kts / build.gradle.kts / gradle.properties
    └── app/
        ├── build.gradle.kts          # Sherpa-onnx native integration + NDK CMake
        └── src/main/
            ├── cpp/                 # JNI bridge (itantra-native.cpp)
            ├── AndroidManifest.xml
            └── java/com/itantra/app/
                ├── MainActivity.kt              # Root Activity, Transmit/Receive/Link switcher
                ├── native/                       # NativeEngine & NativeBridge JNI wrappers
                ├── config/                       # Languages, STT/TTS model registries, VAD config
                ├── audio/                        # AudioRecord capture, PCM framing
                ├── vad/                          # EnergyVad, SentenceSegmenter
                ├── stt/                          # SttEngine, SherpaSttBackend, script repair
                ├── tts/                          # TtsEngine, TtsManager, TtsQueue, TtsModelManager
                ├── packet/                       # ITantraPacket, PriorityClassifier, PacketFactory
                ├── device/                       # Stable device fingerprinting
                ├── transport/                    # UdpTransport, ThrottledTransport, MockTransport
                ├── receiver/                      # ReceivedMessage contracts
                ├── viewmodel/                     # AppViewModel, TransmitterViewModel, ReceiverViewModel
                └── ui/
                    ├── screens/                   # TransmitterScreen, ReceiverScreen, LinkScreen
                    ├── components/                # PttButton, WaveVisualizer, PacketLog, ModelCard,
                    │                               # ReceivedMessageLog, TtsStatusCard, AirtimeRace, ...
                    └── theme/                      # Tactical dark palette and design tokens
```

---

## ⚙️ Prerequisites & Environment Setup

### 1. Requirements
* **Java Development Kit**: **JDK 17** (e.g., Eclipse Temurin 17 or OpenJDK 17).
* **Android SDK & NDK**: CMake 3.22.1+, NDK r26+, Android SDK Platform 35/36.
* **macOS / Linux / Windows**

### 2. Environment Variables (example)
```bash
export JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/tools:$PATH"
```

---

## 📥 Building & Running

### 1. Build the Debug APK
```bash
cd android-native
./gradlew assembleDebug
```

### 2. Install & Launch on Connected Android Devices
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.itantra.app/.MainActivity
```

---

## 🤝 Contributing & License

Contributions are welcome! Please create an issue or submit a Pull Request.

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

### Acknowledgments
* [AI4Bharat IndicConformer](https://github.com/AI4Bharat/IndicConformerASR) — State-of-the-art offline Indian language speech recognition models.
* [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — Embedded offline speech recognition and ONNX runtime.
* [Monocypher](https://monocypher.org/) — Lightweight C crypto library used for AEAD encryption.
