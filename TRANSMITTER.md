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
| STT | `src/core/stt/SherpaSttBackend.ts` | `react-native-sherpa-onnx` → `transcribeSamples()`, Whisper base multilingual |
| Model install | `src/core/stt/ModelManager.ts` | in-app download from the `asr-models` release |
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

## Installing the speech model

The decoder is **Whisper base, multilingual** (`sherpa-onnx-whisper-base`,
~198 MB download, ~154 MB installed as int8). There are two ways to install it.

### 1. In-app download

Press **DOWNLOAD** on the model card. Needs internet once; offline thereafter.

The download deliberately bypasses `react-native-sherpa-onnx`'s model registry
and fetches the release archive directly. The registry resolves ids by listing a
GitHub release's assets and returned `Unknown model id: sherpa-onnx-whisper-base`
for an asset that demonstrably exists — that release carries 498 assets and this
one sits at index 100, right on a pagination boundary.

### 2. Side-load over adb (no internet on the phone)

`ModelManager` checks `<documents>/itantra-models/<model-id>` *before* the
downloader, so a model can be installed by hand. This is the path used for demos
where the phone has no connectivity:

```bash
curl -LO https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-base.tar.bz2
tar -xjf sherpa-onnx-whisper-base.tar.bz2
PKG=com.itantra.app
DEST=files/itantra-models/sherpa-onnx-whisper-base
adb shell mkdir -p /data/local/tmp/wb
adb shell run-as $PKG mkdir -p $DEST
for f in base-encoder.int8.onnx base-decoder.int8.onnx base-tokens.txt; do
  adb push sherpa-onnx-whisper-base/$f /data/local/tmp/wb/$f
  adb shell "run-as $PKG cp /data/local/tmp/wb/$f $DEST/$f"
done
adb shell rm -rf /data/local/tmp/wb
```

Only the int8 weights and the tokens file are needed; the full-precision `.onnx`
files in the archive can be skipped. Restart the app afterwards — the resolved
path is cached.

> **Note.** There is no dedicated Hindi or IndicConformer model in the
> sherpa-onnx release. An earlier version of `src/config/models.ts` named one; it
> did not exist. Hindi and the other Indian languages are served by multilingual
> Whisper, whose target language is set from the language selector.

Whisper takes its language at construction time, so switching languages rebuilds
the recogniser — that is why `setLanguage` warms the decoder immediately rather
than waiting for the next utterance.

To use a different model, change `PRIMARY_MODEL` in `src/config/models.ts`. The
`id` must match the release asset name with `.tar.bz2` stripped.

## Measured on device

Realme RMX3771, Whisper base int8, English:

| | |
|---|---|
| Capture | 16 kHz mono PCM-16 (confirmed by Android's audio subsystem) |
| Decode | ~1450 ms for 2.8 s of speech |
| Engine | `SHERPA-ONNX` |

## Swapping the transport

`MockTransport` is only a default. Implement `Transport` and pass it in:

```ts
const controller = useTransmitterController({ transport: new LoRaTransport() });
```

## Tuning

`src/config/vadConfig.ts` holds the segmenter tuning. The end-of-speech pause
(600 / 750 / 1000 ms) is also exposed in the UI, since it is the one knob that
needs adjusting in the field.
