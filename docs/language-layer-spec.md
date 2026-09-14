# Language Layer — Component Specification

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Language Layer
**Version:** 1.3
**Status:** Final — ready for implementation
**Companions:** `context-manager-spec.md` v1.2 · `tier-1-2-spec.md` v1.5 · `packet-security-transport-spec.md` v1.2 · `receiver-pipeline-spec.md` v1.2
**Languages:** Hindi, Gujarati, Marathi, Kannada, Malayalam, Tamil, Telugu, Odia, Bengali, English

### Implementation resolutions — v1.3 (no version bump, no spec-body change)

Pinned when the language layer was implemented (implementation plan Phase 6, `native/src/lang/`). Items marked *pairing* change what both phones extract from the same Tier 2 text, so they are part of the pairing contract (§16 L1–L6).

- §6.2 / L2 — **Unicode version 16.0.0.** NFC, White_Space, simple case folding and punctuation come from tables generated out of the Unicode Character Database (`native/tools/ucd/gen_unicode_tables.py`; input hashes in `native/src/lang/unicode_tables.h`). NFC was verified against the full `NormalizationTest-16.0.0.txt`: 19,965 lines, plus every codepoint not listed. *pairing*
- §6.1 — pipeline details:
  - Invalid UTF-8 bytes pass through unchanged, one unit each.
  - Step 1 applies the pack's `strip_codepoints` after NFC.
  - Step 3 turns every White_Space run into one U+0020 and trims both ends.
  - Step 5 is Unicode simple case folding (CaseFolding status C + S).
  - Step 6 turns each General_Category P* codepoint into a space, then collapses again, so "gate,send" stays two words.
  - *pairing*
- §4.2 `normalize.json` — exactly four keys, all arrays of strings; unknown keys rejected. `digit_sets` holds ten-codepoint strings for digits 0 … 9 (the first set is also used to render native digits). `strip_codepoints` and `clause_punctuation` hold single codepoints. `clause_conjunctions` holds whole tokens. *pairing*
- §6.1 step 4 / context §7 — segmentation rule. A clause-punctuation codepoint ends a clause and stays with it. A token equal to a conjunction (case-folded) starts a new clause and stays with it. A piece with no content joins the clause before it, or the one after if it comes first. Clause segmentation is sender-only.
- §8.1 / L3 — selection is one total order over lexicon matches, number words and digit runs: longest in **codepoints** first, then leftmost, then lexicon before number. Besides the codepoint-boundary check (L4), a match must start and end on **token boundaries**, so a word never matches inside a longer word; inflected forms are listed whole (§14.1). *pairing*
- §6.4 / §8 typed values:
  - Digit runs of up to 9 ASCII digits (after step 2) are numbers; number words come from `numbers.bin`.
  - `patterns.bin` scanners are tried in file order at each position. The first that matches consumes its items and yields `value = a·n₁ + b·n₂ + c`, accepted only within `[min, max]`, with `min ≥ 1` and `max ≤ 65535`.
  - A value outside that range is flagged unrepresentable and never stored.
  - *pairing*
- §8.2 / L5 — ambiguity: a slot with two different candidate values, or claimed by one surface form whose concepts lie in different slots, is **Ambiguous** and carries no value. The intent's expected slots (§4.1) filter candidates once the intent is known. *pairing*
- §4.2 / Appendix B — file formats:
  - every `.bin` is one container: magic `ITLP`, version 1, kind, big-endian payload, CRC-32;
  - `lexicon.bin` and `numbers.bin` hold a sparse-transition Aho-Corasick automaton (§8.1), used in place;
  - `meta.json` has exactly `language`, `pack_version`, `script`, `tts_voice`, `stt_confidence_threshold` (integer, 0 … 65535);
  - pack language codes are ISO 639-1 for the ten languages.
