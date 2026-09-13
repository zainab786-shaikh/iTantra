# Receiver Pipeline — Component Specification

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Receiver node — end-to-end decode path
**Version:** 1.2
**Status:** Final — ready for implementation
**Companions:** `language-layer-spec.md` v1.3 · `context-manager-spec.md` v1.2 · `tier-1-2-spec.md` v1.5 · `packet-security-transport-spec.md` v1.2 · `validation-benchmark-contract.md` v1.1

This document ties the four layers together on the receive side. Each layer's own spec owns its internals; this one owns the **order**, the **gates**, and the **failure branches**.

### Changes from v1.1

- §3① — **FEC clarified.** Stage ① is a **no-op in the current phase**, because `UdpTransport` provides no FEC. The stage is retained in the flow for the final architecture.

### Changes from v1.0

- §1.1 — new: the input is the **native payload** extracted by the existing Kotlin layer, not a raw wire buffer.
- §3⑤ — `seq` is 8 bits, fixed; not read from `caps()`.
- §7 — priority terminology standardised to `NORMAL` / `CRITICAL`.

---

## 1. Boundary

```
IN    the native payload, handed up by the existing Kotlin layer
OUT   the output interface of §7

NOT   outer framing · UDP reception · FEC ·
      synthesis · playback · UI
```

### 1.1 Where this sits

The Kotlin layer receives the UDP datagram, unwraps the existing `ITantraPacket` outer frame, and hands the **native payload** down to C++. Everything in this document operates on that payload.

```
UDP datagram                          Kotlin, existing
   ↓
ITantraPacket outer frame unwrapped   Kotlin, existing
   ↓
NATIVE PAYLOAD                        ← this document starts here
   ↓
[ decrypt → parse → gate → decode → reconstruct → commit ]
   ↓
Output interface §7                   → Kotlin output layer
```

The outer frame carries no fields this pipeline needs. Tier, priority, language, sequence, negation and the context hash are all **inside the native payload** (`packet-security-transport-spec.md` §1.3).

---

## 2. Full Flow

```
native payload in                (outer frame already unwrapped — §1.1)
   ↓
① FEC decode                     no-op in the current phase — §3①
   ↓
② decrypt + authenticate ─── fail ──→ discard · record gap · request repeat
   ↓ pass
③ parse metadata                 fixed width, no coder state
   ↓
④ negation copies agree? ── no ──→ reject · request repeat
   ↓ yes
⑤ seq gap? ──────────────── yes ─→ flag context suspect · request sync
   ↓                                (continue processing this packet)
⑥ replay check ──── seen ──→ discard silently
   ↓
⑦ hash_present?
   ├── no ─────────────────────→ self-contained · proceed
   └── yes → compare context hash
              ├── match ────────→ proceed
              └── mismatch ─────→ §6 — differs by tier
   ↓
⑧ select model from tier
   ↓
⑨ decode symbol_count symbols
   ↓
⑩ reconstruct by tier
   ├── Tier 1 → frame → resolve slots → template in RECEIVER's language
   └── Tier 2 → subwords → text in SENDER's language
   ↓
⑪ commit context FROM PAYLOAD    same function the sender used
   ↓
⑫ emit to output layer
```

### 2.1 Two things this ordering gets right

**The context hash is a gate before decoding, not a step after.** It is checked at ⑦, ahead of the decoder at ⑨. Checking afterwards would mean decoding against context you have no reason to trust.

**Authentication precedes everything.** Never run a parser or a decoder over bytes already known to be corrupt.

---

## 3. Stage Detail

### ① FEC decode

Transport layer. Strictly outside encryption (`packet-security-transport-spec.md` §7.4) — if it were inside, bit errors would fail authentication before FEC could correct them.

> **No-op in the current phase.** `UdpTransport` / `ThrottledTransport` performs no error correction, so there is nothing to decode. The stage is retained in the flow because the final architecture requires it for the constrained radio.
>
> Do not implement an FEC scheme here. Loss is handled by `seq`, the context hash and repair (§3⑤, §6).

### ② Decrypt and authenticate

ChaCha20-Poly1305, nonce reconstructed from `session_id`, direction and the local wide counter.

