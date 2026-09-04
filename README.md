# iTantra — Tactical Offline Speech-to-Packet Communication Engine

[![React Native](https://img.shields.io/badge/React%20Native-0.86.3-blue.svg)](https://reactnative.dev/)
[![Expo](https://img.shields.io/badge/Expo%20SDK-57-black.svg)](https://expo.dev/)
[![TypeScript](https://img.shields.io/badge/TypeScript-6.0-blue.svg)](https://www.typescriptlang.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Offline AI](https://img.shields.io/badge/Edge%20AI-100%25%20Offline-orange.svg)]()

**iTantra** is a high-performance, edge-first speech transcription and tactical packet dispatch engine built with **React Native** (Expo SDK 57) and on-device **sherpa-onnx** decoders.

Designed for high-stress, low-connectivity, and disaster/tactical communication environments, iTantra records audio, performs on-device Voice Activity Detection (VAD), transcribes speech into text **completely offline**, categorizes priority based on multi-lingual keyword triggers, and wraps the utterance into a standardized, transport-agnostic data packet for broadcast over radio (LoRa, BLE, or tactical mesh networks).

The app runs in two modes sharing a single transport instance:

* **Transmit** — speech in, `iTantraPacket` out.
* **Receive** — packets in, rendered to the log and spoken aloud through on-device TTS, with a banner for `CRITICAL` traffic.

---

## 🚀 Key Features

* **100% Offline Speech-to-Text**: Decodes audio entirely on-device without cloud APIs or network access using quantized ONNX models (`sherpa-onnx`).
* **Sub-Second Low Latency**: Single-pass **CTC decoders** (NeMo CTC for English, AI4Bharat IndicConformer per Indian language) keep transcription well under a second, suitable for push-to-talk field use. Autoregressive models such as Whisper were an order of magnitude slower on the same hardware.
* **10 Indian & International Languages**:
  * **English (`en-IN`)** [Default], **Hindi (`hi-IN`)**, **Marathi (`mr-IN`)**, **Gujarati (`gu-IN`)**, **Kannada (`kn-IN`)**, **Malayalam (`ml-IN`)**, **Tamil (`ta-IN`)**, **Telugu (`te-IN`)**, **Odia (`or-IN`)**, and **Bengali (`bn-IN`)**.
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

*Tested on a physical device (Realme RMX3771 / Android 14, ARM64).*

Speech-to-text is routed **per language**. English uses a NeMo CTC conformer;
each Indian language uses its own dedicated **AI4Bharat IndicConformer** model
rather than one shared multilingual decoder — a single model asked to identify
the language itself proved to be the main source of error.

| Order | Language | Engine | Model | Latency | Status |
| :---: | :--- | :--- | :--- | :---: | :--- |
| 1 | **English** (`en-IN`) | NeMo CTC | Conformer Medium (int8) | ~500 ms | Verified (Default) |
| 2 | **Hindi** (`hi-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 3 | **Marathi** (`mr-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 4 | **Gujarati** (`gu-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 5 | **Kannada** (`kn-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 6 | **Malayalam** (`ml-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 7 | **Tamil** (`ta-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 8 | **Telugu** (`te-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 9 | **Odia** (`or-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |
| 10 | **Bengali** (`bn-IN`) | IndicConformer | AI4Bharat (int8) | sub-second | Verified |

All ten were confirmed working on device. The English figure is an instrumented
measurement against a reference clip; the Indic entries are functional
verification by a native reader, not stopwatch timings.

### Why per-language models

Two shared multilingual decoders were tried first and both failed, for different
reasons. They remain in the registry as fallbacks:

* **Whisper** cannot render Indic script through this library at all. It uses
  byte-level BPE, and the library converts tokens to strings one at a time, so
  partial UTF-8 sequences collapse to empty strings. Hindi decoded to
  `[" ह","म","े","ं"," ","","","","र"]` — three empty tokens where words belong.
  ASCII is one byte per character, so English is unaffected and no model size
  changes this.
* **Dolphin CTC** produced the right sounds in the wrong script, because it
  identifies language itself and the library exposes no way to pin it. Bengali
  speech returned as `अपनारओबस्थान जान` — correct phonetically, written in
  Devanagari.

Dedicated per-language models remove language identification from the problem
entirely.

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
        │                   • Indic: IndicConformer, one model per language (~188 MB each)
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

### Receive path

```
[ Transport Layer ] ──► onPacketReceived
        │
        ▼
[ useReceiverController ] ──► Dedupe + ReceivedMessage state
        │
        ├──► [ ReceivedMessageLog ]   Priority-banded inbox
        ├──► [ CriticalAlertBanner ]  Raised for CRITICAL traffic
        │
        ▼
[ TtsManager ] ──► TtsQueue ──► TtsEngine (sherpa-onnx VITS)
                                • Piper voices: English, Hindi, Malayalam
                                • MMS voices:   remaining Indian languages
```

Both modes are mounted from `App.tsx` and share one transport instance, so a
packet sent in Transmit mode arrives in Receive mode on the same device.

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
├── App.tsx                          # Root; owns both controllers and the shared transport
├── app.json                         # Expo config, permissions, SDK levels
├── README.md                        # This file
├── Design.md                        # Design rationale and system decisions
├── TRANSMITTER.md                   # Technical spec for the transmit pipeline
├── WHAT_WE_DID.txt                  # Plain-language build log and known limits
├── src/
│   ├── config/
│   │   ├── languages.ts             # 10 language definitions, scripts, accent colours
│   │   ├── models.ts                # STT catalog: NeMo CTC, IndicConformer, Dolphin, Whisper
│   │   ├── modelTypes.ts            # STT engine/model type unions
│   │   ├── ttsModels.ts             # TTS catalog: Piper and MMS voices per language
│   │   ├── ttsModelTypes.ts         # TTS model type unions
│   │   └── vadConfig.ts             # VAD thresholds, padding, pause presets
│   ├── core/
│   │   ├── audio/                   # PCM capture, framing, resampling, WAV debug dump
│   │   ├── device/                  # Stable per-install sender ID
│   │   ├── diagnostics/             # On-device STT round-trip and isolation harnesses
│   │   ├── packet/                  # packetFactory + multilingual priority banding
│   │   ├── receiver/                # Received-message contracts
│   │   ├── stt/                     # Sherpa backend, ModelManager, Indic script repair
│   │   ├── transport/               # Transport interface + MockTransport loopback
│   │   ├── tts/                     # TtsEngine, TtsManager, TtsQueue, TtsModelManager
│   │   ├── vad/                     # Energy VAD, Silero VAD, SentenceSegmenter
│   │   ├── nativeModules.ts         # Runtime probes for Expo Go / missing native modules
│   │   └── types.ts                 # Core pipeline contracts
│   ├── hooks/
│   │   ├── useTransmitterController.ts  # Binds audio, VAD, STT, packets, transport
│   │   └── useReceiverController.ts     # Binds transport intake, log, TTS playback
│   ├── screens/
│   │   ├── TransmitterScreen.tsx    # PTT cockpit: visualizer, telemetry, language rail
│   │   └── ReceiverScreen.tsx       # Inbox: received log, critical alert, TTS status
│   └── ui/
│       ├── components/              # PttButton, WaveVisualizer, PacketLog, ModelCard,
│       │                            # ReceivedMessageLog, CriticalAlertBanner, Dev* rows
│       └── theme.ts                 # Dark palette and design tokens
```

> `Dev*` components and `src/core/diagnostics/` are development-only harnesses for
> exercising STT and TTS directly on device. They are not part of the operator flow.

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

### Weight Sources

**Speech-to-text**

| Purpose | Model | Size | Source |
| :--- | :--- | :--- | :--- |
| English | `sherpa-onnx-nemo-ctc-en-conformer-medium` | ~64 MB | [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models) |
| Each Indian language | `indicconformer-<lang>` (AI4Bharat) | ~188 MB each | [Hugging Face](https://huggingface.co/parismitaglobalsolutions/indicconformer-sherpa-onnx) |
| Fallbacks (unused) | Dolphin CTC, Whisper base/small | 77–610 MB | k2-fsa/sherpa-onnx |

Indic models are per-language, so only the languages actually deployed need to
be installed. Each is a `model.int8.onnx` plus a shared `tokens.txt`.

**Text-to-speech** (receive mode) — Piper voices for English, Hindi and
Malayalam; MMS voices for the remaining Indian languages. See
`src/config/ttsModels.ts`.

### Installing Models

Either press **Download** on the model card in-app, or sideload over ADB for
air-gapped devices. `ModelManager` checks the sideload directory *before* the
downloader, so a hand-placed model always wins.

```bash
PKG=com.itantra.app
DEST=files/itantra-models/indicconformer-hi          # one directory per model id

adb shell run-as $PKG mkdir -p $DEST
adb push model.int8.onnx tokens.txt /data/local/tmp/
for f in model.int8.onnx tokens.txt; do
  adb shell "run-as $PKG cp /data/local/tmp/$f $DEST/$f"
done
adb shell rm -f /data/local/tmp/model.int8.onnx /data/local/tmp/tokens.txt
```

Restart the app afterwards — resolved model paths are cached per model id.

---

## ⚠️ Known Limitations

* **CTC decoders emit no capitalisation or punctuation.** This is the cost of
  single-pass decoding and sub-second latency.
* **Whisper is unusable for Indic script** through `react-native-sherpa-onnx`
  (see *Why per-language models*). It is kept for English only.
* **One app at a time can hold the microphone.** An in-progress call, voice
  recorder or assistant will block capture; the app reports this rather than
  substituting generated audio.
* **`MockTransport` is a loopback**, not a radio. It returns sent packets to the
  receiver on the same device so both pipelines can be exercised end to end. Real
  RF transport is a separate workstream.
* **Development builds hot-reload.** Fast Refresh tears down the native audio
  stream, which can crash it mid-capture. Use a release build for demos.

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