- §9 / §14.2 — templates use `{SLOT:form}`: a named form for a concept, `digits` or `native` for a number; a literal is inserted exactly as spoken (§10.5). A missing slot, form or template fails the render; nothing is invented.
- **Resolved (Phase 8) — LangId, the 4-bit `language` value (packet §3.2):** `0` unassigned (names no language) · `1` hi · `2` gu · `3` mr · `4` kn · `5` ml · `6` ta · `7` te · `8` or · `9` bn · `10` en · `11 … 15` reserved. The order is this spec's language list. Append-only: a value is never reused or renumbered, and an unknown value decodes as "no language", never as a different one. Implemented in `native/src/lang/languages.h` (`lang_id_of`, `language_of_id`), the only file where language codes appear.
- **Resolved (Phase 8) — where negation words live:** `lang/<code>/negations.bin` (container kind 11), compiled from `negations.tsv`, one surface form per line, stored normalised like lexicon surfaces. Negation words take part in the one longest-then-leftmost selection (§8.1) with the lowest precedence on an exact tie. A negation word is neither a concept nor unmatched text: its original bytes are recorded per clause, and a clause is negated when it contains one (tier §5.1). The compiler refuses a negation word that is also a lexicon surface. Negation is never context.
- **Resolved (Phase 8) — read-back bars (tier §5.8, §11.1):** `meta.json` gains two required keys, `readback_normal` and `readback_critical`: integer per-mille similarity, 0 … 1000, with critical strictly above normal. Sender-only. The fixture values (600/800; ta 550/750) are not decisions.
- **Open:** the scale of `stt_confidence_threshold`.
- **Open:** conjunctions written as suffixes or clitics do not split clauses (whole tokens only).
- **Open:** a numeric value of 0 (e.g. zero casualties, midnight) cannot be stored, because 0 means empty in context Appendix B. The fixture's TIME scanner (hour × 60 = minutes) is fixture data, not a codebook decision.
- **Resolved (Phase 7):** Tier 2 codes each clause's **original input bytes**, not its normalised form. A clause's Tier 2 input runs from its start in the original utterance to the next clause's start, so the clauses concatenate back to the utterance exactly (tier spec implementation resolutions; `native/src/tier2/encode.h`).
- **Open:** the pack digest verified at HELLO (§18.1, L6), and platform mmap of packs (§4.4).

### Changes from v1.2

- §11.2 — new: priority terminology. `is_alert` sets message priority `CRITICAL`; `HIGH` does not exist.

### Changes from v1.1

- §10 — new: **cross-language operation** as a stated capability, with the per-tier rule and the deferred operator toggle.
- §17.3 — romanised IR narrowed to **compression only**; explicitly not a cross-language mechanism.

---

## 1. Purpose and Scope

Sits between speech recognition and the semantic frame. Converts recognised text in any of 10 languages into a **language-independent representation**, and converts that representation back into natural speech in whichever language the listener selected.

```
User's language setting → STT → [Common IR?] → Language Layer
                                                    ↓
                                         Language-independent frame
                                                    ↓
                                       Context Manager → Tier selection
```

### 1.1 In scope

Language selection · text normalisation · mapping words to language-neutral concept numbers · code-mixed speech · generating natural sentences in the listener's language.

### 1.2 Not in scope

- **Speech recognition.** Recognised text and a confidence value arrive as inputs.
- **Speech synthesis, playback, UI.** Decoded output is handed to the output layer.
- **Machine translation** — rejected, §17.1.
- **Automatic language identification** — rejected, §17.2.
- Tier selection and compression — see `tier-1-2-spec.md`.

---

## 2. Core Decision — No Canonical Language

**Each phone operates in its own language. Nothing is translated.**

Every language maps directly into shared **concept numbers** that carry no language at all.

```
Tamil speech  ──┐
Hindi speech  ──┼──→  concept numbers  ──→  any output language
Odia speech   ──┘
```

The frame is already the neutral representation. A canonical language would add translation cost to obtain something the design already has.

### 2.1 What travels on the wire

```
intent = 12,  LOCATION = 312,  SEVERITY = 3
```

Numbers only. No language is present in a Tier 1 message, and the receiver never needs to know what the sender spoke.

---

## 3. Language Selection

**A user setting. Never detected automatically.**

| | |
|---|---|
| Set by | The operator, in the app |
| Changed | Any time, including mid-session (§12) |
| Scope | Per phone. The two phones need not match. |
| Detection model | None |

### 3.1 Rationale

A language ID model is another model to load, another thing to be wrong, and another entry in the flash and RAM budget. The operator knows what language they speak.

