#pragma once

// Head selection — tier §5.3, register #5, T8. SENDER-ONLY.
//
//   ACTION  >  EVENT  >  STATE  >  ENTITY  >  MODIFIER
//
//   "Highest class present wins; ties break by slot enum order."
//   "No head found → INTENT_NONE. Two concepts of the top class → INTENT_NONE."
//
// DECIDED in Phase 8 — the two sentences above are reconciled as follows
// (tier spec implementation resolutions):
//
//   - Predicate classes (ACTION, EVENT, STATE): exactly one distinct concept of
//     the top class, or INTENT_NONE. "Two predicates in one clause means
//     segmentation failed. Reject rather than pick one."
//   - ENTITY and MODIFIER (a clause with no predicate): ties break by slot enum
//     order — the concept in the lowest SlotId wins; two distinct concepts in
//     that same lowest slot cannot be broken, so INTENT_NONE.
//   - The same concept matched twice is one concept.
//   - A head whose surface form is a homograph (the same match also offers
//     another concept, language §8.2) is ambiguous: INTENT_NONE. Never guessed.

#include "common/types.h"
#include "lang/extract.h"
#include "lang/pack.h"

namespace itantra {

enum class HeadStatus : u8 {
    Found,
    NoHead,        // no concept at all
    TwoTopClass,   // two distinct concepts of the top class (or of its lowest slot)
    Ambiguous,     // the head's surface form also offers another concept
};

struct HeadSelection {
    HeadStatus   status        = HeadStatus::NoHead;
    u16          concept_id    = 0u;
    ConceptClass concept_class = ConceptClass::Action;
    u8           slot          = kNoSlot;
};

HeadSelection select_head(const ClauseExtraction& clause);

}  // namespace itantra
