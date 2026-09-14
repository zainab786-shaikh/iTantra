# Tier 1 & Tier 2 Compression — Component Specification

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Compression Tiers
**Version:** 1.5
**Status:** Final — ready for implementation
**Scope:** Optimisation / compression only. Speech recognition, synthesis, playback and UI are out of scope.
**Companions:** `language-layer-spec.md` v1.3 · `context-manager-spec.md` v1.2 · `packet-security-transport-spec.md` v1.2 · `receiver-pipeline-spec.md` v1.2
**Implementation:** C++17, integer arithmetic only, exposed to Android via JNI

### Implementation resolutions — v1.5 (no version bump, no spec-body change)

- §5.6 / register #9 — **`TIME` never inherits and never uses `REF`.** This is authoritative. `context-manager-spec.md` §13.3 contains older wording that allows a resolved absolute `TIME` to be inherited; that wording is superseded (recorded in the context spec's Phase 5 implementation resolutions). A resolved `TIME` may still be sent explicitly. Enforced in `native/src/context/context.cpp` (`commit()` refuses `INHERIT` / `REF` on `TIME`) and tested by `unit.context` `time_never_inherits`. The Phase 8 slot-resolution code must not emit `INHERIT` or `REF` for `TIME`.

### Changes from v1.4

- §5.4 — the rule table's numeric ordering field is renamed **`rule_priority`**, to keep it distinct from message priority.
- §5.8, §13.2 — message priority terminology standardised to **`NORMAL` / `CRITICAL`**. `HIGH` removed.
- §9 — packet boundary clarified: this layer produces symbols and metadata for the **native payload**, which nests inside the existing Kotlin outer frame.
- §13.3 — `seq` width is fixed at 8 bits, not read from `caps()`.

### Changes from v1.3

- §6.5 — **correction.** Tier 2 with the context boost *does* require the context hash. v1.3 §9.2 stated otherwise and was wrong: a context mismatch desynchronises the decode.
- §5.9 — **new decision.** Tier 1 uses a **static** probability model, no context boost. Buys graceful partial decode on hash mismatch.
- §8.4 — cross-language tier preference (deferred feature, recorded).
- §10 — receiver decode path expanded; full detail now in `receiver-pipeline-spec.md`.
- §13 — measure the tier margin at **packet** level, not payload level. Coder flush cost added as a harness row.
- §9 — packet format moved to `packet-security-transport-spec.md`.

---

## 1. Purpose and Scope

Turns one clause of recognised speech into the smallest safe payload.

```
Tier 1   semantic frame + codebook    lossy by design, gated by a safety check
Tier 2   lossless text compression    the safety floor; always works
Tier 3   neural compression           experimental stub — §7
```

### 1.1 The organising principle

The tiers are not three implementations of one idea. They are three **loss budgets**:

| Tier | Loss | Consequence |
|---|---|---|
| Tier 1 | Lossy — discards wording, keeps meaning | Can use a language-neutral representation |
| Tier 2 | Lossless — preserves the sentence exactly | Must stay in the sentence's own language |

Everything else follows. Tier 1 is small *because* it throws information away; the read-back check (§5.8) decides when that is safe. Tier 2 is the floor, and a floor that alters what it catches is not a floor.

This is why the two tiers do not and cannot share a transmitted representation, and why only Tier 1 crosses languages (language spec §10).

---

## 2. Position in the Pipeline

```
        ═══ upstream, out of scope ═══
                     ↓
Common IR ?                    ← optional, §13.1
  ↓
  ├──────────────────────────────┐
  ↓                              ↓
Semantic extraction         text + confidence
  ↓                              │
Context Manager  ────────────────┤   read here; committed at the end
  ↓                              ↓
Tier 1 encode               Tier 2 encode
(may fail)                  (always succeeds)
  ↓                              ↓
  └────── select: safe AND smaller ──────┘
                  ↓
        Commit context FROM PAYLOAD
                  ↓
            Packet assembly
```

### 2.1 Interface — what arrives

```
recognised text     one clause, in the speaker's language
confidence          a normalised score for that clause
language id         the sender's configured language
```

