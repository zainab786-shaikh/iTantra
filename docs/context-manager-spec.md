# Context Manager — Component Specification

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Context Manager
**Version:** 1.2
**Status:** Final — ready for implementation
**Companions:** `language-layer-spec.md` v1.3 · `tier-1-2-spec.md` v1.5 · `packet-security-transport-spec.md` v1.2 · `receiver-pipeline-spec.md` v1.2
**Implementation:** C++17, integer arithmetic only, exposed to Android via JNI

### Implementation resolutions — v1.2 (no version bump, no spec-body change)

- §5.2 — **context hash algorithm resolved** (implementation plan Phase 1). §5.2 fixed the inputs but not the function. Pinned as **CRC-16/CCITT-FALSE**: polynomial `0x1021`, init `0xFFFF`, no input/output reflection, xorout `0x0000` (check value over ASCII `"123456789"` = `0x29B1`). Input is 24 bytes: for each slot in `SlotId` order, `current >> 8`, `current & 0xFF`, `ver`. Output is 16 bits. Implemented in `native/src/common/hash.h`.
- **Resolved in Phase 3:** the 12-bit wire field (`packet-security-transport-spec.md` §3.3) carries the low 12 bits of this 16-bit hash, `hash & 0x0FFF`. Both phones map their own pre-message hash and compare wire values. Recorded in that spec's implementation resolutions; implemented in `native/src/packet/metadata.h`.
- **Resolved in Phase 5 — `LAST_REF` null (§2.4):** `LAST_REF.current` stores the target slot index **+ 1**; **0 = null** (no reference active). Valid targets are `ACTOR` … `STATE`, stored 1 … 7; `LAST_REF` cannot point at itself, and a stored value above 7 is rejected. This keeps §5.3 exact: the all-zero initial table means "no reference", and its hash is unchanged. Implemented in `native/src/context/context.h`.

### Implementation resolutions — Phase 5 (no version bump, no spec-body change)

Pinned when the Context Manager was implemented (`native/src/context/`). Items marked *pairing* change the context hash or the committed state both phones must agree on.