Failure means corruption or tampering; they are indistinguishable and both mean discard. Because metadata is inside the ciphertext, a failed packet yields nothing readable — the gap is detected one message later, via the next good packet's `seq`.

### ③ Parse metadata

Fixed width, MSB-first, zero coder state.

```
tier · symbol_count · seq · hash_present · priority
negation (Tier 1) | language (Tier 2)
context_hash (conditional)
```

### ④ Negation

Two copies. Disagreement means one flipped, and you cannot know which. **Reject the whole packet** rather than guess.

This is the one field whose flip inverts a life-safety instruction, which is why it is duplicated and why the failure mode is rejection rather than a default.

### ⑤ Sequence gap

`seq` is **8 bits**, carrying the low bits of a wide local counter (`packet-security-transport-spec.md` §3.6).

```
expected = (last_seq + 1) mod 256
gap      = (received - expected) mod 256
```

`gap > 0` raises a sync request and flags context as **suspect**.

It does **not** reset context or block the current packet. The lost message may have changed nothing relevant, and this packet may be self-contained and perfectly usable. Gap detection is an early warning; the hash is the authoritative test.

8 bits tolerates a burst of up to 127 lost or reordered packets before reconstruction becomes ambiguous. The width is fixed and is **not** read from `caps()`.

On the current UDP transport `caps.ordered` is false, so a reordered packet can raise a sync request that was not strictly necessary. Accepted — a spurious sync costs one small exchange, not a lost message.

### ⑥ Replay check

Sliding window over `seq`. A repeat is discarded silently — no repair request, since nothing was actually lost.

### ⑦ Hash verification

The receiver computes its own hash over its eight `(current, ver)` pairs — state **before** this message — and compares.

12 bits means a colliding mismatch passes roughly 1 in 4096 times. Widen to 16 if measurement shows desyncs are common.

Behaviour on mismatch **differs by tier** — see §6.

### ⑧ Model selection

```
tier 1 → static intent table + static slot-value table
         NO context boost (tier spec §5.9)
tier 2 → static n-gram table
         + context boost, only if hash_present == 1 (tier spec §6.5)
```

### ⑨ Decode

Pull exactly `symbol_count` symbols. Ignore flush and padding.

**This step cannot fail** — byte fallback guarantees every token exists, and the non-zero floor guarantees every token has a finite code length (tier spec §6.7).

---

## 4. Reconstruction — Tier 1

```
symbols → intent, slot mode mask, slot values

for each slot, by its mode:
    ID       → value carried in the payload
    LITERAL  → subwords carried in the payload
    REF      → previous message's value
    INHERIT  → context.current

if hash mismatched:
    REF and INHERIT slots → unresolved[]

look up template for (intent, RECEIVER's language)
fill slots via forms.bin, using the grammatical form
    required by each slot position
    ↓
text, in the RECEIVER's language
```

The receiver runs **no head selection and no rule table**. It decodes an intent ID and renders.

---

## 5. Reconstruction — Tier 2

```
symbols → subwords → concatenate
    ↓
reverse IR if adopted (span markers restore originally-Latin runs)
    ↓
text, in the SENDER's language
```

Marked as untranslated for the output layer (§7).

---

## 6. Hash Mismatch — Behaviour Differs By Tier

This asymmetry is deliberate and is the payoff of the Tier 1 static-model decision.

### 6.1 Tier 1 — degrades gracefully

Because Tier 1 uses a static model, the payload **still decodes** on a mismatch:

```
hash mismatch on a Tier 1 packet
   ↓
payload decodes normally
   ↓
intent and explicitly-sent slots are readable
   ↓
only INHERIT and REF slots go to unresolved[]
   ↓
"REQUEST MEDICAL at [unresolved] — urgent"
```

The operator learns most of the message. Request sync; do not commit context.

### 6.2 Tier 2 boosted — cannot decode

The context boost modifies the probability table. Mismatched contexts produce mismatched tables and the arithmetic decode desynchronises into garbage. Nothing partial is recoverable.

```
Request sync
Sender re-encodes UNBOOSTED (hash_present = 0)
That form is self-contained and always decodes
```

This is the context-free fallback path (`context-manager-spec.md` §18.3).

### 6.3 Never guess

In neither case may an unresolved slot be presented as a value. It goes to `unresolved[]` and nothing downstream may render it.