The recogniser is out of scope. Confidence is an opaque value compared against a per-language threshold (§5.8).

### 2.2 The text must be carried forward

Tier 2 compresses the sentence, not the frame. If only the frame reaches the tier stage, Tier 2 has nothing to work with. Confidence travels with it — the tier selector needs it (§8.1).

---

## 3. Shared Foundations

### 3.1 One arithmetic coder, two symbol sources

```
Tier 1 → [ intent, slot modes, slot values ]  ─┐
                                               ├→ coder → bytes
Tier 2 → [ subword, subword, subword, … ]     ─┘
```

Only the meaning of the symbols differs. One coder to write, one to test, one to prove bit-exact.

### 3.2 Integer only

No `float` or `double` in encode, decode, model or threshold paths. Probabilities and thresholds are scaled integers.

A single rounding difference between devices desynchronises the coder, and a desynchronised arithmetic coder does not produce a small error — it produces garbage from that point onward.

### 3.3 Not fixed-width fields

Intent IDs and slot values are entropy-coded. Intents are far from uniformly likely — a medical request vastly outnumbers a supply-level query. Fixed width spends the same bits on both.

### 3.4 Shared versus sender-only

| | Shared (wire contract) | Sender only |
|---|---|---|
| Concept IDs, intent IDs, slot enum | ✓ | |
| Subword vocabulary, n-gram table | ✓ | |
| Head-selection precedence | | ✓ |
| Rule table | | ✓ |
| Concept class and category | | ✓ |
| Read-back thresholds | | ✓ |
| Adjacency FSM | | ✓ |

**The receiver never runs head selection or the rule table.** It decodes an intent ID and renders. So rules, classes, categories and thresholds ship as sender-side updates without breaking compatibility with phones already in the field.

---

## 4. Unit of Work: the Clause

```
"Fire at north gate, send help immediately"
        ↓ clause segmentation (language layer)
clause 1 → tier decision → packet
clause 2 → tier decision → packet
```

### 4.1 Rules

- **Tier is chosen per clause.** One utterance may produce a Tier 1 packet then a Tier 2 packet. Mixing is expected — a clause that frames cleanly should not be penalised because the next one doesn't.
- **One packet per clause**, streamed as each clause completes.
- **Context commits in clause order.**

### 4.2 Known alternative

Bundling clauses would amortise the header. It complicates partial loss, prevents per-clause mixing, and is deferred. Revisit if §13.2's header-overhead measurement is significant — note that overhead matters more than it looks (§13.1).

---

## 5. Tier 1 — Semantic Frame

### 5.1 Input

```
slot values      concept IDs (uint16), from the language layer
negation flag    per clause
STT confidence
context          read-only at this stage
```

Tier 1 never touches text. Everything is integers — which is why one implementation serves all 10 languages.

### 5.2 Adjacency check

Runs first.

```
NEUTRAL ──sent a QUERY intent──→ AWAITING_ANSWER
AWAITING_ANSWER ──any message sent, or timeout──→ NEUTRAL
```

While in `AWAITING_ANSWER`, a bare value answers the pending question:

```
pending = QUERY_CASUALTY_COUNT
heard   = QUANTITY 3
→ intent ANSWER_COUNT, QUANTITY = 3
```

Terse replies — "three", "yes", "confirmed" — have no predicate and would otherwise always miss Tier 1. On push-to-talk they are among the most common messages.

**Sender-side only.** Three states, cannot explode, times out.

### 5.3 Head selection

```
ACTION  >  EVENT  >  STATE  >  ENTITY  >  MODIFIER
```

Highest class present wins; ties break by slot enum order.

```
No head found                  → INTENT_NONE → Tier 2
Two concepts of the top class  → INTENT_NONE → Tier 2
```

Two predicates in one clause means segmentation failed. Reject rather than pick one.

### 5.4 Rule table

Buckets keyed by head concept, each pre-sorted by descending `rule_priority`. First match wins.

> **`rule_priority` is rule evaluation order, not message priority.** It decides which rule matches first. It has no relationship to `NORMAL` / `CRITICAL`, which is a property of the message (`packet-security-transport-spec.md` §11).

