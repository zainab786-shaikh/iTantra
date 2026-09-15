# iTantra — Phased Implementation Plan

**Companion to:** `IMPLEMENTATION-HANDOFF.md`
**Authoritative specs:** the six files listed there
**Version:** 1.0

---

## 0. How To Use This

Fifteen phases. Each one is **independently verifiable**: at every phase boundary the repository builds, all prior tests pass, and work can stop without leaving the tree broken.

```
For each phase:
  1  read the listed spec sections
  2  create/modify only the listed files
  3  write the listed tests alongside the code
  4  run the exit criteria
  5  do not start the next phase until they pass
```

**A phase is not complete because the code exists. It is complete when its exit criteria pass.**

### 0.1 Test environment codes

Many conformance tests cannot run until hardware or a second device exists. Each phase gates only on what is runnable at that point.

| Code | Meaning | Available from |
|---|---|---|
| `[H]` | Host — desktop build, CI | Phase 1 |
| `[D]` | Device — single phone | Phase 11 |
| `[P]` | Pair — two phones | Phase 12 |

A test tagged `[D]` or `[P]` appears as an exit criterion only in Phase 11 or later, even if the code it tests was written in Phase 3.

### 0.2 Proposed native tree

The existing C++/CMake/JNI scaffold takes precedence. If it differs from this, adapt — do not restructure working build config.

```
native/
  CMakeLists.txt
  include/itantra/          public headers
  src/
    common/                 bit I/O, hashing, fixed-width types
    coder/                  arithmetic coder
    packet/                 metadata, assembly, parse
    crypto/                 AEAD, KDF, nonce, replay
    context/                Context Manager
    lang/                   normalisation, extraction, lexicon
    tier1/                  frame encode/decode
    tier2/                  subword, n-gram, byte fallback
    select/                 tier selection
    receiver/               receive pipeline
    api/                    JNI bindings
  test/
    unit/                   per-component, host
    conformance/            C-xx
    bench/                  M-xx
    golden/                 FROZEN vectors — see Phase 3
    fixtures/               synthetic language data — see Phase 6
```

---

## 1. Phase Overview

| # | Phase | Gate | Complexity |
|---|---|---|---|
| 0 | Reconciliation | builds, existing tests pass | low |
| 1 | Deterministic primitives | unit, C-04 | low |
| 2 | Arithmetic coder | unit, fuzz round-trip | **high** |
| 3 | Payload assembly + **golden vectors frozen** | C-03, C-04 | medium |
| 4 | Encryption | C-40…C-43 | medium |
| 5 | Context Manager | unit, C-32 | medium |
| 6 | Language layer + fixtures | C-20…C-24, C-27 | **high** |
| 7 | Tier 2 | C-05, C-06 | **high** |
| 8 | Tier 1 | C-07 | **high** |
| 9 | Tier selection | C-17…C-19 | low |
| 10 | Receiver pipeline | C-31, unit | medium |
| 11 | Kotlin integration | **C-01, C-02**, C-33, C-43 | medium |
| 12 | Two-device conformance | C-08…C-16, C-30, C-34, C-35 | medium |
| 13 | Characterisation | M-01…M-38 recorded | low |
| 14 | Optimisation | no conformance regression | — |

**Phases 2, 6, 7 and 8 carry the real difficulty.** Budget accordingly; the rest is assembly.

---

## PHASE 0 — Reconciliation

**Goal:** align the existing code with the packet spec's layering. No new logic.

**Read:** `packet §1.1–1.4`, `receiver §1.1`, `tier §9.2`

### Files

```
MODIFY   app/.../packet/ITantraPacket.kt
           remove: text, mode, priority, language
           retain: id, senderId
           add:    payload: ByteArray, localTimestamp (not transmitted)

MODIFY   app/.../packet/PacketFactory.kt
           stop generating RAW/PACK7/PHRASE
           accept a native payload byte array

MODIFY   app/.../viewmodel/AppViewModel.kt
           transport chain unchanged; wire the new payload path

AUDIT    native/CMakeLists.txt
           confirm the scaffold builds; do not restructure
```

### Tests

Existing Kotlin tests must still pass. Add none.

### Exit criteria

```
[H]  project builds, debug APK produced
[H]  existing test suite green
     a written note of the Kotlin↔native boundary as it now stands
```

### Notes