Detection would also be unreliable here specifically: Hindi and Marathi share a script (§13), and operational speech is frequently code-mixed (§5).

### 3.2 The setting drives

- which language the shared multilingual STT decodes (§4.5)
- which language pack is loaded (§4)
- which templates render incoming frames
- which TTS voice speaks

---

## 4. Language Data Model

All language-specific material lives in a **language pack**. The extraction engine is one piece of C++ shared by every language.

### 4.1 Shared, language-neutral

```
common/
  concepts.bin        concept ID → slot type
  intents.bin         intent ID  → expected slots, head_implied, is_alert
  schema_version
```

`concepts.bin` contains **no language data whatsoever**. This is what makes the wire neutral.

### 4.2 Per language

```
lang/<code>/
  meta.json           language code, pack version, script, TTS voice ID,
                      STT confidence threshold (§11.1)
  lexicon.bin         surface forms → concept IDs   (match automaton)
  forms.bin           concept ID → output forms, per slot position
  templates.bin       intent ID  → sentence template
  numbers.bin         number words → numeric values
  patterns.bin        time / quantity scanners
  normalize.json      digit maps and script normalisation rules
```

### 4.3 Lexicon entry

```
surface_form   text as actually spoken, in that language's script or Latin
concept_id     uint16, points into concepts.bin
form_class     base | inflected variant
origin         native | loanword | stt_variant
```

`origin` lets code-mixing coverage be measured and lets native speakers review loanwords separately. `stt_variant` marks common misrecognitions added from prototype logs (§8.4).

### 4.4 Loading

A phone loads **only its own language pack**. Switching language swaps a lexicon and a template set — no speech model is reloaded (§4.5).

### 4.5 Speech models

STT and TTS are **two shared multilingual models**, not one pair per language. Both stay loaded; the selected language is a parameter passed to them.

| | Count | Loaded |
|---|---|---|
| STT | 1, multilingual | always |
| TTS | 1, multilingual | always |
| Language packs | 10 shipped | 1 at a time |

Consequences:

- **Flash:** two models instead of twenty. This is where the efficiency saving lives.
- **RAM:** constant, regardless of how many languages are supported.
- **Language switching:** free — no model load (§12).
- **Cross-language playback:** available for Tier 2 as well as Tier 1 (§10), because the TTS can speak any supported language on demand.

---

## 5. Code-Mixing — Requirement

**Every language pack must include the English domain terms its speakers actually use.**

A requirement, not an optimisation. Indian operational speech mixes languages constantly, and does so *more* under stress, because official and technical vocabulary is predominantly English.

### 5.1 Worked example

```
Spoken (Tamil operator):
  "north gate ல தீ, ambulance அனுப்புங்க"

Matched:
  "north gate"   → 312   (loanword entry in the Tamil pack)
  "தீ"           → 47    (native entry)
  "ambulance"    → 205   (loanword entry in the Tamil pack)
```

Resolved with no translation — both the Tamil word and the English word point at the same concept number.

### 5.2 Scope

Only the domain terms speakers naturally reach for. Expect 100–200 entries per language: ambulance, fire brigade, sector, block, gate, oxygen, evacuate, rescue, police, hospital, radio, generator, vehicle, casualty.

### 5.3 Sourcing

Ask native speakers explicitly:

> Which of these would you say in English even when speaking your own language?

Mark those `origin = loanword`. Their answers beat any externally-assembled list.

### 5.4 Verification

The prototype test set must include code-mixed sentences. A test set of pure single-language sentences will pass while the field fails.

---

## 6. Text Normalisation

Runs **before** any matching, on both phones.

### 6.1 Required order

Order matters. Punctuation must survive until after clause segmentation.

```
1. Unicode NFC
2. Digit unification        native digits → ASCII
3. Whitespace collapse
4. Clause segmentation      uses punctuation and conjunctions
   ── per clause from here ──
5. Case folding             affects Latin-script loanwords
6. Punctuation stripping
7. Match against lexicon
```

Stripping punctuation before step 4 destroys the boundaries the segmenter needs.

### 6.2 Unicode NFC

The same Indic word can be encoded more than one way. Two encodings that render identically will not compare equal in a lookup.

