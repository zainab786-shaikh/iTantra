# Implementation Validation & Benchmark Contract

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Test and measurement contract — **normative**
**Version:** 1.1
**Status:** Final — binding on the implementation
**Companions:** `language-layer-spec.md` v1.3 · `context-manager-spec.md` v1.2 · `tier-1-2-spec.md` v1.5 · `packet-security-transport-spec.md` v1.2 · `receiver-pipeline-spec.md` v1.2

### Changes from v1.0

- §2.2 — **golden vectors are captured pre-encryption.** Ciphertext depends on the session key, so key-derived vectors could never be stable across runs.
- §5.1 — C-01/C-02 run on the plaintext payload, with the AEAD bypass flag.
- §5.7 — new: crypto conformance tests C-40…C-43, matching the phase-1 encryption decision.

---

## 1. Purpose and Structure

This document is **normative**. An implementation that does not pass every conformance test in §5 is not conformant, regardless of how well it performs.

### 1.1 Two kinds of test — do not mix them

| Kind | Outcome | Effect of failure |
|---|---|---|
| **Conformance** (§5) | Pass / fail | **Blocks release.** No threshold to tune. |
| **Characterisation** (§6) | A recorded number | Informs a decision. Never "fails". |

A compression ratio cannot fail — it is whatever it is. Bit-exact determinism cannot be "mostly" achieved. Reporting them in one undifferentiated table is how a broken conformance test gets lost among measurements.

### 1.2 Test identifiers

Every test has a stable ID (`C-nn` conformance, `M-nn` characterisation). Results are reported against these IDs. Do not renumber.

---

## 2. Prerequisites

None of §5–§6 can run without these.

| Prerequisite | Owner | Blocks |
|---|---|---|
| **Test corpus** — ≥200 realistic utterances per language, labelled | Team | C-20…C-27, M-01…M-12 |
| **Golden vector file** — fixed symbol sequences + expected bytes | Build | C-01…C-04 |
| **≥3 physical devices**, different SoC vendors | Team | C-01, C-02, M-20…M-24 |
| **Two-device rig** with controllable loss and reordering | Team | C-10…C-16 |

### 2.1 Corpus composition

The corpus is a shipped artifact, version-controlled alongside the language packs.

```
70%   should succeed   — in-domain operational messages
30%   should fail      — out-of-domain, ambiguous, compound,
                         unknown names, deliberately degraded audio
```

A corpus of only-good cases proves the system accepts things. It does not prove the safety gates reject anything, and the gates are half the design.

Each entry carries: language, expected tier, expected intent (or `INTENT_NONE`), priority, and whether it is code-mixed.

### 2.2 Golden vectors

> **Captured pre-encryption.** The vector boundary is the assembled **plaintext** payload — metadata + payload + flush + padding, before AEAD (`packet-security-transport-spec.md` §6.10.1).

Ciphertext depends on the session key. Key-derived vectors would differ on every run and C-01 could never be stable. Encryption is validated separately by C-40…C-43.

**Ordering:** golden vectors cannot exist before an encoder does. The sequence is:

```
1  build the encoder
2  generate vectors from it
3  FREEZE them — they are now the contract
4  thereafter, a vector mismatch is an encoder regression,
   never a reason to regenerate
```

A version-controlled file of `(input symbols, model version, expected plaintext payload bytes)` triples, covering:

```
minimum        1 symbol, no hash, both tiers
maximum        symbol_count at the escape boundary, hash present
all variants   tier × hash_present × priority × negation
literals       every script, including byte-fallback cases
adversarial    random bytes, empty input, mixed scripts
```

Golden vectors are regenerated **only** on a deliberate format version bump, never to make a failing test pass.

---

## 3. Test Environments

Some tests can only run in one environment. Stating this prevents "we ran it on the host" standing in for a device result.

| Env | What runs there | What cannot |
|---|---|---|
| **H — Host** (desktop CI) | Unit tests, corpus sweeps, compression measurement, fast iteration | Determinism across SoCs, real latency, memory, power |
| **D — Device** (single phone) | Determinism, CPU time, peak memory, power | Context sync, end-to-end latency |
| **P — Pair** (two phones) | Context sync, loss/reorder, end-to-end latency, cross-language | — |

Every test below is tagged `[H]`, `[D]` or `[P]`.

---

## 4. Gates

Phases are ordered. **A later phase's results are meaningless if an earlier phase fails.**

```
GATE 1   Determinism        C-01 … C-04
             ↓              Nothing else is meaningful if two devices
                            disagree on bytes.

GATE 2   Correctness        C-05 … C-09, C-40 … C-43
             ↓              Round-trip, totality, crypto.

GATE 3   Safety             C-30 … C-35
             ↓              The gates that exist to prevent confident
                            wrong output.

GATE 4   Synchronisation    C-10 … C-16
             ↓              Context behaviour under a real link.

GATE 5   Coverage           C-20 … C-27
             ↓
GATE 6   Characterisation   M-01 … M-27, M-30 … M-38
                            Numbers that inform open decisions.
```