> A guessed slot produces a fluent, confident, wrong instruction that nobody can detect.
> A visible failure produces a human asking again.

---

## 7. Output Interface

What this pipeline hands downstream, per packet:

```
text            reconstructed message
language id     which language that text is in
mode            TIER_1 | TIER_2 | TIER_3
priority        NORMAL | CRITICAL
unresolved[]    slots that could not be resolved
status          ok | context_mismatch | integrity_fail
```

### 7.1 `unresolved[]` is a contract

A slot listed there **must not** be presented as a value by anything downstream — not spoken, not displayed, not defaulted.

This is the safety property crossing out of the decode layer. It is the one guarantee this layer cannot enforce itself, so it must be stated to whoever builds the output side.

### 7.2 `language id` is not always the receiver's

```
Tier 1  → receiver's language   (frame carried concept IDs)
Tier 2  → sender's language     (payload was the sender's text)
Tier 3  → sender's language
```

The output layer selects the voice from this field, not from the receiver's setting. A Tier 2 message from a Marathi speaker must be spoken with a Marathi voice even on a Gujarati-configured phone.

### 7.3 `mode` should be surfaced

The operator should always know whether they are hearing their own language or the sender's (`language-layer-spec.md` §10.3). How it is surfaced is the output layer's decision.

### 7.4 `priority` is NORMAL or CRITICAL

Two states only. **`HIGH` does not exist** — not on the wire, not in this interface, not in the UI. Full definition in `packet-security-transport-spec.md` §11.

`CRITICAL` obliges the output layer to preempt any `NORMAL` playback in progress, use the highest practical volume, and give vibration plus a critical visual indication. The system never lowers a message's priority.

Not to be confused with `rule_priority`, which is Tier 1 rule evaluation order and never leaves the sender (`tier-1-2-spec.md` §5.4).

---

## 8. Context Commit

```
Same commit() function the sender used, over the payload.
```

### 8.1 Commit only on success

| Condition | Commit? |
|---|---|
| Authentication failed | No |
| Negation copies disagreed | No |
| Replay | No |
| Seq gap, packet otherwise decoded | Yes |
| Hash mismatch | **No** |
| Decode succeeded | Yes |

Updating from a message you could not trust drives the two copies further apart.

### 8.2 Age is not reset by inheritance

```
Explicitly sent slot  → write value, ver++, age = 0
INHERIT slot          → age increments — it is NOT a write
REF slot              → age increments
```

Without this, staleness never fires and a location can be inherited indefinitely while the team moves away from it (`context-manager-spec.md` §4.3).

### 8.3 Tier 2 extraction

The receiver runs the same extractor over the reconstructed text to pick up concepts — **only when both phones share a language**.

```
Same language      → both update context from the text
Different language → both skip the context update
```

The sender knows the receiver's language from HELLO, so the decision is deterministic on both sides.

Tier 1 is unaffected: its commit is from language-neutral concept IDs, so cross-language Tier 1 messages update context normally.

---

## 9. Failure Branch Summary

| Trigger | Action | Context committed? | Repair |
|---|---|---|---|
| FEC uncorrectable | Discard | No | Final architecture only — no-op now |
| Authentication fail | Discard | No | Next good packet reveals gap |
| Negation copies disagree | Reject | No | Request repeat |
| Replay detected | Discard silently | No | None — nothing lost |
| Seq gap | Continue | Yes, if it decodes | Request sync |
| Hash mismatch, Tier 1 | Partial decode | No | Request sync |
| Hash mismatch, Tier 2 boosted | Cannot decode | No | Request unboosted resend |
| Decode error | Impossible (tier spec §6.7) | — | — |

---

## 10. What the Receiver Does Not Do

Deliberate asymmetry. Every one of these is a sender-side judgement, which is what keeps them retunable without breaking field compatibility.

```
no head selection
no rule table evaluation
no read-back check
no staleness reasoning
no pronoun resolution
no tier selection
no clause segmentation
```

---

## 11. Determinism Requirements

