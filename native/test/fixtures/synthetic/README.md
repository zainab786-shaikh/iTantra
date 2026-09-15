# Synthetic language fixture

A **test artifact**, not a shipped language pack (implementation plan Phase 6; handoff "LANGUAGE DATA — SYNTHETIC FIXTURES FIRST").

It exists so that every code path of the language layer can be exercised before the real corpus and codebook exist:
- native words and English loanwords
- code-mixing
- inflected forms
- number words, native digits and ASCII digits
- time and quantity scanners
- literals and unknown words
- a homograph
- NFC/NFD variants

Real packs replace it with **no code change**. If a code change is needed, the interface is wrong.

| | |
|---|---|
| Languages | Hindi (`hi`, Devanagari), Tamil (`ta`, Tamil), English (`en`, Latin) |
| Concepts | 34, covering all seven concept slot types plus EVENT and ACTION heads |
| Intents | 7 |

The vocabulary is illustrative. It has **not** had the native-speaker review that real packs require (language spec §14.3). Nothing here is a codebook decision: concept and intent IDs are fixture IDs only.

## Layout

```
common/        shared data, compiled to concepts.bin, intents.bin, schema_version
lang/<code>/   per language, compiled to meta.json (with read-back bars), normalize.json and six .bin files
               (negations.bin added in Phase 8)
corpus/        test utterances and expected results (read by the conformance tests, never compiled);
               tier1.tsv is the Tier 1 corpus: expected tier, intent and priority, gate reasons (Phase 8)
tier2/         Tier 2 vocabulary pieces, training texts and boost magnitude (Phase 7),
               compiled to tier2/subwords.bin, ngram.bin, boost.bin
sender/        SENDER-ONLY Tier 1 rule table and adjacency answers (Phase 8), compiled by the rule
               compiler to sender/rules.bin; a malformed table fails the build
```

The source formats are documented in `native/tools/packc.cpp`. The build compiles these sources into `<build>/fixtures/synthetic/` with `itantra-packc`.

The Tier 2 tables are trained on `tier2/train.tsv` and the lexicon surfaces — **not** on `corpus/`, so C-05 and C-06 test on text the tables have not seen. The training texts use Devanagari, Tamil and Latin only, so every other script is absent from all training data (C-06). The vocabulary, counts and boost magnitude are fixture values, not decisions (tier §13.2).