Mandatory on every input path, on both phones. Skipping it produces failures invisible on screen and expensive to diagnose.

### 6.3 Digits

Every supported script has its own digit forms, mixed freely with ASCII by speakers and recognisers.

```
Devanagari  ० १ २ ३ ४ ५ ६ ७ ८ ९
Tamil       ௦ ௧ ௨ ௩ ௪ ௫ ௬ ௭ ௮ ௯
Bengali     ০ ১ ২ ৩ ৪ ৫ ৬ ৭ ৮ ৯
```

Map all to ASCII before matching. Table lives in each pack's `normalize.json`.

### 6.4 Number words

Numbers are **values**, not concept IDs — they cannot be enumerated. Each pack carries a number-word table.

```
"three" → 3      "மூன்று" → 3      "तीन" → 3
```

---

## 7. The Concept Codebook

Keyed by **meaning**, not by word. One concept, one number, many surface forms.

```
ID 312
  slot type : LOCATION          ← stored once, on the concept
  Hindi     : उत्तर द्वार
  Tamil     : வடக்கு வாசல்
  Marathi   : उत्तर दरवाजा
  English   : north gate, north entrance
```

### 7.1 Slot type lives on the concept

Stored **once, on the concept number** — never per language. "North gate" cannot be a LOCATION in Hindi and something else in Tamil. Slot assignment at runtime is a table read, not a decision.

This is the property `context-manager-spec.md` §8.1 depends on.

### 7.2 Concept metadata used by Tier 1

`concepts.bin` also carries, for the sender's rule table:

```
concept_class    ACTION | EVENT | STATE | ENTITY | MODIFIER
category_mask    MEDICAL | FIRE | TRANSPORT | SUPPLY | …
```

Sender-only. The receiver never reads them (tier spec §3.4).

### 7.3 Matching direction

- **Input:** many surface forms → one number
- **Output:** one number → the form for the listener's language

### 7.4 IDs are a wire contract

Append only. **Never reuse or renumber.** A reused ID decodes into a confidently wrong message on a phone running an older pack.

Adding a *language* never changes an ID — it adds a column, not a row.

---

## 8. Input Path — Text to Frame

```
Normalised clause
      ↓
Match lexicon               single pass, all entries at once
      ↓
Detect typed values         numbers, times, quantities
      ↓
Identify intent             head concept + rule table (tier spec §5)
      ↓
Assign slots                read slot type from the concept
      ↓
Candidate frame
```

### 8.1 Matching rules

Single-pass multi-pattern automaton (Aho-Corasick). Two rules must be fixed, or implementations disagree:

- **Longest match wins.** "north gate" beats "gate".
- **Leftmost first** where matches of equal length overlap.

Match on NFC-normalised UTF-8 **bytes**, with a codepoint-boundary check on each reported match. Indic characters are 3 bytes, so a byte-level automaton can otherwise report a match slicing a character in half.

Use a compact representation (double-array trie or sparse transitions). Dense 256-way arrays per node will exceed the memory budget.

### 8.2 Ambiguity

Resolved using the detected intent, since each intent declares which slots it expects. If ambiguity remains, the affected slot is **left unchanged** and the message must not be forced into Tier 1.

Both phones run this same code over the same text on Tier 2 messages.

### 8.3 Unknown words

```
Unknown name or number   → frame + spelled-out literal
Nothing matches at all   → Tier 2, sentence sent as text
```

Nothing is lost on either path.

### 8.4 STT variants

Aho-Corasick is exact matching; STT output is not exact. One bad character costs the whole slot.

**Do not add fuzzy matching** — a distance threshold is another number that must behave identically on both phones. Instead, log every clause reaching `INTENT_NONE` during the prototype, inspect what STT actually produced, and add frequent misrecognitions as lexicon entries with `origin = stt_variant`.

That log is also the Tier 1 hit-rate diagnostic, so it comes free.

---

## 9. Output Path — Frame to Speech

```
Received frame
      ↓
Template for (intent, target language)
      ↓
Fill slots using forms.bin
      ↓
Sentence
      ↓
Output layer
```

Which language is "target" depends on the tier — see §10.

---

## 10. Cross-Language Operation