- §4.2 first write to an empty slot (`current == 0`, `V != 0`): `current = V`, `ver += 1`, `age = 0`; nothing is pushed into `recent[]`. The rule's `S.current != 0` condition is read as "0 is never a value to remember". *pairing*
- Value `0` means empty (Appendix B) and is not writable; there is no clear operation. *pairing*
- §4.3 `age` saturates at 255 instead of wrapping, so a stale value can never read as fresh. Sender-only; no wire or hash effect.
- §4.3 every slot the message does not write ages by one: `INHERIT`, `REF`, `LITERAL` (never stored, tier §5.7) and slots the message does not mention.
- `TIME`: `commit()` refuses `INHERIT` **and** `REF` on `TIME` (tier §5.6, tier register #9, handoff "TIME never inherits"). A resolved `TIME` may be written.
- `commit(Context&, const CommitPayload&)`: one function for both phones. It validates the whole payload before applying any of it, so an invalid payload changes nothing. It recomputes `Context::hash` afterwards, which is the pre-message hash (§5.2) for the next message, and sets `Context::seq` to the committed message's `seq`. It never changes `context_id`.
- The payload `commit()` reads is one operation per slot — `Absent`, `Write(value)`, `Literal`, `Inherit`, `Ref` — built from the encoded or decoded payload, never from what was extracted (§10).
- **Resolved — `TIME` inheritance conflict:** §13.3 contains older wording that lets an absolute `TIME` be "stored and inherited once resolved". That wording is superseded. **Authoritative decision: `TIME` never inherits and never uses `REF`**, matching `tier-1-2-spec.md` §5.6 and decision register #9 and the implementation handoff. A resolved `TIME` may still be written (stored) explicitly. The Phase 5 implementation already enforces this: `commit()` returns `TimeInherited` for `INHERIT` or `REF` on `TIME` and leaves the context unchanged. Tests: `unit.context` `time_never_inherits`. The §13.3 text itself is left as written.
- **Open:** the reset flag of §13.4 has no packet field and no defined effect; not implemented.
- **Open:** `Context::seq` is one field, but both phones send. How `seq` is tracked per direction, and the commit order when both phones send at once, are receiver / synchronisation questions (Phases 10, 12).
- **Open:** how `REF` resolves to a value ("same as the previous message", tier §5.6, receiver §4), and how `LAST_REF` is chosen by the sender, belong to Tier 1 and the receiver. `commit()` needs neither: `REF` is not a write, and `LAST_REF` is written like any slot.
- **Phase 8 (Tier 1; tier spec implementation resolutions):**
  - `REF` decodes, commits and resolves exactly like `INHERIT`, to `context.current` — provisional as a meaning. The Phase 8 sender never emits `REF`; its final meaning is otherwise deferred (Phase 10). The Tier 1 golden vectors pin `REF`'s encoding, not its meaning, so **any future change to what `REF` means requires a Tier 1 table / protocol version bump**.
  - The Phase 8 sender never emits `REF` and never writes `LAST_REF`; both remain open.
  - Staleness is sender-side: fresh = `age <= max_inherit_age`, default 254 (provisional). Because `INHERIT` does not reset `age` (§4.3), a repeatedly inherited value goes stale and is then sent explicitly.
  - A fully explicit message (`allow_inheritance = false`) is supported. Which messages use it (the §16.1 periodic-explicit N) and the §13.4 reset flag remain open (Phase 12).
  - On a hash mismatch the receiver resolves explicit slots only (§17, §19); the receiver pipeline's commit decision is Phase 10.

### Changes from v1.1

- §5.1 — **`seq` locked at 8 bits.** Not read from `caps()`; v1.1 said otherwise.
- §16.1 — the development-transport recommendation is withdrawn. **UDP is retained**, so periodic full-explicit is the current mode.
- §21.1 — updated to match.

### Changes from v1.0

- §4.3 — **INHERIT and REF must not reset `age`.** Without this, staleness never fires.
- §16.1 — transport `caps()` now drives mode selection, replacing the manual choice.
- §12 — language switching confirmed free (no model reload); see language spec §4.5.

---

## 1. Purpose and Non-Goals

### 1.1 Purpose

A **small, deterministic memory** held independently on both phones. It retains only the semantic information from previous messages that is useful for handling future ones.

It exists to:

- avoid retransmitting information the other phone already has
- let the sender resolve references ("it", "they", "there") before encoding
- reduce bytes per message
- keep both phones interpreting messages identically

### 1.2 Non-Goals

Not a conversation history. Not a neural model. Not a conversation state machine (an FSM was evaluated and rejected — §22). Not a receiver-side inference engine.

### 1.3 Scope of reference resolution

**Sender-side only.**

When the speaker says "it has stopped", the sender resolves "it" against its own context and transmits either the resolved entity or an explicit inherit instruction. The receiver never performs pronoun resolution.

This is a hard boundary. If both phones resolved pronouns independently, any difference would desynchronise them silently.

---

## 2. Context Structure

A **fixed table of semantic slots**. It cannot grow at runtime.

### 2.1 Slots

```
ACTOR  OBJECT  LOCATION  SEVERITY  QUANTITY  TIME  STATE  LAST_REF
```

### 2.2 Meanings

| Slot | Purpose |
|---|---|
| `ACTOR` | Person, team, vehicle or device involved |
| `OBJECT` | Main entity involved |
| `LOCATION` | Relevant location |
| `SEVERITY` | Importance or severity |
| `QUANTITY` | Number or count |
| `TIME` | Resolved absolute time only (§13.3) |
| `STATE` | Current condition of an entity |
| `LAST_REF` | **Slot index** of the most recently referenced entity (§2.4) |

### 2.3 Deliberate exclusions

**`NEGATION` is not stored.** It is carried in every individual message. Inheriting a stale negation would reverse the meaning of a new one — the highest-consequence failure available in this system.

**`CONTEXT_ID` is not a semantic slot.** It is metadata (§5).

### 2.4 `LAST_REF` is a pointer

It stores a **slot index** (0–7), not a copy of a value. A copy could disagree with the value it duplicates, with no defined winner. A pointer cannot.

Null index when no reference is active.

---

## 3. One Value Per Slot

Each slot holds exactly **one current inheritable value**. This is required for inheritance to be unambiguous — "inherit location" must have exactly one possible answer.

### 3.1 Multiple facts in one utterance

1. **Split into clauses** (§7) and emit one frame per clause. Primary path.
2. **Fall back to Tier 2** if the clauses still do not fit the frame schema.

Both preserve all information.

---

## 4. Slot Metadata

```cpp
struct Slot {
    uint16_t current;      // the one inheritable value
    uint16_t recent[2];    // remembered, NOT inheritable
    uint8_t  ver;          // increments on every write to current
    uint8_t  age;          // messages since current was last written
};
```

### 4.1 `current`

The only value that may ever be inherited.

### 4.2 `recent[]` — update rule

Exists solely so recently-used values stay cheap to re-send: the entropy coder gives them elevated probability. **Never inherited.**

```
On writing a new value V to slot S where S.current != V and S.current != 0:
    S.recent[1] = S.recent[0]
    S.recent[0] = S.current
    S.current   = V
    S.ver      += 1
    S.age       = 0

On writing V where S.current == V:
    S.age = 0          // recent[] and ver unchanged
```

FIFO of depth 2. Oldest discarded on overflow.

**Load-bearing.** `recent[]` influences entropy-coding probabilities on both phones. Divergence desynchronises the arithmetic coder. Because it derives only from committed payloads (§10) using this fixed rule, both sides stay identical.

Not included in the context hash (§5.2).

### 4.3 `age` — and what does *not* reset it

Counts messages since `current` was last written. Used **only** by the sender when deciding whether to inherit (§13). No wire representation, no receiver-side effect.

> **INHERIT and REF must NOT reset `age`.**

```
Explicitly sent slot  → write value, ver++, age = 0
INHERIT slot          → age increments — it is NOT a write
REF slot              → age increments
```

If an inherited slot reset its age, a repeatedly-inherited value would sit at age 0 forever and the sender's staleness check would never fire. A location could then be inherited indefinitely while the team moved away from it.

**Age measures time since the value was last *asserted*, not since it was last *used*.**

### 4.4 `ver`

Increments on every write to `current`. Wraps at 256. Used for granular repair (§18.3).

---

## 5. Context Metadata

```cpp
struct Context {
    Slot     slots[8];
    uint16_t context_id;
    uint16_t hash;
    uint8_t  seq;
};
// approximately 100 bytes
```

### 5.1 `seq`

Sequence number of the last message applied. Detects gaps and reordering — a gap warns that context may be stale before the hash is even checked.

**Fixed at 8 bits on the wire**, carrying the low bits of a wide local counter (`packet-security-transport-spec.md` §3.6). It is **not** read from `caps()`.

8 bits tolerates a burst of up to 127 lost or reordered packets before reconstruction becomes ambiguous — comfortable on the current UDP transport, which guarantees neither delivery nor order.

### 5.2 `hash` — definition

> **The hash covers context state *before* the current message is applied.**

Mandatory and not variable. Both phones must hash at the same moment or the values never match.

- computed over all eight `(current, ver)` pairs
- `recent[]`, `age`, `seq`, `context_id` excluded
- integer arithmetic only

Purpose: the receiver answers *"do we agree on the starting point?"* **before** decoding any inherited slot. Check first, use second.

12 bits gives a ~1-in-4096 chance that a genuine mismatch collides and passes. Widen to 16 if measurement shows desyncs are common.

### 5.3 Initial state

Immediately after a successful HELLO (§18.1):

```
all slots:   current = 0, recent = {0,0}, ver = 0, age = 0
context_id = 0
seq        = 0
hash       = hash of the all-zero slot table
```

Empty matches empty, so the two phones are synchronised from the first instant. The first message inherits nothing.

---

## 6. Context Construction

Only after a complete sentence has been detected.

```
STT output
   ↓
Sentence completed (pause / endpoint)
   ↓
Normalize text
   ↓
Segment clauses
   ↓
Find known words and phrases
   ↓
Find typed values (numbers, quantities, times)
   ↓
Identify intent
   ↓
Create candidate context update
```

Deterministic, no neural model. The output is a **candidate**, not a commit (§10).

---

## 7. Clause Segmentation

```
"Fire at North Gate, casualties at South Gate."
        ↓
clause 1 → FIRE     { LOCATION: North Gate }
clause 2 → CASUALTY { LOCATION: South Gate }
```

Without this, a compound sentence violates §3.

**Properties:** sender-only, so no synchronisation risk. Deterministic — splitting on conjunctions and STT punctuation is a fixed rule. Runs **after** endpointing, since speakers do not reliably pause at clause boundaries.

---

## 8. Deterministic Extraction

```
Normalize            Unicode NFC, punctuation, number-words to digits
   ↓
Match known entries  single-pass Aho-Corasick over codebook + gazetteer
   ↓
Detect typed values  numbers, times, quantities
   ↓
Identify intent      keyword sets per intent
   ↓
Assign slot types    lookup, not a runtime decision
```

### 8.1 Slot type comes from the codebook

Each codebook entry stores its own semantic type. "North Gate" is tagged `LOCATION` at build time. At runtime the system reads a tag rather than making a decision.

### 8.2 Ambiguity

Resolved using the detected intent, since each intent declares which slots it expects. If ambiguity remains, **the affected slot is left unchanged** and the message must not be forced into Tier 1.

Both phones run this same extractor over the same text on Tier 2 messages, so both must reach the same conclusion.

### 8.3 Identical implementation

Compiled once, used by both sender and receiver. Same code path — not a re-implementation, not a variant.

---

## 9. Update Timing

```
Person speaks → streaming STT → pause → completed sentence
   ↓
Context candidate generated
   ↓
Tier selected → payload encoded
   ↓
Context committed
   ↓
Transmission
```

Partial STT output is **never** committed.

---

## 10. The Commit Rule

> **The sender commits what it actually encoded, never what it extracted.**

```
Sentence
   ↓
Extract candidate information       ← not committed
   ↓
Choose tier → build payload
   ↓
Commit context FROM the payload     ← committed here
   ↓
Transmit
```

### 10.1 Why

A Tier 1 frame carries a subset of what the sentence contained. Committing everything extracted would diverge the two contexts **on a perfectly delivered message**, with no packet loss involved.

### 10.2 Symmetry

Both phones call the **identical function**:

```cpp
void commit(Context& ctx, const Payload& p);
```

Sender on the payload it encoded; receiver on the payload it decoded. "Equivalent" is insufficient — two equivalent implementations drift.

### 10.3 Known consequence

Tier 1 builds a thinner context than Tier 2, because Tier 2 transmits the whole sentence and both phones extract everything in it.

Tier 1 therefore saves bytes now and may cost bytes later. Expected, not a defect. Measure it; factor into tier-selection thresholds.

---

## 11. Context and the Three Tiers

Shared by all three. **Not itself a tier**, and it sits upstream of tier selection.

```
                  Context Manager
                         │
          ┌──────────────┼──────────────┐
          ↓              ↓              ↓
       Tier 1          Tier 2          Tier 3
```

| Tier | Use of context |
|---|---|
| Tier 1 | Slot inheritance and references. **Static probability model — no context boost** (tier spec §5.9) |
| Tier 2 | Primes the entropy coder's frequency table (tier spec §6.4) |
| Tier 3 | Interface stub — see tier spec §7 |

Tier is an **encoding decision only**. It never determines what the phone remembers.

---

## 12. Language Switching Mid-Session

**Context survives a language change.**

The Context Manager stores concept numbers, not words. Switching from Hindi to Tamil swaps the lexicon and templates only — no speech model is reloaded (language spec §4.5), and the context table is untouched.

```
Before switch:  LOCATION = 312   ("उत्तर द्वार")
After switch:   LOCATION = 312   ("வடக்கு வாசல்")
                         ^^^ unchanged
```

No resynchronisation, no context reset, no model load, no message to the other phone.

---

## 13. Staleness

Context entries are **never deleted**. The sender decides a value is no longer safe to inherit and sends it explicitly. The value stays and continues to help the entropy coder.

### 13.1 Ownership

Computed **by the sender only**, at encode time, from `age`. No wire representation, no receiver-side effect. The receiver never expires anything.

### 13.2 No shared thresholds

> **There are no shared expiry thresholds, by design.**

Any rule both phones must apply independently is a chance to drift apart. Because staleness lives entirely on the sender, the heuristic can change at any time without touching the receiver and without breaking compatibility.

Starting heuristic: send explicitly if the value changed or if it is a never-inherit field. Refine from corpus measurement.

### 13.3 Never-inherit fields

| Field | Rule |
|---|---|
| `NEGATION` | Not in context at all. Explicit in every message. |
| `TIME` (relative) | Never stored, never inherited |
| `TIME` (absolute) | May be stored and inherited once resolved |

### 13.4 Clocks must not touch context

Staleness is measured in **messages, never wall-clock time**. The two phones' clocks are never identical; independent timers would drift apart silently.

A long pause is signalled by an **explicit reset flag** from the sender. The receiver acts on the flag, never on its own clock.

> Both phones derive context state purely from the messages they received. Never from anything local.

---

## 14. Context Transmission

The full context is **never** retransmitted. Changes ride inside normal messages.

```
Sender Context → message carrying only the change → Radio → Receiver Context
```

There is no separate context-update packet in normal operation. Additional packet types exist only for pairing and repair (§18).

---

## 15. Static and Dynamic Information

### 15.1 Static — preloaded, never transmitted

```
Codebook · semantic schema · common concepts
Generic locations (gate, building, road, river, hospital, bridge,
                   compass directions, relative positions)
```

Provisioned at install or pairing. Zero airtime.

### 15.2 Deployment-specific — provisioned at pairing

```
Local place names · team roster · mission profile
```

Split from the generic set so the system can be redeployed to a new district by swapping this layer alone.

### 15.3 Dynamic — learned during the conversation

```
Current entities · locations · states · recent information
```

Synchronised implicitly through normal messages.

---

## 16. Synchronisation Model

```
Sender Context ──────── synchronised ──────── Receiver Context
```

### 16.1 Mode selection — driven by transport capabilities

The sender must encode against the context it believes the **receiver** holds. Which strategy applies is read from the transport at pairing:

```cpp
caps.reliable && caps.ordered  →  OPTIMISTIC
otherwise                      →  PERIODIC_EXPLICIT
```

| Mode | Behaviour |
|---|---|
| **Optimistic** | Assume delivery. Detect mismatch via hash. Repair on failure. |
| **Periodic explicit** | Every Nth message uses no inheritance at all. Resynchronises automatically within N messages after any loss. |

The periodic-explicit mode is the video I-frame pattern: inherit freely between refresh points.

`caps()` selects the mode only. It does **not** set `seq` width, which is fixed at 8 bits (§5.1).

### 16.2 Current implementation — UDP, periodic full-explicit

The existing `UdpTransport` / `ThrottledTransport` is retained unchanged (`packet-security-transport-spec.md` §7.7). Therefore:

```
caps.reliable = false
caps.ordered  = false
      ↓
PERIODIC_EXPLICIT mode applies
```

Packet loss is possible and produces a genuine context gap. Reordering is possible and may raise a sync request that was not strictly necessary.

All of this layer's machinery — sequence tracking, the context hash, gap detection and the repair path — is **present and exercised** on this transport. It operates over a link that really can lose and reorder, which is closer to the eventual constrained radio than a reliable link would be.

**Recovery-path testing:** inject deliberate loss and reordering to exercise repair on purpose, rather than waiting for it.

No TCP or ACK layer is to be added for the current implementation.

---

## 17. Context Verification

Before decoding any message that relies on inherited context:

```
        Compare context hash
                 │
         ┌───────┴───────┐
       MATCH          MISMATCH
         │               │
         ↓               ↓
   Decode normally   Do not inherit
                          ↓
                     Enter recovery (§18.3)
                          ↓
                     Report unresolved (§19)
```

### 17.1 Rules

- Hash covers state **before** the current message (§5.2)
- Verification happens **before** any inherited slot is decoded
- A message using **no** inherited slots decodes normally regardless of hash state
- Hash is sent on every message that uses inheritance, omitted on fully explicit ones — self-adjusting, no threshold

Exact hash width and packet layout are in `packet-security-transport-spec.md`. The point-in-time definition in §5.2 is not negotiable.

---

## 18. Recovery Mechanisms

### 18.1 HELLO — pairing

Verifies compatible:

```
schema version · codebook version/hash · language configuration
operating role · packet format version · coder version
```

**Incompatibility must fail pairing visibly.** A codebook mismatch makes every subsequent message decode wrongly; it must fail loudly at pairing rather than mysteriously three messages later.

HELLO also carries each phone's language, which is what lets the sender decide the cross-language context rule (tier spec §11.3).

### 18.2 Normal message

Carries semantic information and any context change. No extra packets needed.

### 18.3 Synchronisation repair

**Two-way link:** the receiver sends its eight `ver` values; the sender diffs and resends only the slots that differ. This is what per-slot `ver` exists for.

**One-way / lossy link:** no repair request possible. A periodic fully explicit message re-establishes a known state (§16.1).

**Context-free fallback:** on repeated failure the sender re-encodes without inheritance and without the context boost, producing a self-contained message that decodes regardless of context state (tier spec §6.5).

---

## 19. Safety Rule

> **The receiver must never guess using untrusted context.**

If a message requires inheritance **and** the context does not match:

```
Do not decode the inherited information.
Report the affected slots as unresolved.
Request synchronisation or repetition.
```

### 19.1 Required behaviour

The system must not present a guessed slot as a value. It reports the slot in `unresolved[]` (see `receiver-pipeline-spec.md` §7), and nothing downstream may render it as a value.

### 19.2 Rationale

A guessed slot produces a **fluent, confident, wrong** instruction and nobody can tell. A visible failure produces a human asking again.

For an alert and distress system, an explicit failure is always safer than a fluent but incorrect message.

---

## 20. Determinism Requirements

Both phones run this layer's code — the receiver runs extraction on Tier 2 text to maintain its own context.

| # | Requirement |
|---|---|
| D1 | No `float` or `double` in encode, decode or commit. Thresholds are scaled integers. |
| D2 | `commit()` is one function used by both phones — not two implementations. |
| D3 | The extractor (§8) is one code path, used by both sides on Tier 2/3 messages. |
| D4 | Context derives **only** from received or transmitted messages. Never local time, sensors or state. |
| D5 | The hash covers pre-message state (§5.2). Both sides hash at the same moment. |
| D6 | `recent[]` follows the FIFO rule of §4.2 exactly. |
| D7 | `age` is not reset by INHERIT or REF (§4.3). |
| D8 | Pronoun resolution is sender-side only (§1.3). |
| D9 | Codebook, schema and template files versioned and verified at pairing (§18.1). |
| D10 | No neural inference in the context path. |
| D11 | Golden test vectors round-trip byte-identically on ≥3 phones with different SoC vendors. |

**D11 is not optional.** A system that passes on one device and fails on another fails in the demonstration room, not the lab.

---

## 21. Open Items

### 21.1 Link architecture

The optimistic / periodic-explicit choice (§16.1) and the repair packet layout (§18.3) are now **read from transport `caps()`**, so implementation is unblocked. What remains undecided is which radio is deployed.

The Bluetooth or Wi-Fi hop to the phone is unlikely to be the constrained link; the embedded device's backhaul is. Until settled:

- implement **both** modes
- select at pairing from `caps()`
- `seq` stays at 8 bits regardless (§5.1) — a negotiated width is a future extension, not current behaviour

### 21.2 To be settled by the prototype

- staleness heuristic (§13.2)
- tier-selection thresholds, including the Tier 1 context-thinning effect (§10.3)
- whether `recent[]` depth 2 is correct

---

## 22. Rejected Alternatives

| Alternative | Reason |
|---|---|
| Finite state machine for conversation state | State count grows without bound on unanticipated input. The component only needs current slot values — a table, not a state graph. |
| Dynamic / context-derived slot schema | Field meanings must be a fixed contract. A context mismatch would corrupt message *structure* rather than a single *value* — undetectable and unbounded. |
| Automatic expiry with shared thresholds | Requires both phones to apply an identical rule independently. Sender-only staleness achieves the same protection with no shared rule. |
| Multiple inheritable values per slot | Makes "inherit location" ambiguous with no safe resolution. Clause splitting handles multi-fact utterances. |
| Receiver-side pronoun resolution | Both phones would have to resolve identically on every input. |
| Neural model in the context path | Non-deterministic across SoCs and delegates; adds latency for no compression gain at this payload size. |

---

## Appendix A — Full Flow

```
                 COMPLETED STT SENTENCE
                           │
                           ▼
                ┌────────────────────┐
                │   Normalize        │
                │   Segment clauses  │
                │   Match entries    │
                │   Detect values    │
                │   Identify intent  │
                └─────────┬──────────┘
                          ▼
                ┌────────────────────┐
                │  Context Manager   │
                │  ACTOR  OBJECT     │
                │  LOCATION SEVERITY │
                │  QUANTITY TIME     │
                │  STATE  LAST_REF   │
                └─────────┬──────────┘
                          ▼
                   Candidate Update
                          ▼
                  Select Tier 1/2/3
                          ▼
                   Build Payload
                          ▼
              Commit context FROM PAYLOAD
                          ▼
                   Packet assembly
        ══════════════════╪══════════════════
                          ▼
                 Verify context hash
                  ┌───────┴───────┐
                MATCH          MISMATCH
                  ▼               ▼
             Decode msg      Do not inherit
                  │          Report unresolved
                  ▼          Enter recovery
        Commit context FROM PAYLOAD
              (same function)
```

## Appendix B — Structures

```cpp
struct Slot {
    uint16_t current;      // inheritable value; codebook ID, 0 = empty
    uint16_t recent[2];    // FIFO of displaced values; never inherited
    uint8_t  ver;          // increments on write to current; wraps at 256
    uint8_t  age;          // messages since write; NOT reset by INHERIT/REF
};

enum SlotId : uint8_t {
    SLOT_ACTOR    = 0,
    SLOT_OBJECT   = 1,
    SLOT_LOCATION = 2,
    SLOT_SEVERITY = 3,
    SLOT_QUANTITY = 4,
    SLOT_TIME     = 5,
    SLOT_STATE    = 6,
    SLOT_LAST_REF = 7,     // holds a SlotId, not a value
    SLOT_COUNT    = 8
};

struct Context {
    Slot     slots[SLOT_COUNT];
    uint16_t context_id;
    uint16_t hash;         // over (current, ver) pairs, pre-message state
    uint8_t  seq;
};

void commit(Context& ctx, const Payload& p);   // ONE function, both phones
```

**Slot indices must never be reordered.** They are part of the wire contract via `LAST_REF` and the slot presence mask.