```cpp
struct Condition {
    uint8_t  slot;
    uint8_t  op;         // PRESENT | ABSENT | EQ | IN_CATEGORY | NEG_SET | NEG_CLEAR
    uint16_t operand;    // concept ID or category mask
};

struct Rule {
    uint16_t  head_concept;
    uint8_t   rule_priority;     // evaluation order ONLY — not message priority
    uint16_t  intent;
    uint8_t   cond_count;
    Condition conds[4];
};
```

```
bucket[SEND]:
  rule_priority 200  [NEG_SET]                                 → CANCEL_REQUEST
  rule_priority 100  [OBJECT IN_CATEGORY MEDICAL, LOC PRESENT] → REQUEST_MEDICAL_AT
  rule_priority 100  [OBJECT IN_CATEGORY SUPPLY,  LOC PRESENT] → REQUEST_SUPPLY_AT
  rule_priority  10  []                                        → REQUEST_GENERIC
```

**Negation is a condition, not merely a flag.** "Do not send help" is a cancellation — a different intent, not the same intent with a bit set.

`category_mask` keeps the table small: one rule for "any medical object" rather than forty.

### 5.5 `head_implied`

```
true   → head not transmitted        REPORT_FIRE_AT implies fire
false  → head sent in its slot       REPORT_STATE does not imply which state
```

Without it, `REPORT_FIRE_AT` spends bits re-sending "fire". With a separate intent per state instead, the intent list multiplies for no gain.

### 5.6 Slot resolution

```
Head implied by the intent?        → drop it
Never-inherit slot?                → send explicitly
Value changed since last message?  → send explicitly
Value unchanged and fresh?         → INHERIT
Concept not in the codebook?       → LITERAL
Required slot missing, no context? → INTENT_NONE → Tier 2
```

| Mode | Meaning | Relative cost |
|---|---|---|
| `INHERIT` | Omitted; receiver fills from context | none |
| `REF` | Same as the previous message | lowest coded |
| `ID` | Codebook concept ID | moderate |
| `LITERAL` | Text, for a concept not in the codebook | highest |

`TIME` never inherits. `NEGATION` is not a slot — it lives in the packet metadata.

Staleness is a **sender-side** judgement from the context's `age` field. Note that INHERIT and REF do **not** reset `age` (context spec §4.3) — otherwise staleness never fires.

### 5.7 Literals

```
"tell Ramesh to move"
  intent = REQUEST_MOVE
  ACTOR  = LITERAL "Ramesh"
```

- Encoded with the **same subword coder Tier 2 uses**. Byte fallback (§6.2) means any script always encodes.
- **Never inherited, never stored in context.** Storing them would require both phones to maintain identical literal tables and expire entries at the same instant — a synchronisation hazard not worth a few bytes.
- Repeating a literal costs the same each time. Known inefficiency; measure before optimising.
- A value that recurs often belongs in the deployment gazetteer (language spec §15.2). Provisioning fix, not protocol fix.

Too many literals is self-correcting: the frame grows, Tier 2 encodes smaller, §8.2 selects Tier 2.

### 5.8 Read-back check

The safety gate, and the reason Tier 1 is allowed to be lossy.

```
Build the frame
      ↓
Render it back to a sentence using the SENDER's own templates,
with inherited values resolved
      ↓
Compare against what was actually said
      ↓
similarity ≥ bar  → Tier 1 permitted
otherwise         → Tier 2
```

The sender simulates the receiver and answers the only question that matters: *if I send this, will the other person hear what I meant?*

**Comparison:** word-level edit distance over the normalised token sequence, integer arithmetic.

**The bar is higher for `CRITICAL` than for `NORMAL`.** Urgent messages are where a dropped clause costs most, so the standard rises rather than falls.

**Low input confidence disables Tier 1 outright.** A wrong frame reconstructs into a *fluent, confident, wrong* sentence, and nothing downstream can tell. A damaged Tier 2 payload arrives recognisably damaged and the recipient asks again. Tier 1 launders uncertainty into fluency.

Thresholds are per language (language spec §11.1).

