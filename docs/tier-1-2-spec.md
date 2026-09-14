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

- §6 — **Tier 2, pinned in Phase 7** (`native/src/tier2/`). Tested by `unit.tier2` and `conformance.c05_c06`. Each item below is one of two kinds:
  - **Protocol (frozen).** Shared by both phones under tokenizer version 1 and table version 1. Changing one is a deliberate version bump.
  - **Fixture-only / provisional.** A build-time choice made for the synthetic fixture tables. Not part of the wire contract. Must not be carried into production tables without a decision of its own.

  **Protocol (frozen)**
  - **Input semantics (§6.1).** Tier 2 codes a clause's **original input bytes**, never a normalised form. The language layer's §6.1 steps are not reversible (NFC, whitespace collapse, digit unification, case folding, punctuation stripping), and Tier 2 must return the sentence exactly (§1.1, contract C-05). Where the clause boundaries are cut is a separate, provisional convention (below).
  - **Tokenizer (§6.2, T3, T3a).** Token IDs 0–255 are the 256 byte values; IDs from 256 are subwords of 2–64 bytes of complete UTF-8. Rule: at each position the longest subword, otherwise one byte token. Total and lossless by construction.
  - **`symbol_count` (packet §3.5).** For Tier 2 it is the **token count**: exactly one coded symbol per token, no escapes, no end marker. Any clause of at most 2078 bytes always encodes. More than 2078 tokens returns `ASM_TOO_LONG` with no payload; what the sender does then was decided in Phase 9 (§8 block below).
  - **Frequency formula and floor (§6.3, T3b).** Interpolated Kneser-Ney structure over one preceding token, with history BOS at every clause start: `freq(s | h) = 1 + floor(lambda_h * uni[s] / 2^16) + w_h[s] + boost[s]`, integers only. The `1` is the uniform floor over the whole vocabulary and does not depend on training. Load bounds, checked when the tables load:
    - `uni[]` sums to exactly 2^16
    - each history's `lambda + sum(w) <= 2^23`, and the default lambda `<= 2^23`
    - V <= 2^16
    - boost mass <= 2^23 − 2^16

    Together they keep every total within the coder's 2^24. PPM was not chosen, because its escape symbols would make `symbol_count` exceed the token count.
  - **Boost key and gating (§6.4, §6.5, T5).** Reads only `current` of slots ACTOR … STATE in the pre-message context — the state the context hash covers — never `recent[]`, `age`, `seq`, `context_id` or `LAST_REF`. Keyed by (slot, value), because QUANTITY and TIME hold numbers, not concept IDs. One fixed magnitude per table; each token in the union of the slots' subword lists gets `+magnitude` once. Applied **if and only if `hash_present = 1`**:
    - The encoder writes `wire_context_hash(context_hash(ctx))` of the same context it boosted from.
    - The decoder refuses to decode — no partial output — unless its own pre-message wire hash matches.
    - `hash_present = 0` never reads the context.
  - **Reset (§6.6, T4).** Tables are immutable once loaded; every clause starts at BOS; no state crosses a clause boundary.
  - **Table formats and versioning.** `tier2/subwords.bin` (tokenizer version 1), `ngram.bin` (table version 1) and `boost.bin` (table version 1) are `lang/pack.h` containers of kinds 8, 9 and 10, one set for all languages. Byte layouts are as documented in `tier2/subword.h`, `tier2/ngram.h` and `tier2/boost.h`. Loaders reject any other version, any mismatch in vocabulary size, and anything outside the load bounds.
  - **Language.** The sender's `LangId` is carried unchanged, and the decoded text is the sender's exact bytes; nothing is translated. Which 4-bit value names which language is still open (packet spec).

  **Fixture-only / provisional — not protocol**
  - **n-gram order 2.** Order is a measured value (§13.2). Table version 1 accepts only order 2; order 3 would be a new table version, not a packet format change.
  - **Kneser-Ney training choices** (`estimate_kneser_ney`): the discount 3/4; `uni[]` from continuation counts scaled to 2^16 by largest remainder; each seen history filled to the full 2^23 mass; default lambda 2^23 for an unseen history. These decide table contents, not the formula, the bounds or the format.
  - **Boost-entry construction** (`native/tools/packc.cpp`): for each concept with a slot, the subword tokens (ID >= 256) of all its NFC lexicon surfaces in every compiled language, keyed (slot, concept ID).
  - **Boost magnitude 65536** (`tier2/params.tsv`). A measured value (§13.2).
  - **Vocabulary and training data.** The fixture vocabulary is `tier2/subwords.tsv` plus every lexicon surface and word and every training word. The training texts are `tier2/train.tsv` and the lexicon surfaces. How production vocabulary and training data are chosen is not decided.
  - **Clause input cut** (`tier2_clause_inputs`). A clause's input runs from its start in the utterance (0 for the first clause) to the next clause's start (the end of input for the last). Separators therefore stay with the clause before them, and the clauses concatenate back to the utterance byte for byte. This is a sender-side convention, not wire: the receiver never sees the cut, and every cut yields a valid payload. It stays provisional until the Phase 9 sender pipeline adopts it. The original-byte semantics above remain frozen.
  - **Test language IDs** 1 / 2 / 3 in `native/test/tier2_fixture.h`. Test-only; not a mapping.

  **Resolved in Phase 9 (was open here):** a clause longer than 2078 tokens, where packet §5 ("caller must split"), packet §7.5 ("the tier layer must not split a clause further") and §6.7 ("Tier 2 always succeeds") cannot all hold. See "Clause longer than 2078 Tier 2 tokens" in the §8 block below. The Phase 7 encoder is unchanged: it still returns `TooLong` with no payload and splits nothing.

