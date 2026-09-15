#pragma once

// Text normalisation — language-layer-spec §6.
//
// §6.1 required order, which this module implements exactly and in pieces so
// clause segmentation (step 4, lang/clause.h) sits where the spec puts it:
//
//   normalize_utterance()   1  Unicode NFC            (+ pack strip rules)
//                           2  digit unification      native digits → ASCII
//                           3  whitespace collapse
//   segment_clauses()       4  clause segmentation    punctuation still present
//   normalize_clause()      5  case folding           per clause from here
//                           6  punctuation stripping
//   (lexicon)               7  match
//
// "Stripping punctuation before step 4 destroys the boundaries the segmenter
// needs." normalize_utterance() never touches punctuation.
//
// Language-neutral code (spec L9): Unicode behaviour comes from the Unicode
// 16.0.0 tables (lang/unicode_tables.h); everything language-specific — which
// digits exist, what to strip, which punctuation and conjunctions end a
// clause — comes from the pack's normalize.json (NormalizeRules).
//
// Every output byte records the input bytes it came from, so a span of
// normalised text maps back to exactly what was spoken (literals, C-24).
//
// Pinned here (recorded in the spec's implementation resolutions):
//   - invalid UTF-8 bytes pass through unchanged, one unit each
//   - step 3: every White_Space run becomes one U+0020; leading and trailing
//     whitespace is removed
//   - step 5: Unicode simple case folding (CaseFolding.txt C + S)
//   - step 6: each punctuation codepoint (General_Category P*) becomes a space,
//     then step 3's collapse runs again — "gate,send" must not become one word

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/types.h"

namespace itantra {

// Input byte range [begin, end).
struct SourceSpan {
    u32 begin;
    u32 end;
};

struct MappedText {
    std::string             text;     // normalised UTF-8 (invalid input bytes carried as-is)
    std::vector<SourceSpan> source;   // one per byte of `text`

    // The input bytes that text[begin, end) came from. {0, 0} if empty.
    SourceSpan source_of(u32 begin, u32 end) const noexcept;
};

// ---------------------------------------------------------------------------
// Unicode primitives (tables: lang/unicode_tables.h)
// ---------------------------------------------------------------------------
namespace unicode {

u8   combining_class(u32 cp) noexcept;
bool is_white_space(u32 cp) noexcept;
bool is_punctuation(u32 cp) noexcept;
u32  simple_case_fold(u32 cp) noexcept;

// NFC of a UTF-8 string (invalid bytes carried through).
std::string nfc(const std::string& utf8);

// NFD (full canonical decomposition, canonical order). Not used by the
// pipeline; tests build the "other" normalisation form of a word with it (C-27).
std::string nfd(const std::string& utf8);

}  // namespace unicode

// ---------------------------------------------------------------------------
// Pack rules — normalize.json
// ---------------------------------------------------------------------------
//
//   "digit_sets"           array of strings, each exactly ten codepoints for the
//                          digits 0 … 9 in order (e.g. U+0966 … U+096F)
//   "strip_codepoints"     array of single-codepoint strings removed after NFC
//                          (e.g. U+200D ZERO WIDTH JOINER, if a pack wants it)
//   "clause_punctuation"   array of single-codepoint strings that end a clause
//                          (e.g. ".", ",", U+0964 DEVANAGARI DANDA)
//   "clause_conjunctions"  array of whole tokens that start a new clause
//                          (e.g. "and")
//
// (Source code in lang/ deliberately contains no script characters: every
// language-specific string lives in pack data, spec L9.)
//
// All four keys are required (empty arrays allowed); unknown keys are
// rejected, so a typo cannot silently disable a rule.

struct NormalizeRules {
    std::vector<u32>                primary_digits; // the first digit set, 0 … 9 (empty if none)
    std::vector<std::pair<u32, u8>> digits;         // sorted by codepoint
    std::vector<u32>                strip;          // sorted
    std::vector<u32>                clause_punctuation;   // sorted
    std::vector<std::string>        clause_conjunctions;  // NFC, case-folded
};

bool parse_normalize_rules(const u8* json, std::size_t length, NormalizeRules& out, std::string& error);

// §6.1 steps 1–3 over the whole utterance.
MappedText normalize_utterance(const u8* input, std::size_t length, const NormalizeRules& rules);

// §6.1 steps 5–6 over utterance.text[begin, end). Source spans still refer to
// the original input.
MappedText normalize_clause(const MappedText& utterance, u32 begin, u32 end);

// Steps 1–3, 5, 6 without segmentation: the form a lexicon surface, number
// word or pattern word is stored in, so it compares equal to matched text.
std::string normalize_surface(const u8* input, std::size_t length, const NormalizeRules& rules);

}  // namespace itantra