**A stated capability of the system.** A Marathi speaker can be heard in Gujarati, with no translation model anywhere on the device.

### 10.1 The per-tier rule

> **This differs by tier. Getting it wrong produces garbage, not a degraded result.**

| Tier | Renders in | Because |
|---|---|---|
| **Tier 1** | the **receiver's** language | the frame carries concept IDs, which have no language |
| **Tier 2** | the **sender's** language | the payload *is* the sender's text |
| **Tier 3** | the **sender's** language | same reason |

```
Sender set to Marathi    →    Receiver set to Gujarati

Tier 1:  "उत्तर दरवाजा जवळ आग"
              ↓
           12, 312              ← on the wire
              ↓
         "ઉત્તર દરવાજા પાસે આગ"      ← Gujarati templates

Tier 2:  full sentence crosses as Marathi text
              ↓
         spoken in Marathi, marked as untranslated
```

### 10.2 Why Tier 2 cannot cross

Tier 2 must be **lossless** — it is the safety floor. Translating it would:

- break the read-back check (the comparison would run against translated text, so a translation error passes undetected)
- add 200–500 ms per sentence in the critical path
- require two extra model directions
- **make Tier 2 lossy**, removing the floor the whole tier system rests on

That last point is structural, not a cost trade.

### 10.3 Honest capability statement

```
Structured operational messages   → listener's language
Free-form or unrecognised speech  → sender's language, spoken as heard
```

The second is accurate and is a feature statement, not an apology. Emergency communication *is* mostly structured messages, which is why the codebook approach works at all.

**The output layer must show which mode delivered**, so the operator always knows whether they are hearing their own language or the sender's.

### 10.4 Consequence: hit rate becomes a feature metric

```
before:  Tier 1 hit rate = how often we get the small encoding
after:   Tier 1 hit rate = how often cross-language actually works
```

A message falling to Tier 2 no longer just costs bytes — it arrives in a language the listener may not read. This is the argument for investing more in the intent list and codebook than compression numbers alone would justify.

### 10.5 Literals

Tier 1 literals carry raw text in the sender's language, so a Marathi place name arrives inside a Gujarati sentence. That is correct — names are not translated — but the TTS will read Marathi script with a Gujarati voice.

Mitigation is provisioning, not protocol: place names and team rosters belong in the deployment gazetteer (§15.2), so they are concept IDs rather than literals. Literals should be rare in practice.

### 10.6 Deferred — the operator toggle

Cross-language works today, by default. What is **deferred to after the core implementation**:

```
operator toggle:  language priority  →  prefer Tier 1 when safe,
                                        accept more bytes
                  bandwidth priority →  prefer whichever is smaller,
                                        accept the sender's language

UI indicator showing which mode delivered
tier-preference change in cross-language sessions (tier spec §8.4)
```

None of these block anything; all are additive. A *forced translation* option is **not** on this list — see §10.2.

---

## 11. Wrong Language Setting

If the phone is set to Hindi and the operator speaks Tamil, STT produces nonsense. With no detection model, nothing catches this automatically.

**Mitigation:** if mean STT confidence stays very low across a whole utterance, show:

> Check your language setting.

No model, negligible cost, and it converts a baffling failure into an obvious one. Local UI heuristic, no wire representation.

### 11.1 Thresholds are per language

One multilingual STT gives one consistent confidence scale, so this is a single calibration exercise rather than ten. But the model is still more confident in the languages it knows best — a value tuned on Hindi is too strict for Odia.

Store the threshold **per language, in `meta.json`**. Both consumers use it: the warning above, and the tier fallback rule (tier spec §5.8).

### 11.2 Priority terminology

`intents.bin` carries an `is_alert` flag per intent. It sets the message's priority to **`CRITICAL`**; every other message is **`NORMAL`**.

```
CRITICAL  =  intents.bin[intent].is_alert
             OR the operator's "Send as Critical" override
```

Two states only. **`HIGH` does not exist** — not in this layer, not on the wire, not in the UI. Full definition in `packet-security-transport-spec.md` §11.

Where the read-back threshold is described as higher for urgent messages (tier spec §5.8), the two values are **`NORMAL` and `CRITICAL`**.