`ITantraPacket` is being **changed, not preserved as-is**. The structure and the transport boundary survive; four fields leave the wire because the native payload is authoritative (`packet §1.3`).

Expect the app to be temporarily non-functional end-to-end. That is correct — the payload producer does not exist yet.

---

## PHASE 1 — Deterministic Primitives

**Goal:** the small pieces everything else sits on.

**Read:** `packet §8`, `packet §8.1`, `context §5.2`

### Files

```
NEW   native/src/common/bitio.h / .cpp        BitWriter, BitReader, MSB-first
NEW   native/src/common/types.h               fixed-width aliases, SlotId enum
NEW   native/src/common/hash.h / .cpp         context hash over (current, ver)
NEW   native/test/unit/bitio_test.cpp
NEW   native/test/unit/hash_test.cpp
```

### Tests

```
unit  write/read every width 1…32, round-trip
unit  MSB-first order verified against hand-computed bytes
unit  padding to byte boundary
unit  read past end returns zero, does not crash
unit  hash is stable across process restarts
unit  hash changes when any (current, ver) pair changes
unit  hash ignores recent[], age, seq, context_id
```

### Exit criteria

```
[H]  all unit tests pass
[H]  C-04  static check: zero float/double in these files
```

### Notes

Make C-04 a **build step**, not a manual check. A grep or clang-query in CMake that fails the build.

---

## PHASE 2 — Arithmetic Coder

**Goal:** one coder, both tiers, integer only, fixed flush.

**Read:** `tier §3.1–3.3`, `tier §6.3` (non-zero floor), `tier §13.2` (flush cost)

### Files

```
NEW   native/src/coder/model.h                probability model interface
NEW   native/src/coder/coder.h / .cpp         encode/decode
NEW   native/src/coder/static_model.h/.cpp    frequency table, integer
NEW   native/test/unit/coder_test.cpp
NEW   native/test/unit/coder_fuzz_test.cpp
```

### Tests

```
unit   encode → decode → identical symbols, 10k random sequences
unit   flush emits a FIXED bit count, independent of model or content
unit   every symbol with p > 0 encodes and decodes
unit   a symbol with p == 0 is a programming error → assert, not garbage
fuzz   random symbol sequences, random models, no crash, exact round-trip
unit   two coder instances with identical models produce identical bytes
```

### Exit criteria

```
[H]  all unit and fuzz tests pass
[H]  flush bit count documented as a constant in the header
[H]  C-04 clean
```

### Notes

**The hardest phase.** Get it right before anything depends on it.

The fixed flush count is a wire contract — record it next to the constant, not only in a commit message.

Pick the coder variant deliberately (`tier §13.2`): a naïve 32-bit range coder flushing ~4 bytes is unusable on 2-byte payloads. Measure candidates here, before the format freezes in Phase 3.

---

## PHASE 3 — Payload Assembly + Golden Vectors

**Goal:** the wire format, frozen.

**Read:** `packet §2–5`, `packet §3.6` (seq), `packet §6.10.1` (vector boundary)

### Files

```
NEW   native/src/packet/metadata.h / .cpp     field layout, pack/unpack
NEW   native/src/packet/assemble.h / .cpp     assemble()
NEW   native/src/packet/parse.h / .cpp        parse()
NEW   native/src/packet/seq.h / .cpp          wide counter ↔ 8-bit wire
NEW   native/test/unit/metadata_test.cpp
NEW   native/test/unit/assemble_test.cpp
NEW   native/test/golden/generate.cpp         run ONCE, then freeze
NEW   native/test/golden/vectors.bin          FROZEN ARTIFACT
NEW   native/test/conformance/c01_c04.cpp
```

### Tests

```
unit  every metadata field has an explicit encoder AND decoder
unit  Tier 1 no-hash = 19 bits; with hash = 31; Tier 2 = 21
unit  symbol_count escape path (≥31) round-trips
unit  seq: wide counter → 8 bits → reconstruct, across wrap
unit  assemble → parse → identical AssemblyInput
unit  padding never read; symbol_count bounds the decode
```

### Exit criteria

```
[H]  C-03  same input, 1000 iterations, byte-identical
[H]  C-04  no float
[H]  golden vectors GENERATED and COMMITTED
     C-01/C-02 deferred to Phase 11 (device)
```

### Notes

**This is where the format freezes.** After the vectors are committed, a mismatch is an encoder regression — never a reason to regenerate (`contract §2.2`).