---

## 5. Conformance Tests — Pass / Fail

### 5.1 Bit-exact determinism

| ID | Env | Test | Pass criterion |
|---|---|---|---|
| **C-01** | D | Encode every golden vector on ≥3 devices with different SoC vendors, **AEAD bypassed** (§2.2) | **Byte-identical** output on all devices. Not "equivalent" — identical. |
| **C-02** | D | Decode every golden vector on ≥3 devices, **AEAD bypassed** | Identical decoded symbols on all devices |
| **C-03** | H | Encode the same input 1000 times in one process | Byte-identical every time |
| **C-04** | H | Static analysis: no `float` or `double` in encode, decode, model or threshold paths | Zero occurrences |

**C-01 is the single most important test in this document.** An implementation that passes everything else and fails C-01 works in the lab and fails in the demonstration room.

C-04 is automatable as a build step. Make it one.

### 5.2 Round-trip correctness

| ID | Env | Test | Pass criterion |
|---|---|---|---|
| **C-05** | H | Tier 2: encode → decode every corpus utterance | **Exact string equality.** Byte-for-byte, including whitespace and script. |
| **C-06** | H | Tier 2 adversarial: random bytes, empty input, mixed scripts, characters absent from all training data | Decodable payload produced **every time**; exact round-trip. Proves byte fallback + non-zero floor (`tier-1-2-spec.md` §6.7) |
| **C-07** | H | Tier 1: encode → decode → render → compare against the original | Read-back similarity ≥ the configured threshold for that priority |
| **C-08** | P | Cross-language Tier 1: sender language A, receiver language B | Output is in **language B**, and is the expected rendering of that intent |
| **C-09** | P | Cross-language Tier 2 | Output is in **language A** (sender's), and `language id` in the output interface says so |

C-06 has no threshold. It either always produces a decodable payload or the safety floor has a hole in it.

### 5.3 Context synchronisation

Run on the pair rig with controllable loss and reordering.

| ID | Env | Scenario | Pass criterion |
|---|---|---|---|
| **C-10** | P | 200 messages, no loss | Contexts identical after every message; zero sync requests |
| **C-11** | P | 5% random packet loss | Divergence detected by the hash; repair restores identity; **no silent wrong output** |
| **C-12** | P | Reordering, no loss | Either handled, or a sync raised and recovered. Never a wrong decode. |
| **C-13** | P | Duplicate / replay injection | Duplicates discarded silently; **no repair request**, no context change |
| **C-14** | P | Forced hash mismatch, Tier 1 packet | Payload still decodes; only INHERIT/REF slots in `unresolved[]`; **context not committed** |
| **C-15** | P | Forced hash mismatch, boosted Tier 2 packet | Decode refused; sync requested; unboosted resend succeeds |
| **C-16** | P | Recovery after repair | Contexts identical; subsequent inheritance works normally |

**C-11's pass criterion is the important one.** Detecting divergence is not enough — the test fails if any message is delivered with a wrong inherited value, even once.

### 5.4 Tier selection

| ID | Env | Test | Pass criterion |
|---|---|---|---|
| **C-17** | H | For every corpus utterance where Tier 1 is safe, compare both encodings | Selected tier is the **smaller of the two** |
| **C-18** | H | For every utterance where a safety gate trips | Tier 2 selected, **regardless of size** |
| **C-19** | H | Size comparison uses the complete native packet | Comparison includes metadata, flush, padding and authentication tag — **not** the compressed payload alone |

> **C-19 guards a structural decision.** Fixed overhead is identical for both tiers, so comparing payloads overstates Tier 1's advantage by roughly 2.4× (`tier-1-2-spec.md` §13.1). A payload-level comparison would select Tier 1 in cases where Tier 2 produces a smaller packet, and would mislead the "does Tier 1 justify frames?" decision.

### 5.5 Language coverage

| ID | Env | Test | Pass criterion |
|---|---|---|---|
| **C-20** | H | All 10 languages, full corpus | No crashes; every utterance produces a decodable packet |
| **C-21** | H | Code-mixed utterances (English loanwords in each language) | Loanwords resolve to the same concept IDs as their native equivalents |
| **C-22** | H | Numbers: digits, native digits, number words | Correct numeric value extracted; exact round-trip |
| **C-23** | H | Inflected forms | Matched to the correct concept; correct output form rendered per slot position |
| **C-24** | H | Unknown names → literals | Literal round-trips exactly, in any script |
| **C-25** | H | Low-confidence STT input | **Tier 1 not used.** Tier 2 selected without exception. |
| **C-26** | H | `NORMAL` and `CRITICAL` messages | Priority survives round-trip; `CRITICAL` uses the higher read-back bar |
| **C-27** | H | Unicode NFC: same word in two normalisation forms | Both match the same lexicon entry |

### 5.6 Additional guards — silent-failure prevention

**These six were not in the original test list. Each guards a decision that fails quietly rather than loudly.**

| ID | Env | Test | Pass criterion | Guards |
|---|---|---|---|---|
| **C-30** | P | Corrupt one copy of the duplicated negation bit | Packet **rejected**, repeat requested. Never decoded with a guessed negation. | The highest-consequence field in the system |
| **C-31** | P | Any slot appearing in `unresolved[]` | Never rendered as a value — not spoken, not displayed, not defaulted | The safety property crossing out of the decode layer |
| **C-32** | H | Inherit the same slot 50 consecutive times | `age` increments every time; staleness eventually fires | `context-manager-spec.md` §4.3 — if INHERIT reset `age`, staleness never fires and a location is inherited indefinitely |
| **C-33** | D | Run a session of 100,000 messages; log every derived nonce | **Zero repeats** | Nonce reuse breaks AEAD catastrophically |
| **C-34** | P | Pair two devices with mismatched codebook / schema / packet-format versions | Pairing **fails visibly**. Never proceeds to message exchange. | A version mismatch that fails silently decodes every message wrongly |
| **C-35** | P | Cross-language Tier 2 message | **Both** phones skip the context update. Verify by hash comparison after. | If only one side skips, the contexts diverge on a perfectly delivered message |

C-32 and C-35 are regression tests for decisions that were wrong in earlier spec revisions. Keep them.

### 5.7 Cryptography

Encryption is implemented in phase 1 (`packet-security-transport-spec.md` §6.10), so these are phase-1 conformance tests, not future work.

| ID | Env | Test | Pass criterion |
|---|---|---|---|
| **C-40** | H | Encrypt → decrypt every golden vector payload | Exact plaintext recovery |
| **C-41** | H | Flip one bit of the tag; flip one bit of the ciphertext | **Rejected** in both cases. Never partially decoded. |
| **C-42** | P | Replay a previously accepted packet | Rejected by the replay window (§5.3 C-13); no context change |
| **C-43** | D | Release build with `ITANTRA_DISABLE_AEAD` set | **Compile error.** The bypass must not be reachable in a release build. |

C-33 (nonce uniqueness over 100,000 messages) remains in §5.6 as a silent-failure guard.

---

## 6. Characterisation Measurements — Record, Do Not Grade

These produce numbers. None of them can "fail". Several settle open architectural decisions.

### 6.1 Compression reporting

For every corpus utterance, report **all seven** figures. Partial reporting hides where the bytes actually go.

| ID | Measurement |
|---|---|
| **M-01** | Original UTF-8 bytes |
| **M-02** | Native payload bytes (pre-encryption) |
| **M-03** | Encrypted bytes (payload + AEAD tag) |
| **M-04** | Complete `ITantraPacket` bytes (including outer frame) |
| **M-05** | Compression ratio — **M-01 ÷ M-04**, not M-01 ÷ M-02 |
| **M-06** | Savings percentage |
| **M-07** | Tier 1 hit rate |
| **M-08** | Tier 2 fallback rate |

> **M-05 uses the complete packet.** Quoting M-01 ÷ M-02 overstates the achieved compression by excluding the tag and outer frame, which together can exceed the payload on short messages.

Report aggregated **per language** and **per tier**, not only overall — a strong average can conceal one language performing badly.

**M-07 is also the cross-language coverage figure** (`language-layer-spec.md` §10.4). A message falling to Tier 2 does not merely cost bytes; it arrives in a language the listener may not read.

### 6.2 Performance

| ID | Env | Measurement | Definition |
|---|---|---|---|
| **M-20** | D | STT → encoded payload latency | From the endpointer firing to the native payload being ready |
| **M-21** | D | Decode → output latency | From the native payload arriving to the output interface being populated |
| **M-22** | P | End-to-end | From the endpointer firing on A to the output interface populated on B |
| **M-23** | D | C++ encode CPU time | Per clause, excluding STT |
| **M-24** | D | C++ decode CPU time | Per clause, excluding TTS |
| **M-25** | D | Peak native memory | RSS attributable to the native module, steady state and peak |
| **M-26** | D | Idle-listening CPU | With VAD active, no speech — feeds the 20% efficiency score |
| **M-27** | D | Power draw | Where measurable. Report method; an unmeasured estimate is not a result. |

**Define the measurement points in code, not in prose.** "Latency" measured from different events on different runs is not comparable. Instrument once and reuse.

### 6.3 Open decisions these settle

| ID | Measurement | Decision it settles |
|---|---|---|
| **M-30** | Native-script Tier 2 vs romanised Tier 2, same corpus | Whether to adopt romanised IR (`tier-1-2-spec.md` §13.1) |
| **M-31** | Tier 1 vs Tier 2 margin at **complete packet** level | Whether frames justify their complexity |
| **M-32** | Coder flush cost, per coder candidate | Range coder vs rANS vs carefully-terminated variant |
| **M-33** | Header + outer frame overhead as a fraction of the packet | Whether clause bundling is worth building |
| **M-34** | Per-language STT WER | Where to set the per-language confidence thresholds |
| **M-35** | Utterances reaching `INTENT_NONE`, with the STT text that produced them | Which `stt_variant` lexicon entries to add |
| **M-36** | Effort per language, split into input-side and output-side hours | Confirms "adding a language is data, not code" |
| **M-37** | Literal repetition cost across a session | Whether literals should ever be inheritable |
| **M-38** | Context-richness effect: inheritance quality after Tier 1 vs Tier 2 messages | Whether Tier 2's thicker context offsets its size |

**M-31 is the highest-leverage measurement in this document.** If Tier 1 beats Tier 2 by only a byte or two at packet level, a single lossless path is the simpler system and the frame machinery is not earning its complexity.

---

## 7. Reporting Format

One machine-readable result file per run, so results are diffable across builds.

```
run_id · timestamp · build hash
spec versions of all five documents
corpus version · golden vector version
device list (model, SoC, Android version)

conformance:  [ { id, env, status: PASS|FAIL, detail } ]
characterisation: [ { id, env, value, unit, breakdown } ]
```

### 7.1 Rules

- **A conformance failure is reported as a failure**, never as a low score.
- Characterisation values carry their **unit** and their **breakdown by language and tier**. An aggregate alone is not a result.
- A skipped test is reported as `SKIPPED` with a reason. Absence is not a pass.
- Measurements taken on the host are labelled `[H]` and **do not** substitute for device results.

### 7.2 Regenerating golden vectors

Permitted **only** on a deliberate format version bump, recorded in the relevant spec's change log. Regenerating them to clear a failing C-01 destroys the only evidence that two devices agree.

---

## 8. Minimum Viable Validation

If time is short, this is the ordering that retains the most assurance per hour:

```
1   C-01   determinism across devices
2   C-05   Tier 2 exact round-trip
3   C-06   Tier 2 adversarial totality
4   C-25   low confidence never selects Tier 1
5   C-30   negation disagreement rejects
6   C-31   unresolved never rendered as a value
7   C-11   loss → detected, repaired, never silently wrong
8   M-31   Tier 1 vs Tier 2 margin at packet level
```

The first seven are the tests whose failure produces **confident wrong output** — the failure mode this architecture exists to prevent. The eighth decides whether half the architecture is worth keeping.

---

## Appendix A — Traceability

Each conformance test traces to the spec clause it enforces.

| Test | Enforces |
|---|---|
| C-01…C-04 | tier §12 T1–T10 · context §20 D1–D11 · packet §8 |
| C-05, C-06 | tier §6.2, §6.3, §6.7 |
| C-07 | tier §5.8 |
| C-08, C-09 | language §10.1 |
| C-10…C-16 | context §16, §17, §18 · receiver §6, §9 |
| C-17…C-19 | tier §8.1, §8.2, §13.1 |
| C-20…C-27 | language §5, §6, §8, §11 · tier §5.8 |
| C-30 | packet §3.2 · receiver §3④ |
| C-31 | receiver §7.1 |
| C-32 | context §4.3 |
| C-33 | packet §6.5 |
| C-34 | context §18.1 · packet §8.3 |
| C-35 | tier §11.3 · receiver §8.3 |
| C-40…C-43 | packet §6, §6.10 |

## Appendix B — Register

| # | Decision | Status |
|---|---|---|
| 1 | Conformance and characterisation are separately reported | Locked |
| 2 | C-01 byte-exact across ≥3 SoC vendors is mandatory | Locked |
| 3 | Compression ratio quoted at complete-packet level (M-05) | Locked |
| 4 | Tier selection compares complete packets (C-19) | Locked |
| 5 | Corpus is 70% should-succeed, 30% should-fail | Locked |
| 6 | Golden vectors regenerate only on a format version bump | Locked |
| 7 | Host results do not substitute for device results | Locked |
| 8 | Six silent-failure guards C-30…C-35 are mandatory | Locked |
| 8a | Golden vectors captured pre-encryption; frozen after first generation | Locked |
| 8b | Crypto conformance C-40…C-43 is phase 1, not future work | Locked |
| 9 | Corpus size per language | **To be set by the team** |
| 10 | Absolute latency and memory targets | **Open — measure first, then set** |