| # | Requirement |
|---|---|
| R1 | Authenticate before parsing. Parse before decoding. |
| R2 | Verify the context hash **before** decoding any inherited slot. |
| R3 | `commit()` is the same function the sender uses. |
| R4 | The Tier 2 extractor is the same code path the sender uses. |
| R5 | Metadata parsing uses fixed widths and MSB-first order, no coder state. |
| R6 | No `float` or `double` anywhere in the decode path. |
| R7 | Never substitute a value for an unresolved slot. |
| R8 | Commit only on successful decode with a matching hash. |
| R9 | `age` is not reset by INHERIT or REF. |
| R10 | Golden vectors decode identically on ≥3 SoC vendors. |

---

## Appendix A — Receiver Node

```
┌──────────────────────────────────────────────────────┐
│  RECEIVER NODE        Receive → Verify → Decode →    │
│                       Reconstruct → Emit             │
└──────────────────────────────────────────────────────┘
                          ↓
        ┌─────────────────────────────────────┐
        │  UDP receive · outer frame unwrap   │  Kotlin, existing
        │  → native payload                   │
        └─────────────────┬───────────────────┘
                          ↓
        ┌─────────────────────────────────────┐
        │  FEC decode   (no-op, current phase)│
        └─────────────────┬───────────────────┘
                          ↓
        ┌─────────────────────────────────────┐
        │  Decrypt + Authenticate             │  fail → discard
        └─────────────────┬───────────────────┘
                          ↓
        ┌─────────────────────────────────────┐
        │  Parse Metadata                     │
        │  tier · count · seq · priority      │
        └─────────────────┬───────────────────┘
                          ↓
        ┌─────────────────────────────────────┐
        │  GATES                              │
        │  negation agree? · seq gap?         │  fail → reject
        │  replay? · CONTEXT HASH?            │       → request sync
        └─────────────────┬───────────────────┘
                          ↓
        ┌───────────┬─────┴──────┬────────────┐
        ↓           ↓            ↓            │
   ┌─────────┐ ┌─────────┐ ┌ ─ ─ ─ ─ ┐        │
   │ Tier 1  │ │ Tier 2  │ │ Tier 3  │        │
   │ frame   │ │ subword │ │ stub    │        │
   │ decoder │ │ + ngram │ │         │        │
   │ STATIC  │ │ +boost  │ └ ─ ─ ─ ─ ┘        │
   └────┬────┘ └────┬────┘                    │
        │           │                         │
        └─────┬─────┘                         │
              ↓                               │
   ┌──────────────────────┐                   │
   │  Context Manager     │←──── read (Tier 1 inherit,
   │  read + commit       │      Tier 2 boost) ───────┘
   └──────────┬───────────┘
              ↓
   ┌──────────────────────┐
   │  Language Layer      │
   │  Tier 1 → receiver's language
   │  Tier 2 → sender's language
   └──────────┬───────────┘
              ↓
   ┌──────────────────────┐
   │  Output Interface    │
   │  text · language id  │
   │  mode · priority     │
   │  unresolved[] · status
   └──────────┬───────────┘
              ↓
        Output layer
        (TTS, display — out of scope)
```

---

## Appendix B — Decisions Register

| # | Decision | Status |
|---|---|---|
| 1 | Authenticate → parse → gate → decode, in that order | Locked |
| 2 | Context hash verified **before** decoding inherited slots | Locked |
| 3 | Negation disagreement rejects the packet | Locked |
| 4 | Seq gap warns but does not block or reset | Locked |
| 5 | Replay discarded silently | Locked |
| 6 | Tier 1 decodes on hash mismatch; only inherited slots unresolved | Locked |
| 7 | Tier 2 boosted cannot decode on mismatch; unboosted resend is the fix | Locked |
| 8 | Never substitute a value for an unresolved slot | Locked |
| 9 | Commit only on successful decode with matching hash | Locked |
| 10 | `age` not reset by INHERIT or REF | Locked |
| 11 | Tier 2 context extraction only when languages match | Locked |
| 12 | `language id` in the output drives voice selection, not the receiver setting | Locked |
| 13 | Receiver runs no sender-side logic (§10) | Locked |
| 14 | Input is the native payload; outer frame unwrapped by Kotlin | Locked |
| 15 | `seq` is 8 bits, fixed, not read from `caps()` | Locked |
| 16 | Priority is `NORMAL` or `CRITICAL` only | Locked |
