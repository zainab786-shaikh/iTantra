# The Kotlin ↔ Native Boundary, as it stands after Phase 0

Required by the implementation plan's Phase 0 exit criteria. This records the
boundary **as it actually is today**, not as it will be. Phase 11 replaces most
of it.

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

`ITANTRA_CORE_SOURCES` is empty in Phase 0 and is populated from Phase 1. The
host build excludes the JNI sources: `jni.h` and `android/log.h` do not exist on
the host, and the JNI layer is a thin adapter with no logic of its own to test.

The split exists so a host test exercises the same translation units that reach
the phone. Without it, C-03 (1000 identical encodes) and C-01 (byte-identical
across ≥3 SoC vendors) would be testing different code.

NDK is pinned to **27.0.12077973** in `app/build.gradle.kts`. The Phase 3 golden
vector freeze is a contract against a specific toolchain.

> **Open:** there is no host C++ compiler installed on the current development
> machine (no MSVC, clang, gcc, or ninja on PATH). `native/CMakeLists.txt` is
> written and its source-list mechanism is verified, but it cannot be configured
> until one exists. This blocks every `[H]` test from Phase 1 onward.
