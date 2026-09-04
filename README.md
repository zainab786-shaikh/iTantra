# iTantra — Tactical Offline Speech-to-Packet Communication Engine

[![React Native](https://img.shields.io/badge/React%20Native-0.86.3-blue.svg)](https://reactnative.dev/)
[![Expo](https://img.shields.io/badge/Expo%20SDK-57-black.svg)](https://expo.dev/)
[![TypeScript](https://img.shields.io/badge/TypeScript-6.0-blue.svg)](https://www.typescriptlang.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Offline STT](https://img.shields.io/badge/Offline%20STT-AI4Bharat%20IndicConformer-brightgreen.svg)]()
[![Offline TTS](https://img.shields.io/badge/Offline%20TTS-Sherpa--ONNX%20VITS-orange.svg)]()

**iTantra** is a high-performance, edge-first tactical voice communication engine built with **React Native** (Expo SDK 57) and on-device **sherpa-onnx** ONNX Runtime decoders.

Designed for high-stress, low-connectivity, disaster-response, and tactical military/defense environments, iTantra records audio, performs on-device Voice Activity Detection (VAD), transcribes speech into text **completely offline**, categorizes priority based on multi-lingual keyword triggers, and wraps the utterance into a standardized, transport-agnostic data packet for broadcast over radio (LoRa, BLE, or tactical mesh networks).

The app runs a complete bidirectional tactical communication system:

* **Transmit (STT)** — Speech in ➔ On-Device VAD ➔ Offline STT ➔ Priority Banding ➔ `iTantraPacket` out.
* **Receive (TTS)** — Packets in ➔ Priority Filtering ➔ On-Device TTS Speech Synthesis ➔ Audio Spoken Aloud.

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
  * Adaptive noise-floor energy + Zero Crossing Rate (ZCR) detector with optional Silero VAD v5 ONNX support.
  * Hysteresis smoothing, configurable pause flush timing (600 ms, 750 ms, 1000 ms), and pre-speech audio buffering (ensuring leading syllables are never clipped).
* **Deterministic Priority Banding**:
  * Real-time automated priority tagging: `CRITICAL`, `HIGH`, `MEDIUM`, or `NORMAL`.
  * Transparent, rule-based multilingual keyword triggers across all 10 supported languages (detecting emergency, fire, attack, injury, or rescue terms in native scripts).
* **Field-Ready Dark Cockpit UI**:
  * Sleek, high-contrast tactical dark aesthetic with audio wave visualizer, live audio level monitor, latency telemetry strip, and connection status indicators.

---

## 📊 Measured Performance on Real Hardware (STT + TTS)

*Tested and verified on a physical device (**Realme RMX3771 / Android 14, ARM64**).*

Every language is mapped to a dedicated speech-to-text decoder and text-to-speech voice:

| Order | Language | Code | STT Engine (Speech-to-Text) | STT Accuracy | STT Latency | TTS Engine (Text-to-Speech) | Status |
| :---: | :--- | :---: | :--- | :---: | :---: | :--- | :---: |
| **1** | **English** (Default) | `en-IN` | NeMo CTC Conformer Medium (int8) | **Exact** | ~500 ms | Piper VITS (`en_US-lessac`) | **Verified** |
| **2** | **Hindi** | `hi-IN` | AI4Bharat IndicConformer (int8) | **Exact (Devanagari)** | sub-second | Piper VITS (`hi_IN-pratham`) | **Verified** |
| **3** | **Marathi** | `mr-IN` | AI4Bharat IndicConformer (int8) | **Exact (Devanagari)** | sub-second | Meta MMS VITS (`mar`) | **Verified** |
| **4** | **Gujarati** | `gu-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`guj`) | **Verified** |
| **5** | **Kannada** | `kn-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`kan`) | **Verified** |
| **6** | **Malayalam** | `ml-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Piper VITS (`ml_IN-arjun`) | **Verified** |
| **7** | **Tamil** | `ta-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`tam`) | **Verified** |
| **8** | **Telugu** | `te-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`tel`) | **Verified** |
| **9** | **Odia** | `or-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`ory`) | **Verified** |
| **10** | **Bengali** | `bn-IN` | AI4Bharat IndicConformer (int8) | **High / Exact** | sub-second | Meta MMS VITS (`ben`) | **Verified** |

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
[ Microphone Input ] (16 kHz mono int16 via expo-audio)
        │
        ▼
[ AudioCaptureService ] ──► Frame Resampling & 512-sample Slicing
        │
        ▼
[ Voice Activity Detector ] ──► Adaptive Energy + ZCR (or Silero VAD v5)
        │
        ▼
[ SentenceSegmenter ] ──► Pre-speech Padding + Dynamic Audio Buffering + Pause Flush
        │
        ▼
[ STT Engine Provider ] ──► TurboModule ONNX Runtime (sherpa-onnx)
        │                   • English: NVIDIA NeMo CTC (~64 MB)
        │                   • Indic: AI4Bharat IndicConformer (~188 MB each)
        │
        ▼
[ Post-Processing ] ──► Hallucination Rejection + Indic Script Normalization
        │
        ▼
[ packetFactory ] ──► Multi-lingual Keyword Priority Banding (CRITICAL / HIGH / MED / NORM)
        │             • Packages into UUID v4 iTantraPacket
        ▼
[ Transport Layer ] ──► Dispatched to Transport (MockTransport / LoRa / BLE Mesh)
```

### 2. Receive Path (Packet ➔ Audio)

```
[ Transport Layer ] ──► onPacketReceived
        │
        ▼
[ useReceiverController ] ──► Deduplication + Message State Management
        │
        ├──► [ ReceivedMessageLog ]   Priority-banded cockpit inbox
        ├──► [ CriticalAlertBanner ]  Raised for CRITICAL emergency alerts
        │
        ▼
[ TtsManager ] ──► TtsQueue ──► TtsEngine (sherpa-onnx VITS)
                                • Piper voices: English, Hindi, Malayalam
                                • MMS voices:   Marathi, Gujarati, Kannada, Tamil,
                                                Telugu, Odia, Bengali
```

Both modes are active and share a single transport instance: any message transmitted in Transmit mode can be immediately received and spoken aloud in Receive mode.

---

## 📦 Wire Contract (`iTantraPacket`)

Every finalized transmission is encapsulated in a standardized, transport-agnostic format:

```typescript
export type PacketPriority = 'NORMAL' | 'MEDIUM' | 'HIGH' | 'CRITICAL';

export interface iTantraPacket {
  id: string;             // UUID v4 (RFC 4122)
  senderId: string;       // Unique hardware/device fingerprint
  timestamp: number;      // Epoch millisecond timestamp
  language: string;       // BCP-47 language tag (e.g., 'en-IN', 'hi-IN', 'mr-IN')
  text: string;           // Final decoded transcript
  priority: PacketPriority; // Computed priority band (CRITICAL / HIGH / MEDIUM / NORMAL)
  isCompressed: boolean;  // Wire payload compression flag
}
```

---

## 📁 Repository Structure

```
iTantra/
├── App.tsx                          # Root container; manages shared transport and screens
├── app.json                         # Expo configuration, native permissions, SDK levels
├── README.md                        # Master project documentation
├── Design.md                        # Architecture decisions and design rationale
├── TRANSMITTER.md                   # Technical specification for the transmit pipeline
├── WHAT_WE_DID.txt                  # Plain-language build log and verification notes
├── src/
│   ├── config/
│   │   ├── languages.ts             # 10 language definitions, scripts, accent tokens
│   │   ├── models.ts                # STT catalog: NeMo CTC, AI4Bharat IndicConformer
│   │   ├── modelTypes.ts            # STT engine and descriptor type unions
│   │   ├── ttsModels.ts             # TTS catalog: Piper and Meta MMS voice registry
│   │   ├── ttsModelTypes.ts         # TTS model type unions
│   │   └── vadConfig.ts             # VAD thresholds, padding, pause presets
│   ├── core/
│   │   ├── audio/                   # Audio streaming, PCM framing, resampling, WAV debug
│   │   ├── device/                  # Stable device fingerprinting
│   │   ├── diagnostics/             # On-device STT & TTS diagnostic test harnesses
│   │   ├── packet/                  # packetFactory + multilingual keyword priority rules
│   │   ├── receiver/                # Received-message contracts and deduplication
│   │   ├── stt/                     # Sherpa backend, ModelManager, Indic script repair
│   │   ├── transport/               # Transport interface + MockTransport loopback
│   │   ├── tts/                     # TtsEngine, TtsManager, TtsQueue, TtsModelManager
│   │   ├── vad/                     # Energy VAD, Silero VAD, SentenceSegmenter
│   │   ├── nativeModules.ts         # Runtime probes for native modules
│   │   └── types.ts                 # Core pipeline contracts
│   ├── hooks/
│   │   ├── useTransmitterController.ts  # Transmitter state machine (Audio -> VAD -> STT -> RF)
│   │   └── useReceiverController.ts     # Receiver state machine (RF -> Inbox -> TTS Speech)
│   ├── screens/
│   │   ├── TransmitterScreen.tsx    # Transmitter cockpit: mic, visualizer, telemetry, language rail
│   │   └── ReceiverScreen.tsx       # Receiver inbox: message list, critical banner, TTS controls
│   └── ui/
│       ├── components/              # PttButton, WaveVisualizer, PacketLog, ModelCard,
│       │                            # ReceivedMessageLog, CriticalAlertBanner
│       └── theme.ts                 # Tactical dark palette and design tokens
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
Because `react-native-sherpa-onnx` compiles native C++ binaries, the app must run as a **native Android build** (not inside Expo Go):

Connect your Android phone via USB or Wi-Fi ADB with USB debugging enabled, then execute:
```bash
npx expo run:android
```

To start Metro bundler during development:
```bash
npx expo start --dev-client
```

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
* [AI4Bharat IndicConformer](https://github.com/AI4Bharat/IndicConformerASR) — State-of-the-art offline Indian language speech recognition models.
* [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) — Embedded offline speech recognition and ONNX runtime.
* [thewh1teagle/react-native-sherpa-onnx](https://github.com/thewh1teagle/react-native-sherpa-onnx) — React Native TurboModule bindings.
* [Expo](https://expo.dev) — React Native ecosystem.