### 5.9 Encoding — static model, no context boost

```
symbols → [ intent, slot presence/mode mask, slot values, literal subwords ]
        → arithmetic coder, STATIC tables
        → payload
```

> **Tier 1 uses a static probability model. The context boost of §6.4 is NOT applied.**

Tier 1's compression comes from *inheritance* (0 bits for a known slot), not from probability boosting. A boost would gain little and cost a great deal: the payload would become undecodable whenever contexts disagreed.

With a static model, a hash mismatch degrades gracefully:

```
hash mismatch on a Tier 1 packet
   ↓
payload still decodes
   ↓
intent and explicitly-sent slots are readable
   ↓
only INHERIT and REF slots are unresolved
   ↓
"REQUEST MEDICAL at [unresolved] — urgent"
```

The operator learns most of the message. Far better than losing it.

---

## 6. Tier 2 — Lossless Text

### 6.1 Input

The clause text as produced by the language layer (IR form if §13.1 adopts it, otherwise native script).

Tier 2 compresses the **original wording**, not the semantic representation. Concept IDs cover only recognised words; the unrecognised ones are usually why the clause fell through.

### 6.2 Symbol alphabet — subwords with byte fallback

```
"வடக்கு வாசலில் தீ"
       ↓ shared multilingual subword vocabulary
[ வடக்கு ][ வாசல் ][ இல் ][ தீ ]
```

**One vocabulary for all 10 languages.** Subwords rather than words, because Tamil, Kannada, Malayalam and Telugu generate dozens of forms per root.

The tokenizer is fixed, deterministic and versioned.

#### Byte fallback — the tokenizer must be total

**All 256 byte values are vocabulary entries.** Anything the subword vocabulary cannot cover decomposes into raw bytes.

```
"வாசல்"   → [ வாசல் ]                  known subword
"ẍ"       → [ 0xE1 ][ 0xBA ][ 0x8D ]   byte fallback
```

Tier 2 is the safety floor. An `<unk>` token is **lossy**, and a floor with a hole in it is not a floor. Byte fallback makes the tokenizer *total*: it cannot fail on any input, in any script, ever.

Cost: 256 entries, negligible, plus more bits for rare sequences — correct behaviour.

**Decomposition rule, fixed:** longest match against the subword vocabulary first, byte tokens for the remainder.

**This also protects Tier 1** — literals use the same coder (§5.7).

### 6.3 Probability model

A **static** table, trained on the corpus and shipped in the app:

```
order-2 or order-3 over subwords
Kneser-Ney or PPM escape backoff
```

#### Non-zero floor — a hard requirement

Byte fallback guarantees a token exists. It does **not** guarantee the model has seen it. A byte token absent from training would receive probability zero, and `-log2(0)` is infinite — the encoder breaks.

```
Requirement: every token in the vocabulary has p > 0
             in every context, without exception.
```

The backoff chain must terminate in a **uniform floor over the entire vocabulary**, byte tokens included. Kneser-Ney and PPM provide this by construction, but a naïve implementation will emit a zero and only fail on the one input nobody tested.

Both are required. Byte fallback alone still breaks (token codes to infinity). Smoothing alone still breaks (no token at all).

#### Why not a neural model

A neural LM needs roughly 10–100× more tokens than parameters to avoid heavy overfitting — hundreds of thousands of sentences per language. A few thousand will exist. And because arithmetic-coding cost is `-log2(p)`, an overfit model does not compress slightly worse: a confidently wrong prediction costs *many* bits. A smoothed n-gram estimates far fewer parameters and works with the data that will exist.

### 6.4 Context boost

Entities in the Context Manager have their subwords' counts raised before coding:

```
Context: LOCATION = 312
   ↓ concept → subword table (shared, all 10 languages)
[ வடக்கு ][ வாசல் ]
   ↓
counts boosted by a fixed integer amount
```

A place already mentioned costs a few bits instead of a dozen. This recovers most of the benefit sending concept IDs would give, with none of the machinery.

The concept-to-subword table is part of the shared, versioned contract, so both phones compute an identical boost regardless of who is speaking.

