# The Kotlin ↔ Native Boundary

Required by the implementation plan's Phase 0 exit criteria, and brought up to
date in Phase 11. **§0 is the boundary as it stands after Phase 11.** §1–§5 are
the Phase 0 record, kept for history; where they disagree with §0, §0 wins.

---

## 0. After Phase 11

```
Kotlin (com.itantra.app)                         C++ (native/src)
------------------------                         ----------------
TransmitterViewModel.handleSegment
  STT text ─► transmit()
    buildNativePackets()          packet/PacketFactory.kt
      NativeEngine.sendUtterance ─JNI─► api/engine.cpp  Engine::send_utterance
         (ONE call per utterance)         extract_utterance → per clause:
                                          select_tier → aead_seal → commit
      ◄── NativeClauseEncoding[] ───────  one result per clause
    ITantraPacket(payload = sealed) ─► ThrottledTransport ─► MockTransport (loopback)
                                                               │
ReceiverViewModel.handlePacket ◄───────────────────────────────┘
  NativeEngine.receive ─JNI─► api/engine.cpp  Engine::receive
     (ONE call per payload)      receive_native_payload (receiver §2)
  ◄── NativeReceiveResult ─────  receiver §7: text · language · mode ·
                                 priority · unresolved[] · status
  → ReceivedMessage → TtsManager (voice from the output's language id)
```

| | |
|---|---|
| Library | `libitantra-native.so`, `arm64-v8a`, `armeabi-v7a`, `x86`, `x86_64` |
| JNI adapter | `android-native/app/src/main/cpp/itantra-native.cpp` — conversion only |
| Engine | `native/src/api/engine.h` — in `ITANTRA_CORE_SOURCES`, host-tested |
| Kotlin wrapper | `app/.../native/NativeBridge.kt` (load, packs), `NativeEngine.kt` (calls, result types) |
| Packs | synthetic fixture packs (hi, ta, en), built by `itantra-packc`, packaged as `assets/itantra/packs/` |
| Session | per-launch loopback: random PSK + HELLO nonces → pinned KDF; this phone's receiver authenticates its own stream |
| Link | `AppViewModel.USE_REAL_LINK = false` — the in-app loopback, until HELLO (Phase 12) |

Rules that hold at this boundary:

- **Coarse.** Send crosses once per utterance and returns one result per clause
  (Kotlin cannot segment clauses without calling native); receive crosses once
  per payload. No per-token or per-symbol entry point exists.
- **Text as UTF-8 bytes** in both directions, never JNI strings (modified UTF-8
  would change supplementary-plane text; Tier 2 is byte-exact).
- **The payload stays opaque.** Tier, priority, language and unresolved slots
  reach Kotlin only through the engine's results, never by parsing bytes.
- **`unresolved[]`** reaches `ReceivedMessage` as slot names only, with empty
  text; such rows are never spoken or replayed (C-31).
- **STT confidence.** The recogniser reports none, so Kotlin passes
  `NativeBridge.CONFIDENCE_UNAVAILABLE`, which is below every threshold: live
  speech is always Tier 2 (C-25) until a confidence scale exists.
- **Priority** comes from the native sender (`is_alert` of a verified Tier 1
  intent, or "Send as Critical"); the Kotlin keyword classifier is no longer on
  the send path.
- `ITANTRA_DISABLE_AEAD`: `-Pitantra.disableAead=true` → CMake; compile error in
  release builds (C-43).

Device conformance programs (C-01/C-02, C-33) are built from the same
`sources.cmake` by a third entry point, `native/test/device/CMakeLists.txt`.

---

# Phase 0 record

Spec basis: `packet-security-transport-spec.md` §1.1–1.4, `receiver-pipeline-spec.md` §1.1,
`tier-1-2-spec.md` §9.2, `IMPLEMENTATION-HANDOFF.md` "JNI BOUNDARY".

---

## 1. The layering, as specified

```
┌────────────────────────────────────────────────────────────┐
│  KOTLIN                                                    │
│                                                            │
│  ITantraPacket    id · senderId · localTimestamp · payload │
│    ├─ contains                                             │
│    │  ┌──────────────────────────────────────────────────┐ │
│    │  │  NATIVE C++  — the native payload                │ │
│    │  │  [ metadata ][ symbols ][ flush ][ pad ][ tag ]  │ │
│    │  │  opaque to Kotlin                                │ │
│    │  └──────────────────────────────────────────────────┘ │
│    └─ carried by                                           │
│  PacketCodec  →  UdpTransport / ThrottledTransport  →  UDP │
└────────────────────────────────────────────────────────────┘
```