Vectors are captured **pre-encryption** (`packet §6.10.1`). `generate.cpp` runs once and is then effectively dead code; keep it for a deliberate version bump only.

Cover in the vector set: min, max, every `tier × hash_present × priority × negation`, literals in every script including byte fallback, and adversarial input.

---

## PHASE 4 — Encryption

**Goal:** AEAD in the path, phase 1 as decided.

**Read:** `packet §6` entire, especially `§6.5`, `§6.10`

### Files

```
NEW   native/src/crypto/aead.h / .cpp         ChaCha20-Poly1305 wrapper
NEW   native/src/crypto/kdf.h / .cpp          PSK → session key
NEW   native/src/crypto/nonce.h / .cpp        derivation from counter
NEW   native/src/crypto/replay.h / .cpp       sliding window
NEW   native/test/conformance/c40_c43.cpp
MODIFY native/src/packet/assemble.cpp         encrypt after assembly
MODIFY native/src/packet/parse.cpp            decrypt before parse
MODIFY native/CMakeLists.txt                  ITANTRA_DISABLE_AEAD flag
```

### Tests

```
[H] C-40  encrypt → decrypt every golden vector payload, exact recovery
[H] C-41  flip a tag bit; flip a ciphertext bit → rejected, both
unit      nonce derivation is deterministic for a given counter
unit      replay window accepts in-order, rejects seen, tolerates gaps
unit      ITANTRA_DISABLE_AEAD bypasses cleanly in debug
```

### Exit criteria

```
[H]  C-40, C-41 pass
[H]  bypass flag works in debug
     C-42 (replay, pair) and C-43 (release compile-fail) → Phase 11/12
```

### Notes

Use a reviewed ChaCha20-Poly1305 implementation. Do not hand-roll the cipher.

The cipher adds nothing to the determinism surface — it is byte-exact by specification. Only the KDF and nonce derivation need pinning.

**Order is binding:** compress → encrypt. Never the reverse.

---

## PHASE 5 — Context Manager

**Goal:** the shared deterministic memory.

**Read:** `context` entire, especially `§4.2`, `§4.3`, `§5.2`, `§10`

### Files

```
NEW   native/src/context/context.h / .cpp     Slot, Context, commit()
NEW   native/src/context/hash.cpp             uses common/hash
NEW   native/test/unit/context_test.cpp
NEW   native/test/conformance/c32.cpp
```

### Tests

```
unit      recent[] FIFO exactly per §4.2
unit      ver increments on write, not on same-value write
[H] C-32  inherit the same slot 50× → age increments every time
unit      INHERIT and REF do NOT reset age
unit      TIME never inherits
unit      NEGATION is absent from the slot table
unit      initial state: all zero, hash of all-zero table
unit      commit() is ONE function — sender and receiver call the same symbol
```

### Exit criteria

```
[H]  all unit tests pass
[H]  C-32 passes
     C-10…C-16 (pair) → Phase 12
```

### Notes

C-32 is a **regression test for a bug that was in an earlier spec revision**. If INHERIT resets `age`, staleness never fires and a location is inherited indefinitely. Keep the test.

`commit()` must be a single symbol, not two implementations that agree. Enforce it — the receiver links the same object.

---

## PHASE 6 — Language Layer + Synthetic Fixtures

**Goal:** text ⇄ concepts, deterministic, testable without the real corpus.

**Read:** `language §4`, `§6`, `§7`, `§8`

### Files

```
NEW   native/src/lang/normalize.h / .cpp      NFC, digits, whitespace
NEW   native/src/lang/clause.h / .cpp         clause segmentation
NEW   native/src/lang/lexicon.h / .cpp        Aho-Corasick, longest-leftmost
NEW   native/src/lang/extract.h / .cpp        the §8 pipeline
NEW   native/src/lang/pack.h / .cpp           language pack loader (mmap)
NEW   native/test/fixtures/synthetic/         2–3 languages, ~40 concepts
NEW   native/test/unit/normalize_test.cpp
NEW   native/test/unit/lexicon_test.cpp
NEW   native/test/conformance/c20_c27.cpp
```

### Tests