- §5 — **Tier 1, pinned in Phase 8** (`native/src/tier1/`). Tested by `unit.tier1`, `conformance.c07`, `rulec.*` and `conformance.tier_vectors`. Three kinds of item: **protocol (frozen)** — both phones, Tier 1 table version 1, pinned by `native/test/golden/tier_vectors.bin`; **sender-only** — the sender may change these without breaking compatibility (§3.4, context §13.2); **fixture-only / provisional** — values chosen for the synthetic fixture or pending measurement.

  **Protocol (frozen)**
  - **Frame (§5.5–5.7, §5.9).** An intent plus one mode per concept slot ACTOR … STATE: `Absent 0 · Inherit 1 · Ref 2 · Id 3 · Literal 4`. NEGATION is the packet's negation bit, never a slot; LAST_REF is never in a frame.
  - **Symbol order (§5.9).** Intent (index among intents.bin IDs) → one mode per slot the intent *expects*, in SlotId order → for each Id slot in order, a concept index among that slot's concepts or ESCAPE followed by a number (bit-length class 1 … 16, then the bits below the leading 1) → for each Literal slot in order, a token count 1 … 255 then that many Tier 2 subword tokens. A value's kind is not transmitted: it is a Concept iff concepts.bin has that ID in that slot, otherwise a Number; a Number that collides with such an ID is refused by the sender and rejected by the decoder.
  - **Static models, table version 1 (§5.9, T4a).** No context boost anywhere in Tier 1. Intent uniform; mode `3·4·1·4·1` with Absent = 0 on a required slot and Inherit = Ref = 0 on TIME; concepts uniform plus ESCAPE 1; number class `17 − c`; bits 1·1; literal length uniform over 1 … 255. Literal tokens use the Tier 2 static n-gram table **unboosted**, with history restarting at BOS at every literal — no Tier 2 boost, history or metadata. The frequencies are provisional (below); changing them is a new Tier 1 table version.
  - **TIME (§5.6, #9).** Inherit and Ref on TIME have probability 0, are refused by the frame encoder and by `commit()`, and cannot be decoded.
  - **hash_present.** Set exactly when the frame has an Inherit or Ref slot, with the wire hash of the sender's pre-message context (context §17.1). The decoder rejects a payload where the two disagree.
  - **Ref.** Decodes, commits (`SlotOp::Ref`, not a write) and resolves exactly like Inherit: to `context.current` (receiver §4 "previous message's value"). Provisional as a meaning — see Open.
  - **Negation.** The wire negation bit is the clause's negation flag (§5.1). A negated clause may use Tier 1 **only** through a rule with a NEG_SET condition, so the negated meaning lives in the intent (§5.4). A negation copy disagreement is rejected, never decoded (receiver §3④, C-30).
  - **Priority.** `CRITICAL = intents.bin is_alert OR manual override`; never lowered (language §11.2, packet §11). CRITICAL uses the pack's `readback_critical` bar.
  - **Receiver.** The static model decodes whatever the context state (§5.9). On a hash mismatch, Id and Literal slots resolve and Inherit / Ref slots are unresolved; rendering refuses while any slot is unresolved (receiver §6.1, §7.1). Tier 1 renders with the listener's own templates (language §10.1).
  - **LangId (packet §3.2).** `0` unassigned · `1 … 10` = hi, gu, mr, kn, ml, ta, te, or, bn, en · `11 … 15` reserved; append-only (`native/src/lang/languages.h`).

  **Sender-only**
  - **Head selection (§5.3).** Reconciles "ties break by slot enum order" with "two concepts of the top class → INTENT_NONE": for ACTION / EVENT / STATE exactly one distinct concept of the top class, else INTENT_NONE; for ENTITY / MODIFIER the lowest SlotId wins, and two distinct concepts in that slot are INTENT_NONE. The same concept twice is one concept. A head whose surface form is a homograph is INTENT_NONE.
  - **Rules (§5.4, §12.1).** `sender/rules.bin` (container kind 12). Conditions read the clause's own extracted slots, never context; a condition reading an Ambiguous slot stops with INTENT_NONE. The operand is u16 as in §5.4's struct, so rule categories use bits 0 … 15. The rule compiler (`native/src/tier1/rulec/`, linked only into host tools) fails the build on each §12.1 item 1–4 violation, and also when a `head_implied = 0` intent does not expect its head's slot, a `head_implied = 1` intent requires it, or an answer intent does not expect its slot. Item 5 is the coverage report written by `conformance.c07`.
  - **Slot resolution (§5.5–5.7).** Implied head → Absent; a non-implied head → Id, never inherited. A value said: TIME → Id; equal to `context.current` and fresh → Inherit; otherwise Id. A required slot not said: when it is the only one and the clause's unmatched words form one contiguous run → Literal of their exact original bytes (what was said wins over memory); else fresh non-TIME context → Inherit; else INTENT_NONE. The Phase 8 sender never emits Ref and never writes LAST_REF. More than 255 literal tokens → INTENT_NONE.
  - **Staleness (context §13).** Fresh = `age <= max_inherit_age`, a sender setting; `allow_inheritance = false` gives a fully explicit message (context §16.1, §18.3).
  - **Read-back (§5.8, T9).** Render with the sender's templates, inherited values resolved from the sender's context; normalise both sides with language §6.1 steps 1–3, 5, 6; Levenshtein over U+0020 tokens; similarity `floor((L − d) · 1000 / L)`, 1000 for two empty sequences. Tier 1 is permitted only if **all** of these hold (fixed before commit, after an independent review showed word-level similarity alone accepted `"Never send water"` as `"Send water"` and a transmitted-but-unrendered quantity):
    - **R2 — rendered slots:** every non-Absent slot of the frame is a placeholder of the sender's template for the intent (`ReadbackUnrenderedSlot`). The pack compiler requires every language's template for an intent to reference only required slots, and the same slots in every language (`rulec::check_templates`, run by `itantra-packc`), so a verified value is rendered in every listener's language.
    - **R1 — coverage:** every spoken token appears among the rendered tokens, counted as a multiset (`ReadbackUnexplainedWord`). Tokens of the unmatched words the literal was taken from are exempt. An unlisted negation word, a dropped quantity or any other unexplained spoken word refuses Tier 1, whatever the similarity.
    - **Bar:** `similarity >= bar` remains as an additional guard (`ReadbackBelowBar`).

    Before sending, the sender decodes its own payload and requires the same frame, negation and priority. These are sender-only checks: no wire, frame, model or golden-vector change.
  - **Adjacency (§5.2).** Two states, as drawn. `on_query()` marks a QUERY this phone must answer; any message sent, or a caller-supplied tick reaching the deadline, returns to NEUTRAL (the FSM reads no clock). A bare value is a clause with no concept, no negation and exactly one slot value; `sender/rules.bin`'s answer table maps (pending query, slot) to the answer intent.

  **Fixture-only / provisional**
  - The Tier 1 static-model frequencies above (table version 1) — to be replaced by trained tables under a new version (§13.2).
  - The default staleness limit, 254 messages (§13.2's starting heuristic, with a saturated age counting as stale).
  - The literal rule "all unmatched words, only if contiguous": function words next to a name are included (see Open).
  - Fixture data: 7 intents added (CANCEL_REQUEST, REPORT_FLOOD_AT, REPORT_COLLAPSE_AT, REQUEST_STOP, REQUEST_EVACUATE_AT, QUERY_CASUALTY_COUNT, REQUEST_GENERIC), `head_implied = 1` on the ACTION-headed intents and on REPORT_CASUALTY_COUNT (its templates say "injured" as text, so under R2 the STATE head must not be transmitted), concept `query_count`, `sender/rules.tsv` and `answers.tsv`, negation words, the read-back bars (600/800; ta 550/750) and templates.

  **Recorded spec inconsistencies**
  - §5.4's example gives REQUEST_MEDICAL_AT and REQUEST_SUPPLY_AT the same `rule_priority 100` in one bucket, which §12.1 item 1 forbids. The build check wins; the fixture uses 100 and 90, and `rulec.rejects.duplicate-priority` compiles the example as written and rejects it.
  - §5.2 says "three states" but draws two; two are implemented.

  **Open**
  - **Literal span and mixed-language output — open Phase 9 decision.** A literal is all the clause's unmatched words, provided they are contiguous. Words around a name travel inside it, in the sender's language: "Tell Ravi to move" reaches a Hindi listener as "Tell Ravi to हटो", and "Tell Ravi never to move" as "Tell Ravi never to हटो". R1 does not catch this, because nothing is dropped. What stays fixed:
    - No script or language field is added to the wire; a literal is raw bytes (the frame above), and `tier_vectors.bin` is unchanged.
    - The receiver already knows the sender's language from HELLO (context §18.1). Marking literal spans for TTS is output-layer work: today `render_frame` returns one flat string.
    - Phase 9, which knows both languages, decides the cross-language policy (for example, refusing multi-word literals there). Mixed output itself is accepted (language §10.5); the long-term mitigation is provisioning (§15.2).
    - **Phase 9 did not decide it.** Phase 9 was instructed to preserve the cross-language behaviour already established, so selection applies no literal-span policy and still needs a decision.
  - **Read-back and alternative surface forms.** Templates render one form, so number words ("three" vs "3"), code-mixed loanwords ("north gate पर" vs "उत्तर द्वार पर") and STT variants leave a spoken word unexplained. Under R1 they are refused as `ReadbackUnexplainedWord`: safe, but it lowers the Tier 1 hit rate (see the fixture coverage report). Whether a concept-level comparison should be added is not decided (§5.8 specifies word level).
  - **Ref.** The Phase 8 sender never emits Ref. The golden vectors pin Ref's encoding, not its meaning, so **any future change to what Ref means requires a Tier 1 table / protocol version bump**. Its final semantics are otherwise not resolved in Phase 8 (receiver pipeline, Phase 10). Also open: how LAST_REF is chosen, and where pronoun words live — no pack data is specified.
  - **Which messages are fully explicit** (the periodic-explicit N, context §16.1) and the reset flag (context §13.4) — Phase 12.
  - **The STT confidence scale** is still open (language spec). The comparison is `confidence < threshold` → no Tier 1.
  - **Receiver / sender separation** is enforced at include level (a test scans the shared files). A receiver-only library is Phase 10–11.
  - **More than 2078 symbols** (packet §5 vs §7.5 vs §6.7) — resolved in Phase 9 (§8 block below). Tier 1 reports it as `TooLong` (Tier 1 not available).

- §8 — **Tier selection, pinned in Phase 9** (`native/src/select/`). Tested by `unit.select` and `conformance.c17_c19`.
  - **Scope:** every item here is **sender-only**.
  - **Unchanged:** the wire format, the frame, the models, the tables, the Tier 1 and Tier 2 encoders, and both golden artifacts (`vectors.bin`, `tier_vectors.bin`).

  **Sender-only**
  - **Order (§8).** Tier 1 is encoded first, and its safety is decided. Tier 2 is then encoded at the resulting priority. The two are compared only when Tier 1 is safe.
  - **Safety (§8.1).** Every `tier1_encode` outcome other than `Ok` sends Tier 2. Outcomes map to §8.1 rows as follows:
    - STT confidence: `LowConfidence`
    - no head: `NoHead`, `AmbiguousHead`
    - two top-class concepts: `TwoTopClass`
    - no rule: `NoRule`, `AmbiguousRuleSlot`, `NegationWithoutRule`
    - required slot missing: `RequiredSlotMissing`, `LiteralNotPlaceable`
    - read-back lost meaning: the four `Readback*` outcomes
    - negation copies disagree: `SelfCheckFailed`
    - sender refusals outside the table: `NegationAmbiguous`, `AmbiguousSlot`, `UnrepresentableValue`, `ValueKindCollision`, `LiteralTooLong`
    - Tier 1 unavailable: `TooLong`
    - Tier 1 invalid: `InvalidArgument`

    For an `Ok` encoding the selector also parses the Tier 1 payload's metadata:
    - Negation copies that disagree send Tier 2.
    - The following must all agree with the encoding, or the payload is invalid and Tier 2 is sent: tier, `symbol_count`, `seq`, negation (both the encoding's flag and the clause's), priority (`is_alert` OR override), `hash_present` (the frame uses context), and `context_hash` (the pre-message context).
  - **Size (§8.2, §13.1, C-19).**
    - **Packet size.** `native packet bytes = plaintext native payload bytes + kAeadTagBytes (4)`. The plaintext payload is exactly metadata + coder bits + 2 flush bits + zero padding to a byte. `kAeadOverheadBytes` (0 under the debug AEAD bypass) is never used.
    - **Rule (approved after the Phase 9 review).** Tier 1 is sent only if its packet is **strictly smaller**. An exact-size tie sends Tier 2: it is lossless and leaves a richer context (§8.3).
    - **Why coder bits alone are wrong.** Comparing coder bits ("payload size") alone can choose the larger packet, for two reasons:
      - Metadata differs by tier: Tier 1 is 19 bits and Tier 2 is 21. Each tier adds a 12-bit hash under its own condition, and more than 30 symbols adds 11 bits.
      - Byte padding differs.
    - `conformance.c17_c19` requires such cases on the fixture, in both directions, and checks every packet size against real sealing.
    - The Kotlin outer frame is the same for both tiers and is not counted.
  - **Tier 2 form.** The caller chooses between two forms (`SelectRequest::boost_tier2`); the selector does not encode both:
    - boosted: `hash_present = 1`, from the pre-message context
    - unboosted: the §6.5 recovery path
    - **Context-free requests are always unboosted (Phase 10 fix).** A request with `policy.allow_inheritance = false` is fully explicit: the context spec §16.1 periodic refresh and the §18.3 context-free fallback. It is sent unboosted whatever `boost_tier2` says (`tier2_boosted()`), so it carries no hash on either tier.
      - Without this, a Tier 2 refresh would be boosted, and a receiver whose context has drifted could not decode the message meant to resynchronise it.
      - A Tier 1 frame that relies on context is refused for such a request.
      - The reset flag (context §13.4) stays deferred.
      - Tests: `unit.select`, and `unit.receiver`, where refreshes decode under a mismatched receiver context.
  - **Priority (language §11.2, packet §11.1).**
    - **Tier 1 safe.** The message carries Tier 1's priority (`is_alert` OR manual override) **whichever tier is sent**. Choosing the smaller encoding never lowers a verified alert.
    - **Tier 1 not safe.** Tier 2 carries the manual override only, as packet §11.1 says ("the only path available to Tier 2 messages"). An alert intent whose Tier 1 failed a gate was not verified, so it does not raise priority.
  - **Clause longer than 2078 Tier 2 tokens (packet §5 vs §7.5 vs §6.7) — resolved; approved after the Phase 9 review.**
    - **No splitting.** No layer splits the clause: one clause is one message (packet §7.5), so context commits stay ordered.
    - **Tier 1 safe.** Tier 1, if it is safe and fits, is sent as the only candidate.
    - **Tier 1 not safe.** Selection returns `ClauseTooLong`: no payload, no context update, and the counter is not consumed. The sender pipeline must tell the operator rather than send part of the clause. How the operator is told is Phase 12.
    - **Reading of packet §5.** "Caller must split" is read as the layer that forms clauses. Its rule is fixed (context §8), so there is no further cut to make.
    - **Reading of §6.7.** §6.7 is read as the coder's totality: every token has a finite code, within packet §5's size bound. A clause of at most 2078 bytes always has a Tier 2 payload.
    - A Tier 2 failure other than `TooLong` (broken tables) with Tier 1 unsafe returns `NoEncoding`.
    - The comments in `tier2/encode.h` and `packet/assemble.h` that said "caller must split" now point here. The change is comments only; no code changed.
  - **Context update (§11.2, §11.3).** Selection reports which update applies and commits nothing itself:
    - Tier 1: commit the frame's payload.
    - Tier 2, same language: both phones update from the text.
    - Tier 2, different languages: both skip the update.
  - **Cross-language.** Unchanged. §8.2's safe-and-smaller rule applies in every language pair; the §8.4 preference is still deferred. `unit.select` checks that every listener language gets the same choice and the same bytes.
  - **Clause input cut.** Still provisional. The selector takes the clause's Tier 2 bytes as a span; the tests use `tier2_clause_inputs`.

  **Open**
  - The cross-language literal-span policy (§5 block above), which Phase 9 did not decide.
  - When to send boosted rather than unboosted Tier 2, and whether to encode both and compare. The first message of a session, for example, pays 12 hash bits for an empty boost.
  - The §8.3 second-order effect (a larger Tier 2 buying better inheritance later) is not measured.

- §10, §11 — **Receive side and the Tier 2 commit, pinned in Phase 10.** Receiver detail is in the receiver spec's implementation resolutions. Tested by `unit.receiver` and `conformance.c31`.
  - **Tier 2 text commit (§11.2, §11.3; shared).** `native/src/tier2/commit.h` is one function used by both phones, over the same bytes and with the same extractor:
    - One message is one commit, whatever number of clauses the extractor finds.
    - A slot is written only when every clause that has a value there agrees on exactly one non-zero value and no clause is ambiguous there. Otherwise it is left absent; two values are never guessed between.
    - `LAST_REF` is never written.
    - It is used only when the sender's language equals the listener's. Otherwise both phones skip the update.
    - `unit.receiver` runs whole corpus conversations in all nine fixture language pairs through Phase 9 selection and the receiver; both contexts are equal after every message.
  - **Tier 1 receive (§10, §5.9).** The static model decodes whatever the context state. On a hash mismatch, only Inherit / Ref slots are unresolved, there is no text, and nothing is committed. Rendering uses the receiver's templates.
  - **Tier 2 receive.** A boosted payload with a mismatch is not decoded; an unboosted one always is.
  - **Template and form coverage (build-time, Phase 10).** `rulec::check_templates`, run by `itantra-packc`, previously skipped a language with no template for an intent and never checked forms. It now fails the build when:
    - any language lacks a template for any intent
    - a concept that can fill a named-form placeholder lacks that form in that language
    - a pack using "native" digits has no digit set

    A number in a named-form slot, or a concept in a digits slot, can still fail to render; the receiver reports that as `render_fail`.

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