### 6.5 The boost requires the context hash

> **Correction to v1.3 §9.2, which stated Tier 2 needs no context hash.**

Tier 2 does not inherit *slots*, but the boost modifies the **probability table**. If the two contexts differ, the tables differ, and the arithmetic decode desynchronises into garbage. Nothing partial is recoverable.

No new field is needed — `hash_present` already carries it:

```
hash_present = 1   boosted; hash checked; decode requires a match
hash_present = 0   unboosted; genuinely self-contained; always decodes
```

**The unboosted form is the true recovery path.** On a sync failure the sender re-encodes unboosted and the message gets through regardless of context state — which is the context-free fallback `context-manager-spec.md` §18.3 requires.

### 6.6 No within-session adaptation

```
Reset to the static table at every clause boundary
Apply the context boost (if enabled for this message)
Encode
```

A coder that learns as it goes compresses better, but a lost packet diverges the two tables and a diverged arithmetic coder produces garbage. The only session-dependent input is the Context Manager, which is hash-checked and repairable.

Some compression is given up. What is gained is that **Tier 2 can never be poisoned by packet loss.**

### 6.7 Tier 2 always succeeds

There is no failure path. This is a guarantee, and it rests on exactly two things:

```
§6.2  byte fallback   → a token always exists
§6.3  non-zero floor  → that token always has a finite code length
```

Remove either and it becomes "almost always" — which, for a safety floor, is the same as no guarantee.

**Test it directly.** Feed the encoder random bytes, mixed scripts, empty input, and characters absent from every training corpus. It must produce a decodable payload every time.

---

## 7. Tier 3 — Experimental Stub

```
in:  text + context        out: bytes + tier flag
```

Returns "not available". The interface is retained so the evaluation is real rather than hypothetical.

### 7.1 Four independent reasons it is not built

**Decode latency.** One forward pass per symbol. Encoding can be a single parallel pass; **decoding cannot** — symbol *N* must be decoded before *N+1* can be predicted. That serial cost sits on the receive path.

**Resident memory.** Cannot be loaded on demand (hundreds of ms, worse than the decode), so it stays in RAM permanently on top of the speech models. RAM paid *always* for a tier used *rarely* — the worst shape for the efficiency score.

**Training data.** As §6.3. The data does not exist at the necessary scale for these languages, and an underfed model compresses *worse* than the n-gram that shipped.

**History desynchronisation.** An LM's advantage comes from conditioning on prior text. After a Tier 1 message the two phones hold *different* prior text — the sender has the original sentence, the receiver its own language's rendering. Conditioning on that desynchronises the coder. The safe workaround is to condition only on prior Tier 2/3 messages, which in a Tier-1-dominated conversation leaves almost no history, removing the advantage that motivated the tier.

### 7.2 What to record

Document all four with measurements. *"We evaluated this and rejected it, here is the data"* is stronger than having built it.

---

## 8. Tier Selection

Two independent questions, in order.

### 8.1 Is Tier 1 safe?

| Trigger | Result |
|---|---|
| STT confidence below the language's threshold | Tier 2 |
| No head concept found | Tier 2 |
| Two concepts of the top class | Tier 2 |
| No rule matches | Tier 2 |
| Required slot missing and absent from context | Tier 2 |
| Read-back lost meaning | Tier 2 |
| Negation copies disagree | Tier 2 |

### 8.2 Is Tier 1 smaller?

Only asked if Tier 1 is safe. **Encode both and compare** rather than predicting:

```
Tier 1 → 9 bytes
Tier 2 → 7 bytes
         ↑ send this
```

Both encodings take microseconds. This also removes any need for a literal count limit — a frame heavy with literals loses on size.

### 8.3 Known second-order effect

**Tier 2 leaves both phones with a richer context than Tier 1.** The whole sentence passes through, so both extract everything they recognise; a Tier 1 frame carries only what fitted.

A marginally larger Tier 2 message may pay for itself later through better inheritance. Measure it; do not optimise for it yet.

### 8.4 Cross-language preference — deferred

When the two phones are set to different languages, §8.2's rule is wrong: only Tier 1 delivers in the listener's language (language spec §10).