```
[H] C-20  fixture languages, no crash, every utterance handled
[H] C-21  code-mixed: loanword and native word → same concept ID
[H] C-22  digits, native digits, number words → correct value
[H] C-23  inflected forms → correct concept, correct output form
[H] C-24  unknown names → literal, exact round-trip, any script
[H] C-27  two NFC forms of one word → same lexicon entry
unit      normalisation order exactly §6.1 — punctuation survives to step 4
unit      longest-match wins; leftmost on ties
unit      byte-level match rejected if it splits a codepoint
unit      ambiguity leaves the slot UNCHANGED, never guesses
```

### Exit criteria

```
[H]  C-20…C-24, C-27 pass on fixtures
[H]  zero language-specific code paths — differences live in data only
```

### Notes

**Do not block on the real corpus.** The fixture is a test artifact covering every code path: native words, loanwords, inflected forms, literals, unknowns, code-mixing. Real packs replace it with **no code change** — if a code change is needed, the interface is wrong.

Normalisation order is a trap: stripping punctuation before clause segmentation destroys the boundaries the segmenter needs.

---

## PHASE 7 — Tier 2

**Goal:** the lossless safety floor.

**Read:** `tier §6` entire, especially `§6.2`, `§6.3`, `§6.5`, `§6.7`

### Files

```
NEW   native/src/tier2/subword.h / .cpp       tokenizer + byte fallback
NEW   native/src/tier2/ngram.h / .cpp         static table, KN or PPM
NEW   native/src/tier2/boost.h / .cpp         context boost
NEW   native/src/tier2/encode.h / .cpp
NEW   native/src/tier2/decode.h / .cpp
NEW   native/test/conformance/c05_c06.cpp
```

### Tests

```
[H] C-05  every fixture utterance: encode → decode → EXACT string equality
[H] C-06  random bytes, empty input, mixed scripts, untrained characters
          → decodable payload EVERY time, exact round-trip
unit      all 256 byte values present in the vocabulary
unit      every token has p > 0 in every context — backoff reaches a floor
unit      table resets at every clause boundary
unit      boost applied only when hash_present == 1
unit      unboosted encoding decodes with ANY context state
```

### Exit criteria

```
[H]  C-05 and C-06 pass
[H]  C-06 has no threshold — it either always works or the floor has a hole
```

### Notes

**Tier 2 before Tier 1**, because Tier 1's literals use this subword coder (`tier §5.7`). Building Tier 1 first means stubbing the literal path and revisiting it.

Byte fallback and the non-zero floor are **both** required. Byte fallback alone: the token exists but codes to infinity. Smoothing alone: no token at all.

The boost requires the hash (`tier §6.5`). Unboosted is the recovery path and must decode regardless of context.

---

## PHASE 8 — Tier 1

**Goal:** the lossy, safety-gated frame encoding.

**Read:** `tier §5` entire

### Files

```
NEW   native/src/tier1/adjacency.h / .cpp     3-state FSM
NEW   native/src/tier1/head.h / .cpp          class precedence
NEW   native/src/tier1/rules.h / .cpp         rule_priority table
NEW   native/src/tier1/slots.h / .cpp         INHERIT/REF/ID/LITERAL
NEW   native/src/tier1/readback.h / .cpp      safety gate
NEW   native/src/tier1/encode.h / .cpp
NEW   native/src/tier1/decode.h / .cpp
NEW   native/src/tier1/rulec/                 build-time rule compiler
NEW   native/test/conformance/c07.cpp
```

### Tests

```
[H] C-07  read-back similarity ≥ threshold for the message's priority
unit      head precedence ACTION > EVENT > STATE > ENTITY > MODIFIER
unit      ties break by slot enum order
unit      no head → INTENT_NONE; two top-class → INTENT_NONE
unit      rule_priority sorted, first match wins
unit      NEG_SET selects a DIFFERENT intent, not a flag on the same one
unit      head_implied true → head not transmitted
unit      TIME never inherits
unit      literals encode via the Tier 2 coder, any script
unit      literals never written to context
unit      adjacency FSM times out
unit      STATIC model — no context boost applied
build     rule compiler: duplicate rule_priority in a bucket = build error
build     every intent reachable; every ACTION/EVENT/STATE has a rule
```

### Exit criteria

```
[H]  C-07 passes
[H]  build-time rule validation fails the build on a malformed table
[H]  coverage report produced: utterances reaching INTENT_NONE
```

### Notes

**Tier 1 uses a static model, no boost** (`tier §5.9`). This is what lets a hash mismatch degrade gracefully instead of failing entirely.