## 2. Where the boundary actually is today

**There is no data path across it.** That is the honest state and it is what
Phase 0 was scoped to produce.

| | |
|---|---|
| Library | `libitantra-native.so`, built for `arm64-v8a`, `armeabi-v7a`, `x86`, `x86_64` |
| Loaded by | `com.itantra.app.native.NativeBridge`, in its `init` block |
| Called by | `AppViewModel.init` → `getNativeVersion()`, for a log line. Nothing else. |
| Encode path | **Absent.** `TransmitterViewModel` sends `ByteArray(0)`. |
| Decode path | **Absent.** `ReceiverViewModel` marks every arriving packet undecodable. |

The five existing JNI stubs (`getNativeVersion`, `processText`,
`classifyPriority`, `compressPayload`, `decompressPayload`) are **retained and
untouched** per the handoff's "Do not delete or undo the existing native setup."
None of them implements any specified behaviour:

- `processText` returns its input unchanged.
- `compressPayload` / `decompressPayload` are UTF-8 passthroughs.
- `classifyPriority` returns 0–3 on the retired four-band scale. It is **dead
  code and is now also wrong** — `NORMAL`/`CRITICAL` are the only states
  (`packet §11`). Nothing calls it. Phase 11 replaces the whole surface.

## 3. What the boundary will be

One call per clause, in each direction, exchanging byte arrays. Not per token,
not per symbol — JNI crossings are expensive and surface in M-20/M-21.

```
Kotlin                                   C++
  encodeClause(text, lang, conf, …) ───► assemble → tier select → AEAD
                                    ◄─── native payload bytes

  decodePayload(bytes)              ───► decrypt → parse → gate → decode
                                    ◄─── text · languageId · tier
                                         priority · unresolved[] · status
```

The return shape is `receiver §7`'s output interface. `unresolved[]` crosses
here and carries a hard contract with it (§7.1): a slot listed there must never
be spoken, displayed or defaulted. Kotlin must honour that — C-31.

Ownership stays split: **native** owns the context, the models and the coder;
**Kotlin** owns audio, transport and UI.

## 4. What Kotlin must never do

`ITantraPacket.payload` is opaque. Kotlin parsing it would recreate the second
source of truth `packet §1.3` exists to remove, and both copies would then be
able to disagree.

Everything the decoder needs — `tier`, `symbol_count`, `seq`, `hash_present`,
`priority`, `negation`, `language`, `context_hash` — is inside the payload.
Phase 0 removed the outer frame's copies of `mode`, `priority`, `language` and
`originalBytes`; the wire header went from 11 bytes to 8.

Sender-side facts that are still legitimately Kotlin's (the plaintext, its
UTF-8 size, the chosen priority, the sender's language) live on
`packet.BuiltPacket` and `core.LogEntry` — on the sending device only, never
transmitted.

## 5. Build boundary

`android-native/app/src/main/cpp/sources.cmake` is the single authoritative
source list. Two entry points include it and neither owns it:

| Entry point | Produces | Consumes |
|---|---|---|
| `android-native/app/src/main/cpp/CMakeLists.txt` | `libitantra-native.so` (AGP) | `ITANTRA_JNI_SOURCES` + `ITANTRA_CORE_SOURCES` |
| `native/CMakeLists.txt` | `[H]` host tests | `ITANTRA_CORE_SOURCES` only |

`ITANTRA_CORE_SOURCES` was empty in Phase 0. From Phase 1 it lists files under
`native/src/` (the plan's §0.2 tree), located relative to `sources.cmake` so
both entry points resolve the same paths. The host build excludes the JNI sources: `jni.h` and `android/log.h` do not exist on
the host, and the JNI layer is a thin adapter with no logic of its own to test.

The split exists so a host test exercises the same translation units that reach
the phone. Without it, C-03 (1000 identical encodes) and C-01 (byte-identical
across ≥3 SoC vendors) would be testing different code.

NDK is pinned to **27.0.12077973** in `app/build.gradle.kts`. The Phase 3 golden
vector freeze is a contract against a specific toolchain.

> **Resolved (Phase 1):** the host toolchain is Visual Studio 2026 Community,
> MSVC 19.51, CMake 4.3.1 (the copy bundled with VS, not on PATH), Windows SDK
> 10.0.28000.0. `native/CMakeLists.txt` configures and builds, and runs the
> `[H]` suite through CTest.
