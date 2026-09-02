# iTantra — Tactical Offline Speech-to-Packet Transmitter Engine

[![React Native](https://img.shields.io/badge/React%20Native-0.86.3-blue.svg)](https://reactnative.dev/)
[![Expo](https://img.shields.io/badge/Expo%20SDK-57-black.svg)](https://expo.dev/)
[![TypeScript](https://img.shields.io/badge/TypeScript-6.0-blue.svg)](https://www.typescriptlang.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Offline AI](https://img.shields.io/badge/Edge%20AI-100%25%20Offline-orange.svg)]()

**iTantra** is a high-performance, edge-first speech transcription and tactical packet dispatch engine built with **React Native** (Expo SDK 57) and on-device **sherpa-onnx** decoders.

Designed for high-stress, low-connectivity, and disaster/tactical communication environments, iTantra records audio, performs on-device Voice Activity Detection (VAD), transcribes speech into text **completely offline**, categorizes priority based on multi-lingual keyword triggers, and wraps the utterance into a standardized, transport-agnostic data packet for broadcast over radio (LoRa, BLE, or tactical mesh networks).

---

## 🚀 Key Features

* **100% Offline Speech-to-Text**: Decodes audio entirely on-device without cloud APIs or network access using quantized ONNX models (`sherpa-onnx`).
* **Sub-Second Low Latency**: Utilizes single-pass **CTC models** (NeMo CTC & Dolphin CTC) achieving ultra-fast transcribing times (~500–600 ms) suitable for push-to-talk (PTT) field operations.
* **10 Indian & International Languages**:
  * **English (`en-IN`)**, **Hindi (`hi-IN`)**, **Marathi (`mr-IN`)**, **Tamil (`ta-IN`)**, **Bengali (`bn-IN`)**, **Telugu (`te-IN`)**, **Kannada (`kn-IN`)**, **Gujarati (`gu-IN`)**, **Malayalam (`ml-IN`)**, and **Odia (`or-IN`)**.
* **Intelligent Sentence Segmentation & VAD**:
  * Adaptive noise-floor energy + Zero Crossing Rate (ZCR) detector with optional Silero VAD v5 ONNX support.
  * Hysteresis smoothing, configurable pause flush timing (600 ms, 750 ms, 1000 ms), and pre-speech audio buffering (ensuring leading syllables are never clipped).
* **Deterministic Priority Banding**:
  * Real-time automated priority tagging: `CRITICAL`, `HIGH`, `MEDIUM`, or `NORMAL`.
  * Transparent, rule-based multilingual keyword triggers across all 10 supported languages (e.g., detecting emergency, fire, attack, injury, or rescue terms in native scripts).
* **Indic Script Normalization & Repair**:
  * Cross-script Unicode transposition to recover Indic phonetic sounds misidentified across closely related Brahmic scripts.
  * Subtitle hallucination filter (suppressing artifact tokens like `[Music]` or `Thanks for watching`).
* **Transport Agnostic Wire Format**:
  * Standardized UUID-tagged `iTantraPacket` ready for any RF transport layer (e.g., LoRa, Bluetooth Mesh, Wi-Fi Direct, or Cellular).
* **Field-Ready Dark UI**:
  * Sleek, high-contrast dark aesthetic with audio wave visualizer, live audio level monitor, latency telemetry strip, and connection status indicators.

---

## 📊 Measured Performance on Real Hardware

*Tested on physical device (Realme RMX3771 / Android 14, ARM64):*

| Language | Engine | Model Architecture | Average Latency | Transcription Fidelity |
| :--- | :--- | :--- | :---: | :--- |
| **English** | NeMo CTC | Conformer Medium (int8) | **~500 ms** | High / Exact |
| **Hindi** | Dolphin CTC | Multilingual Conformer (int8) | **~546 ms** | High / Exact |
| **Tamil** | Dolphin CTC | Multilingual Conformer (int8) | **~555 ms** | High / Exact |
| **Bengali** | Dolphin CTC | Multilingual Conformer (int8) | **~591 ms** | High / Exact |
| **Telugu** | Dolphin CTC | Multilingual Conformer (int8) | **~617 ms** | Moderate (Script repair active) |
| **Kannada** | Dolphin CTC | Multilingual Conformer (int8) | **~601 ms** | Fair |
| **Marathi** | Dolphin CTC | Multilingual Conformer (int8) | **Fast** | High / Exact |
| **Gujarati, Malayalam, Odia** | Dolphin CTC | Multilingual Conformer (int8) | **Fast** | Model mapped |

---

## 🛠 Architecture & Signal Flow

```
[ Microphone Input ] (16 kHz mono int16 via expo-audio)
        │
        ▼
[ AudioCaptureService ] ──► Frame Resampling & 512-sample Slicing
        │
        ▼
[ Voice Activity Detector ] ──► Adaptive Noise Floor + ZCR (EnergyVad / SileroVad)
        │
        ▼
[ SentenceSegmenter ] ──► Pre-speech Padding + Speech Buffering + Pause Detection
        │
        ▼
[ STT Engine Provider ] ──► TurboModule ONNX Runtime (sherpa-onnx)
        │                   • English: NeMo CTC (~64 MB)
        │                   • Indic: Dolphin CTC (~183 MB)
        │
        ▼
[ Post-Processing ] ──► Hallucination Rejection + Indic Unicode Script Repair
        │
        ▼
[ packetFactory ] ──► Multi-lingual Keyword Priority Banding (CRITICAL / HIGH / MED / NORM)
        │             • Generates UUID v4 iTantraPacket
        ▼
[ Transport Layer ] ──► Dispatched to Transport (MockTransport / LoRa / BLE Mesh)
```

---

## 📦 Wire Contract (`iTantraPacket`)

Every finalized audio segment is packaged into a compact payload:

```typescript
export type PacketPriority = 'NORMAL' | 'MEDIUM' | 'HIGH' | 'CRITICAL';

export interface iTantraPacket {
  id: string;             // UUID v4 (RFC 4122)
  senderId: string;       // Unique hardware/device fingerprint
  timestamp: number;      // Epoch millisecond timestamp
  language: string;       // BCP-47 language tag (e.g., 'en-IN', 'hi-IN')
  text: string;           // Final decoded & repaired transcript
  priority: PacketPriority; // Priority band determined by triggers
  isCompressed: boolean;  // Wire payload compression flag
}
```

---

## 📁 Repository Structure

```
iTantra/
├── App.tsx                          # Root application entry point
├── app.json                         # Expo configuration and permissions
├── package.json                     # Dependencies & scripts
├── TRANSMITTER.md                   # Technical specification & signal pipeline docs
├── WHAT_WE_DID.txt                  # Changelog, performance benchmarks, and debug notes
├── src/
│   ├── config/
│   │   ├── languages.ts             # 10 language definitions, script mappings, & colors
│   │   ├── models.ts                # Model catalog (NeMo, Dolphin, Whisper)
│   │   ├── modelTypes.ts            # STT engine and model type definitions
│   │   └── vadConfig.ts             # VAD threshold, padding, and pause configurations
│   ├── core/
│   │   ├── audio/                   # PCM streaming, resampling, WAV debug logger
│   │   ├── device/                  # Device fingerprinting & unique node ID generator
│   │   ├── packet/                  # packetFactory and multilingual priority classification
│   │   ├── stt/                     # Sherpa-onnx backend, ModelManager, Indic script repair
│   │   ├── transport/               # Transport interface & MockTransport
│   │   ├── vad/                     # Energy VAD, Silero VAD, and SentenceSegmenter
│   │   └── types.ts                 # Core TypeScript contracts and pipeline interfaces
│   ├── hooks/
│   │   └── useTransmitterController.ts # State machine binding audio, VAD, STT, and UI
│   ├── screens/
│   │   └── TransmitterScreen.tsx    # Primary cockpit view with PTT control & telemetry
│   └── ui/
│       ├── components/              # PttButton, WaveVisualizer, PacketLog, ModelCard, etc.
│       └── theme.ts                 # Tactical dark color palette & styling tokens
```

---

## ⚙️ Prerequisites & Environment Setup

### 1. Requirements
* **Node.js**: `v18+` or `v20+`
* **Java Development Kit**: **JDK 17** (e.g., Eclipse Temurin 17 or OpenJDK 17).
  > **Crucial:** Android Gradle Plugin requires JDK 17. Java 21 or Java 25 will cause build errors.
* **Android SDK**: Build-tools, platform-tools (`adb`), and Android SDK Platform 34/35.
* **macOS / Linux / Windows**

### 2. Environment Variables (macOS/zsh example)
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
cd iTantra
```

### 2. Install Node Dependencies
```bash
npm install
```

### 3. Verify TypeScript Types
```bash
npm run typecheck
```

### 4. Run on Android Device
Because `react-native-sherpa-onnx` contains native C++ binaries and TurboModules, it must be run as a **native Android build** (not inside standard Expo Go):

Connect your Android phone via USB with USB debugging enabled, then execute:
```bash
npx expo run:android
```

To start Metro bundler during development:
```bash
npm start
# or
npx expo start --dev-client
```

---

## 🧠 Speech Models Setup

Models can be installed via **In-App Download** or directly **sideloaded via ADB** for air-gapped devices.

### Official Weights Source
Models are fetched from the [`k2-fsa/sherpa-onnx`](https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models) release registry:
* **English:** `sherpa-onnx-nemo-ctc-en-conformer-medium` (~64 MB)
* **Indic (All 9 Indian Languages):** `sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02` (~183 MB)
* **Whisper Base (Alternative):** `sherpa-onnx-whisper-base` (~198 MB)

### Sideloading Models via ADB (Air-Gapped / Offline Deployment)
To manually load models directly onto an Android device without internet access:

```bash
# 1. Download model tarball
curl -LO https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02.tar.bz2
tar -xjf sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02.tar.bz2

# 2. Push files to app's internal document storage
PKG=com.itantra.app
DEST=files/itantra-models/sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02

adb shell mkdir -p /data/local/tmp/model
adb shell run-as $PKG mkdir -p $DEST
adb push sherpa-onnx-dolphin-small-ctc-multi-lang-int8-2025-04-02/* /data/local/tmp/model/
adb shell "run-as $PKG cp -r /data/local/tmp/model/* $DEST/"
adb shell rm -rf /data/local/tmp/model
```

---

## 🔌 Swapping the Transport Layer

The transmission layer implements the `Transport` interface defined in `src/core/transport/Transport.ts`:

```typescript
export interface Transport {
  sendPacket(packet: iTantraPacket): Promise<void>;
  getStatus(): TransportStatus;
}
```

To integrate custom hardware (e.g. LoRa SX1262 / SX1276 over UART/USB, BLE mesh, or ESP-NOW), implement the interface and inject it into the controller:

```typescript
import { useTransmitterController } from './src/hooks/useTransmitterController';
import { LoRaTransport } from './transports/LoRaTransport';

const controller = useTransmitterController({
  transport: new LoRaTransport({ frequency: 868e6, txPower: 22 })
});
```

---

## 🤝 Contributing & License

Contributions are welcome! Please create an issue or submit a Pull Request.

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

### Acknowledgments
* [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — Embedded offline speech recognition and ONNX runtime.
* [thewh1teagle/react-native-sherpa-onnx](https://github.com/thewh1teagle/react-native-sherpa-onnx) — React Native TurboModule bindings.
* [Expo](https://expo.dev) — React Native ecosystem.