`rule_priority` is evaluation order. It has nothing to do with `NORMAL`/`CRITICAL`.

The build-time validations are not optional. A duplicate `rule_priority` makes rule selection order-dependent, which is a determinism bug that will surface as an intermittent test failure months later.

---

## PHASE 9 — Tier Selection

**Goal:** safe AND smaller, at complete-packet level.

**Read:** `tier §8`, `tier §13.1`

### Files

```
NEW   native/src/select/select.h / .cpp
NEW   native/test/conformance/c17_c19.cpp
```

### Tests

```
[H] C-17  Tier 1 safe → the SMALLER of the two encodings is selected
[H] C-18  any safety gate trips → Tier 2, regardless of size
[H] C-19  comparison uses the COMPLETE native packet —
          metadata + flush + padding + AEAD tag, not payload alone
unit      all seven safety triggers route to Tier 2
unit      low confidence → Tier 1 never selected
```

### Exit criteria

```
[H]  C-17, C-18, C-19 pass
[H]  a test that FAILS if the comparison uses payload size only
```

### Notes

C-19 guards a structural decision. Fixed overhead is identical for both tiers, so a payload-level comparison overstates Tier 1's advantage by roughly 2.4× and would select Tier 1 where Tier 2 produces a smaller packet.

Write the negative test explicitly — one that passes on payload comparison and fails on packet comparison, asserted the right way round.

---

## PHASE 10 — Receiver Pipeline

**Goal:** gates, failure branches, output contract.

**Read:** `receiver` entire

### Files

```
NEW   native/src/receiver/pipeline.h / .cpp   the §2 flow
NEW   native/src/receiver/output.h            the §7 interface
NEW   native/test/conformance/c31.cpp
```

### Tests

```
[H] C-31  any slot in unresolved[] is NEVER rendered as a value
unit      gate order: authenticate → parse → gates → decode
unit      hash verified BEFORE decoding any inherited slot
unit      no-inherit packet decodes regardless of hash state
unit      Tier 1 + hash mismatch → decodes; INHERIT/REF → unresolved[]
unit      Tier 2 boosted + mismatch → refuses to decode
unit      commit ONLY on success; never on mismatch or auth failure
unit      FEC stage is a no-op — no algorithm present
unit      language id in output: Tier 1 receiver's, Tier 2 sender's
```

### Exit criteria

```
[H]  C-31 passes
[H]  every §9 failure branch has a test
     C-14, C-15, C-30, C-34, C-35 (pair) → Phase 12
```

### Notes

`unresolved[]` is the safety property crossing out of the native layer. Phase 11 must honour it on the Kotlin side.

---

## PHASE 11 — Kotlin Integration

**Goal:** end to end on one device.

**Read:** `packet §1.1–1.4`, `receiver §1.1`, handoff JNI section

### Files

```
NEW    native/src/api/jni.cpp                 coarse: one call per clause
NEW    app/.../native/ItantraNative.kt        JNI wrapper
MODIFY app/.../packet/PacketFactory.kt        call native encode
MODIFY app/.../receiver/*.kt                  call native decode
MODIFY app/.../viewmodel/*.kt                 wire the pipeline
MODIFY app/.../ui/components/TelemetryStrip.kt
MODIFY app/.../ui/components/PacketLog.kt
NEW    native/test/conformance/c01_device.cpp
```

### Tests

```
[D] C-01  golden vectors byte-identical on ≥3 SoC vendors, AEAD bypassed
[D] C-02  decode identical on ≥3 devices
[D] C-33  100,000 messages, zero nonce repeats
[D] C-43  release build with ITANTRA_DISABLE_AEAD → COMPILE ERROR
[D] C-25  low-confidence STT never selects Tier 1
[D] C-26  NORMAL and CRITICAL survive round-trip
unit      JNI boundary is one call per clause, not per token
```

### Exit criteria

```
[D]  C-01, C-02 pass on three devices — THE critical gate
[D]  C-33, C-43, C-25, C-26 pass
     end-to-end speech → speech works on a single device (loopback)
```

### Notes

**C-01 is the single most important test in the project.** An implementation passing everything else and failing C-01 works in the lab and fails in the demonstration room.

If C-01 fails, use `ITANTRA_DISABLE_AEAD` to isolate whether the divergence is in the payload or the crypto. That is what the flag exists for.

Keep the JNI boundary coarse. One call per clause.