This is unrelated to `rule_priority`, the Tier 1 rule table's evaluation-order field (tier spec §5.4).

---

## 12. Language Switching Mid-Session

**Context survives.** The Context Manager stores concept numbers, not words. Switching swaps the lexicon and templates only — no model reload, and the context table is untouched.

```
Before:  LOCATION = 312   ("उत्तर द्वार")
After:   LOCATION = 312   ("வடக்கு வாசல்")
```

No resynchronisation, no context reset, no model load, no message to the other phone.

---

## 13. Shared Scripts

Hindi and Marathi both use Devanagari. Script does not identify language — a further reason the user setting is correct.

**Requirement: separate lexicons per language, even where scripts are shared.** Vocabulary, word forms and templates differ; merging produces wrong matches in both.

---

## 14. Word Forms and Morphology

The hardest linguistic work, in both directions.

### 14.1 Input — recognising inflected words

Speakers do not use dictionary forms. Tamil says `வாசலில்`, not `வாசல்`.

**Decision: list the forms in the lexicon. Do not build a stemmer.**

```
ID 312, Tamil entries:
  வடக்கு வாசல் · வடக்கு வாசலில் · வடக்கு வாசலுக்கு · …
```

More entries, but the code stays simple and always produces the same answer. Flash cost negligible; algorithmic risk zero.

### 14.2 Output — generating correct forms

A template inserting the plain form produces sentences a native speaker hears as broken. Each concept stores the form required for each slot position:

```
ID 312, Hindi:
  plain     : उत्तर द्वार
  locative  : उत्तर द्वार पर

Template for intent 12:
  "{LOCATION:locative} आग"   →   "उत्तर द्वार पर आग"
```

### 14.3 Native-speaker review is mandatory

The single most likely place for output to sound wrong while everything else works — and *"high human legibility and flow"* is half of the 40% accuracy score.

Every language's templates and output forms must be reviewed by a native speaker. Budget the time explicitly.

---

## 15. Provisioning Split

### 15.1 Generic — baked in

```
Codebook · schema · common concepts
Generic locations: gate, building, road, river, hospital, bridge,
                   compass directions, relative positions
```

### 15.2 Deployment-specific — provisioned at pairing

```
Local place names · team roster · mission profile
```

Redeploying to a new district swaps this layer alone. This is also where §10.5's literal problem is solved.

---

## 16. Determinism Requirements

Both phones run this layer's code — the receiver runs extraction on Tier 2 text to maintain its context.

| # | Requirement |
|---|---|
| L1 | Normalisation is one code path, in the exact order of §6.1. |
| L2 | Unicode NFC on every input path without exception. |
| L3 | Longest-match, leftmost-first, identical on both sides (§8.1). |
| L4 | Codepoint-boundary check on every byte-level match (§8.1). |
| L5 | Ambiguity leaves the slot unchanged (§8.2) — never guesses. |
| L6 | Language packs versioned and verified at pairing. |
| L7 | No float arithmetic in matching or normalisation. |
| L8 | No neural model in this layer. |
| L9 | Zero language-specific code paths — differences live in data only. |

---

## 17. Rejected Alternatives

### 17.1 Canonical language (translate everything to one language)

**Rejected.** Three independent reasons.

**It breaks the read-back safety check.**

```
Tamil    : "வடக்கு வாசல்"   (north gate)
translate: "south gate"              ← translation error
extract  : LOCATION = south gate
read back: "south gate"
compare  : MATCH ✓                   ← error passes undetected
```

Direct mapping fails loudly — an unmatched word yields no frame and falls back. Translation fails **silently**, and the wrong message is announced confidently.

**It saves the easy half of the work.**

| Work item | Direct mapping | Canonical language |
|---|---|---|
| Input lexicon | 10 | 1 |
| Input inflected forms | 10 | 1 |
| **Output templates** | 10 | **10** |
| **Output word forms** | 10 | **10** |
| Translation models | 0 | 2 directions |

The receiver still hears its own language, so all output-side work remains — and that is the harder half.

**It costs on all three scored dimensions.** Accuracy: errors stack across STT and two translation hops. Efficiency: two extra model directions. Latency: 200–500 ms per sentence.

### 17.2 Automatic language identification