```
same language      →  pick safe AND smaller          (current behaviour)
cross-language     →  prefer Tier 1 whenever SAFE,
                      even if Tier 2 encodes smaller
```

The safety gate is untouched — this changes only what happens when both tiers are available.

**Deferred** until after core implementation, along with the operator toggle (language spec §10.6). Cross-language itself works today without this; the preference rule only improves how often it applies.

---

## 9. Packet Format

Owned by `packet-security-transport-spec.md` — metadata layout, flush, padding, encryption and the transport interface.

### 9.1 What this layer supplies

```
tier            1 or 2
symbols         the coded symbol list
model           which probability table was used
metadata        priority (NORMAL | CRITICAL), negation,
                hash_present, context_hash,
                language (Tier 2), symbol count
```

### 9.2 Where that lands in the stack

This layer's output becomes the **native payload**, which nests inside the existing Kotlin frame. It is not the complete wire packet.

```
Kotlin   ITantraPacket outer frame        existing, unchanged
           ↓ contains
C++      [ metadata ][ compressed symbols ][ flush ][ pad ][ tag ]
           ↓ carried by
Kotlin   UdpTransport / ThrottledTransport → UDP     existing, unchanged
```

The new Tier 1 / Tier 2 format **replaces the prototype's RAW / PACK7 / PHRASE payload generation**. It does not replace the outer framing or the transport. See `packet-security-transport-spec.md` §1.1–1.4.

---

## 10. Receiver

Full detail in `receiver-pipeline-spec.md`. Summary of this layer's part:

```
Verify context hash          ← before decoding any inherited slot
   ↓
MATCH ─────→ decode → resolve inherited slots
MISMATCH ──→ Tier 1: decode anyway (static model, §5.9),
                     report INHERIT/REF slots as unresolved
             Tier 2 boosted: cannot decode, request unboosted resend
   ↓
Tier 1 → frame → templates in the RECEIVER's language
Tier 2 → text  in the SENDER's language
   ↓
Commit context (same function the sender used)
```

The receiver runs **no head selection and no rule table**.

---

## 11. Context Interaction

### 11.1 Read

Consulted during slot resolution (§5.6) and for the Tier 2 boost (§6.4). Not a pass-through stage.

### 11.2 Commit

```
Commit context FROM THE ENCODED PAYLOAD — never from what was extracted
```

The sender extracted more than a Tier 1 frame carries. Committing everything extracted would diverge the contexts **on a perfectly delivered message**.

For Tier 2 the payload *is* the sentence, so the distinction vanishes — another reason Tier 2 builds a richer context.

Both phones call the **identical function**.

### 11.3 Cross-language Tier 2

If the phones are set to different languages the receiver has no lexicon for the sender's text and cannot extract concepts. The sender could, so they would drift apart.

```
Same language      → both update context from the text
Different language → both skip the context update
```

The sender knows the receiver's language from HELLO, so the decision is deterministic on both sides. The payload still decodes correctly; only the context update is skipped.

Note this does **not** affect Tier 1, whose commit is from language-neutral concept IDs — so cross-language Tier 1 messages update context normally on both phones. Preferring Tier 1 in cross-language sessions (§8.4) therefore preserves context quality as well as language.

---

## 12. Determinism Requirements

| # | Requirement |
|---|---|
| T1 | No `float` or `double` in any encode, decode, model or threshold path. |
| T2 | One arithmetic coder implementation, shared by both tiers and both phones. |
| T3 | Subword tokenizer fixed and versioned; identical output on every device. |
| T3a | Byte fallback present; tokenizer is total and cannot fail on any input (§6.2). |
| T3b | Every vocabulary token has p > 0 in every context; backoff terminates in a uniform floor (§6.3). |
| T4 | n-gram table static within a session; reset at every clause boundary. |
| T4a | **Tier 1 uses static tables only — no context boost** (§5.9). |
| T5 | Tier 2's context boost derived only from synchronised context state, and gated by `hash_present` (§6.5). |
| T6 | `commit()` is one function used by sender and receiver alike. |
| T7 | Rule evaluation order fixed at build time — sorted by `rule_priority`, first match wins. |
| T8 | Head-selection precedence and tie-breaking fixed and documented. |
| T9 | Read-back comparison in integer arithmetic. |
| T10 | Golden test vectors round-trip byte-identically on ≥3 phones with different SoC vendors. |