---

## PHASE 12 — Two-Device Conformance

**Goal:** context synchronisation under a real link.

**Read:** `context §16–18`, `receiver §6`, `§9`

### Files

```
NEW   native/test/conformance/c10_c16.cpp
NEW   tools/lossy_proxy/                   controllable loss + reorder
MODIFY app/.../transport/UdpTransport.kt   test hooks only
```

### Tests

```
[P] C-08  cross-language Tier 1 → output in the RECEIVER's language
[P] C-09  cross-language Tier 2 → output in the SENDER's language
[P] C-10  200 messages, no loss → contexts identical, zero syncs
[P] C-11  5% loss → detected, repaired, NEVER silently wrong
[P] C-12  reordering → handled or synced; never a wrong decode
[P] C-13  duplicates discarded silently, no repair request
[P] C-14  forced mismatch, Tier 1 → partial decode, no commit
[P] C-15  forced mismatch, boosted Tier 2 → refused, unboosted resend works
[P] C-16  recovery after repair → contexts identical
[P] C-30  corrupt one negation copy → packet REJECTED
[P] C-34  version mismatch → pairing fails VISIBLY
[P] C-35  cross-language Tier 2 → BOTH phones skip the context update
[P] C-42  replay rejected
```

### Exit criteria

```
[P]  all of C-08…C-16, C-30, C-34, C-35, C-42 pass
[P]  C-11's criterion: not one message delivered with a wrong
     inherited value, across the whole run
```

### Notes

The lossy proxy is a real deliverable. Without controllable loss and reordering, C-11 through C-13 cannot be run deliberately — only waited for.

C-35 is a regression test: if only one side skips the cross-language context update, the two diverge on a perfectly delivered message.

---

## PHASE 13 — Characterisation

**Goal:** the numbers that settle open decisions.

**Read:** `contract §6`

### Files

```
NEW   native/test/bench/compression.cpp    M-01…M-08
NEW   native/test/bench/perf.cpp           M-20…M-27
NEW   native/test/bench/decisions.cpp      M-30…M-38
NEW   tools/report/                        machine-readable output
```

### Tests

Recorded, not graded. All seven compression figures for every corpus utterance, aggregated per language **and** per tier.

### Exit criteria

```
[H][D][P]  every M-xx has a recorded value with its unit
           report file produced in the §7 format
           M-31 computed at COMPLETE PACKET level
```

### Notes

**M-31 is the highest-leverage measurement in the project.** If Tier 1 beats Tier 2 by only a byte or two at packet level, a single lossless path is the simpler system and the frame machinery is not earning its complexity.

M-30 settles romanised IR. M-32 settles the coder choice — though by this point the coder is frozen, so a bad result here is expensive. Prefer measuring flush candidates in Phase 2.

---

## PHASE 14 — Optimisation

**Goal:** only now.

### Rule

```
No optimisation may break a conformance test.
Re-run the full C-xx suite after every change.
Re-run M-xx to confirm the optimisation actually helped.
```

Candidates, in order of likely payoff: coder flush cost, lexicon memory layout, JNI call overhead, clause bundling (if M-33 justifies it).

---

## 2. Gate Discipline

```
Phase 3   format freezes        — vectors committed, never regenerated
Phase 11  determinism proven    — C-01 across devices
Phase 12  synchronisation proven — C-11 never silently wrong
```

Those three are hard gates. Work may continue past a soft phase with a known failure; it may not continue past these.

## 3. If Time Runs Short

Minimum path retaining the most assurance (`contract §8`):

```
Phases 0–5     primitives, coder, format, crypto, context
Phase 7        Tier 2 only — the lossless floor
Phase 9        trivial: Tier 2 always
Phases 10, 11  receiver + integration
C-01, C-05, C-06, C-25, C-31
```

That ships a working, deterministic, lossless, encrypted system with no Tier 1. It is honest, it demonstrates the architecture, and Tier 1 is additive afterwards.

Dropping Tier 1 also drops cross-language operation — say so rather than omitting it.

## 4. Per-Phase Reporting

At each phase boundary, report:

```
phase number and name
files created / modified
tests added, and which C-xx or M-xx they implement
exit criteria: pass / fail per item
spec sections satisfied
anything that required a decision the specs did not cover
```

That last line is the important one. A decision the specs did not cover is a spec gap — report it rather than absorbing it into the code.
