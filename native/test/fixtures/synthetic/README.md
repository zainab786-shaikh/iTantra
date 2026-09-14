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
lang/<code>/   per language, compiled to meta.json, normalize.json and the five .bin files
corpus/        test utterances and expected results (read by the conformance tests, never compiled)
```

The source formats are documented in `native/tools/packc.cpp`. The build compiles these sources into `<build>/fixtures/synthetic/` with `itantra-packc`.
