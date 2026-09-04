# iTantra — Transmitter Engine

Offline speech → structured packet pipeline for React Native (Expo SDK 57, RN 0.86).

## Signal flow

```
mic ──► AudioCaptureService ──► VadBackend ──► SentenceSegmenter ──► SttEngineProvider ──► packetFactory ──► Transport
        16 kHz mono int16       speech prob     START/END_OF_SPEECH    sherpa-onnx           iTantraPacket     sendPacket()
        re-framed to 512         per frame       + pause flush          or simulated          UUID v4
```

| Stage | Module | Implementation |
|---|---|---|
| Capture | `src/core/audio/AudioCaptureService.ts` | `expo-audio` `useAudioStream` at 16 kHz / mono / `int16` |
| Framing | same | resamples if the hardware refuses 16 kHz, re-chops to exact 512-sample frames |
| VAD | `src/core/vad/EnergyVad.ts` | adaptive noise-floor energy + ZCR detector (default) |
| VAD (opt) | `src/core/vad/SileroVad.ts` | Silero v5 via `onnxruntime-react-native` |
| Segmentation | `src/core/vad/SentenceSegmenter.ts` | hysteresis + pre-speech padding + pause flush |
| STT | `src/core/stt/SherpaSttBackend.ts` | `react-native-sherpa-onnx` → `transcribeSamples()`, routed per language |
| Script repair | `src/core/stt/indicScript.ts` | transliterates cross-script output, rejects unrecoverable |
| Model install | `src/core/stt/ModelManager.ts` | in-app download, or side-load dir checked first |
| Packet | `src/core/packet/packetFactory.ts` | UUID v4 via `expo-crypto`, keyword priority banding |
| Transport | `src/core/transport/MockTransport.ts` | logging stand-in behind the `Transport` interface |

## Running it

```bash
npm start
```

The UI runs anywhere. **Real on-device recognition needs an Android development
build** — `react-native-sherpa-onnx` is a TurboModule with prebuilt native
libraries and does not exist in Expo Go or on web:

```bash
npx expo prebuild --clean && npx expo run:android
```

Without a native decoder the engine falls back to `SimulatedSttBackend`. **That
fallback does not listen.** It performs no recognition at all: it reports the
length and peak level of the audio it received and states that no decoder is
installed. It deliberately does not emit realistic sentences, because output
that looks like a bad transcription is worse than output that is obviously
absent.

## Decoder routing

There is no single model that serves both English and Indic well, so
`resolveModelForLanguage()` routes by language:

| Language | Model | Source |
|---|---|---|
| English | `sherpa-onnx-nemo-ctc-en-conformer-medium` (~64 MB) | k2-fsa release |
| 9 Indian languages | `indicconformer-<lang>` (AI4Bharat, ~188 MB each) | Hugging Face |

Dolphin and Whisper remain in `STT_MODELS` below these, as fallbacks. Neither is
used, and both are there for a reason worth keeping on record:

**Whisper cannot produce Indic script through this library.** It uses byte-level
BPE, and the library converts tokens to strings individually, so partial UTF-8
sequences collapse to empty strings. Verified on device — Hindi decoded to:

```
tokens: [" ह","म","े","ं"," ","","","","र"]
                          ^^  ^^  ^^  empty
```

ASCII is one byte per character, so English is unaffected. No Whisper model size
changes this, and `0.4.3` is the latest published version of the library.

**Dolphin CTC writes the right sounds in the wrong script.** It identifies
language itself with no way to pin it; Bengali speech returned as
`अपनारओबस्थान जान` — phonetically correct, in Devanagari. `indicScript.ts` was
written to repair exactly this, and still runs as a safety net: the Indic Unicode
blocks are ISCII-aligned, so cross-script conversion is a code-point shift.
Output in an unrelated script (Arabic, Cyrillic) has no such correspondence and
is rejected rather than emitted as a fragment.

Per-language models remove language identification from the problem entirely,
which is why all ten languages now work.

## Installing models

Press **Download** on the model card, or side-load over ADB. `ModelManager`
checks `<documents>/itantra-models/<model-id>` *before* the downloader, so a
hand-placed model always wins — this is the path used for air-gapped demos.

```bash
PKG=com.itantra.app
DEST=files/itantra-models/indicconformer-hi

adb shell run-as $PKG mkdir -p $DEST
adb push model.int8.onnx tokens.txt /data/local/tmp/
for f in model.int8.onnx tokens.txt; do
  adb shell "run-as $PKG cp /data/local/tmp/$f $DEST/$f"
done
```

Restart the app afterwards; resolved paths are cached per model id.

## Measured on device

Realme RMX3771, Android 14, ARM64:

| | |
|---|---|
| Capture | 16 kHz mono PCM-16 (confirmed by Android's audio subsystem) |
| English decode | ~500 ms, exact on the reference clip |
| Indic decode | sub-second, all nine verified working |

CTC decoders emit no capitalisation or punctuation — the cost of single-pass
decoding.

## Swapping the transport

`MockTransport` is only a default. Implement `Transport` and pass it in:

```ts
const controller = useTransmitterController({ transport: new LoRaTransport() });
```

## Tuning

`src/config/vadConfig.ts` holds the segmenter tuning. The end-of-speech pause
(600 / 750 / 1000 ms) is also exposed in the UI, since it is the one knob that
needs adjusting in the field.