**T10 is not optional.**

### 12.1 Build-time validation

Enforce in the rule compiler, not at runtime:

1. No two rules in a bucket share a `rule_priority`
2. Every intent reachable by at least one rule
3. Every rule's conditions reference slots its intent declares
4. Every ACTION / EVENT / STATE concept has at least one rule
5. Coverage report — run the corpus, list clauses reaching `INTENT_NONE`

Item 5 is the Tier 1 hit rate, measured directly. Given language spec §10.4, it is also the cross-language coverage figure.

---

## 13. Parameters to Measure

**Nothing numeric in this document is a decision.**

### 13.1 Structural, decided by one benchmark each

| Question | How it is settled |
|---|---|
| **Common IR (romanised)** | One harness row: native-script Tier 2 vs romanised Tier 2, same corpus. Adopt only on a meaningful margin; if adopted, span marking for code-mixed Latin text becomes mandatory. Rejecting it deletes one box. **Compression only — not a cross-language mechanism** (language spec §17.3). |
| **Does Tier 1 justify frames?** | **Measure at packet level, not payload level.** |

> **Why packet level.** Every packet pays the same metadata, flush, padding and integrity overhead regardless of tier. A payload-level comparison overstates Tier 1's advantage substantially:
>
> ```
>               payload    packet
> Tier 1          2 B       ~11 B
> Tier 2          7 B       ~16 B
>
> payload ratio   3.5×
> packet ratio    1.45×
> ```
>
> Comparing payloads would conclude Tier 1 is worth far more complexity than it actually is. It also raises the value of §4.2 bundling, which amortises the fixed overhead.

### 13.2 Values

```
Tier 1 payload size, by intent
Tier 2 payload size, by language
Tier 1 hit rate (= cross-language coverage)
Read-back threshold — NORMAL and CRITICAL
STT confidence threshold — per language
Intent count · rule count
Subword vocabulary size
n-gram order (2 vs 3) · Kneser-Ney vs PPM
Context boost magnitude
Coder flush cost                        ← see below
Header overhead as a fraction of packet (decides §4.2 bundling)
Tier 2 context-richness effect (§8.3)
Literal repetition cost (§5.7)
```

**Coder flush cost is not a rounding error at these sizes.** A standard 32-bit range coder flushes ~4 bytes — on a 2-byte payload the terminator costs more than the message.

```
32-bit range coder, naive flush     ~4 bytes
carefully-terminated range coder    ~1–2 bytes
rANS, compact termination           ~1 byte
```

Treat coder choice as a measured decision, not a default.

### 13.3 External dependency

`seq` is **fixed at 8 bits** on the wire, backed by a `uint32_t` or wider local counter. It is **not** read from `caps()`. See `packet-security-transport-spec.md` §3.6.

What `caps()` does select is the context mode — optimistic versus periodic full-explicit. For the current implementation over UDP, `caps.reliable` and `caps.ordered` are both false, so periodic full-explicit applies.

---

## 14. Rejected Alternatives

| Alternative | Reason |
|---|---|
| **Neural LM in the compression path** | Serial decode, one pass per symbol; resident RAM for a rarely-used tier; training data does not exist at scale; an underfed model compresses worse because `-log2(p)` punishes confident errors. Retained as a stub. |
| **Within-session adaptive coder** | One lost packet diverges the tables and a diverged arithmetic coder emits garbage, not a small error. |
| **Common transmitted representation for all tiers** | The tiers are defined by differing loss budgets. A lossless representation cannot express a frame, because a frame is defined by what it discards. Forcing one deletes Tier 1's entire advantage. |
| **Translation to a canonical language** | Breaks the read-back check — the comparison runs against translated text, so an error passes undetected. Also makes Tier 2 and Tier 3 lossy, removing the safety floor. |
| **Codebook IDs inside Tier 2** | Circular — mixed IDs plus text is Tier 1 with literals. The context boost recovers most of the benefit. |
| **Context boost on Tier 1** | Would make Tier 1 undecodable on hash mismatch, losing graceful partial decode for a negligible size gain (§5.9). |
| **Compiling the rule table to a DFA** | ~3–5× faster, but both forms are sub-microsecond against a 300–700 ms endpointer. Reviewability by domain experts is worth more. |
| **Conversation state machine** | States must cover every possible next utterance. The bounded three-state adjacency check (§5.2) captures the useful part. |
| **Fixed-width intent and slot fields** | Intents are far from uniformly distributed. |
| **Storing literals in context** | Both phones would need identical literal tables expiring at the same instant. |

