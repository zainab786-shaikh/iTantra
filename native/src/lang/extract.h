#pragma once

// Deterministic extraction — language-layer-spec §8; context §6, §8.
//
//   normalise (§6.1 1–3) → segment clauses (4) → per clause: normalise (5–6)
//   → match lexicon, number words, digit runs and negation words in ONE
//     longest-then-leftmost selection (§8.1, lang/lexicon.h)
//   → detect typed values with the pack's scanners (patterns.bin)
//   → assign slots from each concept's slot type (§7.1, context §8.1)
//
// Output is a CANDIDATE (context §6, §10), never a commit.
//
// Deliberately NOT here (sender-only, tier1/): intent identification — head
// selection and the rule table (tier §5.3–5.4); literal choice for a slot (tier
// §5.6–5.7); tier selection (Phase 9).
//
// Negation (Phase 8, tier §5.1 "negation flag per clause"): words from the
// pack's negations.bin are matched like lexicon entries (same selection, lowest
// precedence on an exact tie). A negation word is neither a concept nor
// unmatched text; its original bytes are recorded in `negations`. Negation is
// never context (context §2.3) — nothing here writes it anywhere else.
//
// Ambiguity (§8.2, context §8.2, L5): "Resolved using the detected intent,
// since each intent declares which slots it expects. If ambiguity remains, the
// affected slot is left unchanged." assign_slots() takes the intent's
// expected-slot mask once Tier 1 knows it; with every slot allowed it runs at
// extraction time. A slot with two different candidate values — or claimed by
// a surface form that maps to concepts in different slots — is Ambiguous and
// carries no value. It is never guessed.
//
// Unmatched text (§8.3): every token no match or scanner consumed is kept,
// with the exact bytes it came from in the original input — so a literal
// (tier §5.7) is the speaker's text byte for byte, in any script (C-24).

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"
#include "lang/clause.h"
#include "lang/normalize.h"
#include "lang/pack.h"

namespace itantra {

enum class SlotState : u8 {
    None,        // nothing for this slot
    Value,       // exactly one candidate value
    Ambiguous,   // conflicting candidates: left unchanged (§8.2)
};

struct SlotCandidate {
    SlotState state = SlotState::None;
    u16       value = 0u;   // Value only: a concept ID, or a scanned value
};

struct ConceptMatch {
    u32          group;          // one surface-form match; homographs share a group
    u16          concept_id;
    u8           slot;           // from concepts.bin, or kNoSlot
    ConceptClass concept_class;  // sender-only (tier §3.4)
    u32          categories;     // sender-only
    FormClass    form_class;
    Origin       origin;
    u32          begin;          // bytes in ClauseExtraction::match_text
    u32          end;
    SourceSpan   source;         // bytes in the original input
};

struct TypedValue {
    u8         slot;
    u32        value;
    SourceSpan source;
};

struct TextToken {
    std::string text;            // normalised (match_text bytes)
    u32         begin;
    u32         end;
    SourceSpan  source;          // exact original bytes
};

struct ClauseExtraction {
    SourceSpan                source;       // the clause in the original input
    std::string               clause_text;  // §6.1 steps 1–3
    std::string               match_text;   // §6.1 steps 5–6
    std::vector<ConceptMatch> concepts;     // text order
    std::vector<TypedValue>   values;
    std::vector<TextToken>    unmatched;
    std::vector<SourceSpan>   negations;    // negation words, original bytes, text order
    SlotCandidate             slots[kConceptSlotCount];
    u8                        ambiguous_slots = 0u;   // bit per SlotId
    bool                      unrepresentable_value = false;   // a scanner matched out of range

    // §8.3 "Nothing matches at all → Tier 2".
    bool nothing_matched() const noexcept { return concepts.empty() && values.empty(); }

    bool negated() const noexcept { return !negations.empty(); }
};

struct UtteranceExtraction {
    MappedText                    normalized;   // §6.1 steps 1–3 of the whole utterance
    std::vector<ClauseExtraction> clauses;
};

void extract_utterance(const LanguagePack& pack, const CommonPack& common, const u8* input, std::size_t length,
                       UtteranceExtraction& out);

// Recomputes slots and ambiguous_slots, considering only slots in
// `expected_slots` (kConceptSlotMask = all).
void assign_slots(ClauseExtraction& clause, u8 expected_slots);

}  // namespace itantra