**Rejected.** Extra model, extra failure mode, and unreliable here — Hindi and Marathi share a script, and speech is routinely code-mixed.

### 17.3 Romanised IR as a cross-language mechanism

**Rejected for that purpose.** Romanisation changes the **script**, not the **language**:

```
Marathi:     वडाच्या झाडाजवळ
romanised:   vaḍācyā jhāḍājavaḷa
```

Still Marathi words. A Gujarati listener understands it no better in Latin letters.

**Romanised IR remains a live candidate for one thing only: Tier 2 compression** — a smaller alphabet gives denser n-gram statistics from limited data, and lets related Indo-Aryan languages share statistics. That is measured in the harness (tier spec §13.1), and adopting it requires span marking for code-mixed Latin text.

It is **not** a cross-language mechanism and must not be presented as one.

### 17.4 Stemming instead of listed forms

**Rejected for v1.** Listing inflected forms is larger in flash but carries no algorithmic risk. Revisit only if lexicon size becomes a measured problem.

### 17.5 Merging lexicons for shared scripts

**Rejected.** Hindi and Marathi share Devanagari but not vocabulary, word forms or templates.

### 17.6 Fuzzy matching for STT errors

**Rejected.** A distance threshold is another value that must behave identically on both phones. Add observed misrecognitions as lexicon entries instead (§8.4).

---

## Appendix A — Full Language Flow

```
                    SENDER
        User's language setting
                    ↓
          Multilingual STT (language = setting)
                    ↓
        ┌───────────┴────────────┐
        ↓                        ↓
  Normalise (§6.1)         text + confidence
        ↓                        │
  Segment clauses                │
        ↓                        │
  Match lexicon                  │
        ↓                        │
  Detect typed values            │
        ↓                        │
  Identify intent                │
        ↓                        │
  Language-independent frame     │
  (may be partial or empty)      │
        ↓                        ↓
        └──────────┬─────────────┘
                   ↓
         Context Manager (candidate)
                   ↓
             Tier selection
                   ↓
           Packet assembly
                   ↓
              Transmit
   ════════════════╪════════════════
                   ↓
                RECEIVER
                   ↓
             Decode by tier
                   ↓
   Tier 1 → frame → templates in the
            RECEIVER's language
   Tier 2 → text  in the SENDER's language
                   ↓
             Output layer
```

---

## Appendix B — Language Pack Layout

```
common/                      shared, no language content
  concepts.bin               concept ID → slot type, class, category
  intents.bin                intent ID  → slots, head_implied, is_alert
  schema_version

lang/hi/                     Hindi
  meta.json  lexicon.bin  forms.bin  templates.bin
  numbers.bin  patterns.bin  normalize.json

lang/ta/  …                  Tamil
lang/en/  …                  English
```

Ten packs ship. **One pack loads.**

---

## Appendix C — Language Decisions Register

| # | Decision | Status |
|---|---|---|
| 1 | Language is a user setting, never detected | Locked |
| 2 | Direct language → shared concept numbers; no canonical language | Locked |
| 3 | One shared C++ extraction engine | Locked |
| 4 | Separate language data per language | Locked |
| 5 | English domain loanwords in every language pack | Locked — requirement |
| 6 | Unicode NFC and digit normalisation before matching | Locked |
| 7 | Low STT confidence triggers a language-setting warning | Locked |
| 8 | Separate lexicons even for shared scripts | Locked |
| 9 | Context survives a mid-session language switch | Locked |
| 10 | Adding a language requires data, not code | Locked — verified by prototype |
| 11 | Two shared multilingual models — one STT, one TTS | Locked |
| 12 | No TTS voice provisioning; output language is a parameter | Locked |
| 13 | STT confidence thresholds stored per language | Locked |
| 14 | **Cross-language: Tier 1 renders in receiver's language; Tier 2/3 in sender's** | Locked |
| 15 | STT misrecognitions handled as lexicon entries, not fuzzy matching | Locked |
| 15a | `is_alert` sets message priority CRITICAL; HIGH does not exist | Locked |
| 16 | Operator language/bandwidth toggle | **Deferred — §10.6** |
| 17 | Romanised IR — compression only, not cross-language | **Pending one benchmark** |