---

## Appendix A — Sender Flow

```
                     CLAUSE
                       │
        ┌──────────────┴──────────────┐
        ↓                             ↓
  concept IDs                   text + confidence
        │                             │
        ↓                             │
  ┌───────────────┐                   │
  │ adjacency?    │                   │
  │ head select   │                   │
  │ rule table    │                   │
  │ slot resolve  │← context (read)   │
  │ read-back     │                   │
  └───────┬───────┘                   │
     safe │ unsafe                    │
          ↓    └───────────────┐      ↓
   Tier 1 encode               └→ Tier 2 encode
   (static model)                 (+ boost, hash_present=1)
          │                           │
          └──────────┬────────────────┘
                     ↓
            pick safe AND smaller
            (cross-language: prefer Tier 1 — §8.4, deferred)
                     ↓
        commit context FROM PAYLOAD
                     ↓
         native payload assembly
         (nests inside the existing
          Kotlin frame — §9.2)
```

## Appendix B — Decisions Register

| # | Decision | Status |
|---|---|---|
| 1 | Tiers distinguished by loss budget, not implementation | Locked |
| 2 | One arithmetic coder, two symbol sources | Locked |
| 3 | Integer arithmetic only, everywhere | Locked |
| 4 | Clause is the unit; tier per clause; packets streamed | Locked |
| 5 | Head selection by class precedence, ties by slot order | Locked |
| 6 | Concept-indexed rule table, `rule_priority`-sorted, first match wins | Locked |
| 7 | `head_implied` per intent | Locked |
| 8 | Slot modes INHERIT / REF / ID / LITERAL | Locked |
| 9 | TIME never inherits; NEGATION is not a slot | Locked |
| 10 | Literals never inherited, never stored in context | Locked |
| 11 | Read-back gates safety; size comparison gates selection | Locked |
| 12 | Low STT confidence disables Tier 1 | Locked |
| 13 | Tier 2: shared multilingual subword vocabulary | Locked |
| 13a | Byte fallback — tokenizer total, no OOV possible | Locked |
| 13b | Non-zero probability floor over the full vocabulary | Locked |
| 14 | Tier 2: static smoothed n-gram, no within-session adaptation | Locked |
| 15 | Tier 2 context boost from synchronised context only | Locked |
| 15a | **Boosted Tier 2 requires the context hash; unboosted is the recovery path** | Locked — corrects v1.3 |
| 15b | **Tier 1 uses a static model, no context boost** | Locked — new |
| 16 | Rules, classes, categories and thresholds are sender-side only | Locked |
| 17 | Commit context from the payload, never from the sentence | Locked |
| 18 | Tier 3 remains an interface stub | Locked |
| 19 | Cross-language: Tier 1 only (language spec §10) | Locked |
| 20 | Cross-language tier preference + operator toggle | **Deferred — §8.4** |
| 21 | `rule_priority` is rule evaluation order, distinct from message priority | Locked |
| 22 | Message priority is `NORMAL` or `CRITICAL` only | Locked |
| 23 | This layer produces the native payload; outer frame and UDP unchanged | Locked |
| 24 | `seq` fixed at 8 bits, not read from `caps()` | Locked |
| 25 | Common IR (romanised) | **Pending one benchmark — §13.1** |
| 26 | Constrained radio choice | **Outstanding — context spec §21.1** |
