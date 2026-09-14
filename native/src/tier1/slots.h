#pragma once

// Slot resolution — tier §5.5, §5.6, §5.7; context §4.3, §13, §16, §18.3.
// SENDER-ONLY.
//
// For each slot the intent EXPECTS, in SlotId order (intents.bin masks):
//
//   the head's slot, head_implied       → Absent   (§5.5 "head not transmitted")
//   the head's slot, not head_implied   → Id(head) (§5.5 "head sent in its slot";
//                                          always explicit, so the predicate stays
//                                          readable under a hash mismatch, §5.9)
//   value said, TIME                    → Id       (TIME never inherits, #9)
//   value said, equal to context.current and fresh
//                                       → Inherit  (§5.6 "unchanged and fresh")
//   value said, otherwise               → Id       (§5.6 "changed since last message")
//   not said, optional                  → Absent
//   not said, required:
//     exactly one such slot, and the clause's unmatched words form one
//     contiguous run                    → Literal (their exact original bytes, §5.7)
//     otherwise context.current is set, fresh, and not TIME
//                                       → Inherit
//     otherwise                         → RequiredSlotMissing
//                                          (§5.6 "Required slot missing, no context
//                                           → INTENT_NONE → Tier 2")
//
// The sender never emits Ref (§5.6's decision list has no Ref branch; see
// tier1/frame.h). LAST_REF is never written by Tier 1.
//
// Staleness is sender-only (context §13.1): a context value is fresh when its
// age <= policy.max_inherit_age. No wire representation, no shared threshold
// (§13.2); INHERIT does not reset age (§4.3), so a repeatedly inherited value
// goes stale and is then sent explicitly.
//
// DECIDED in Phase 8, PROVISIONAL (tier spec implementation resolutions):
//   - the default freshness limit is 254 messages — §13.2's starting heuristic
//     ("send explicitly if the value changed or if it is a never-inherit
//     field") plus a saturated age (255, context Phase 5) counting as stale;
//   - allow_inheritance = false gives a fully explicit message (context §16.1
//     periodic explicit, §18.3 context-free fallback); which messages use it is
//     the synchronisation policy's decision (Phase 12);
//   - an unknown word takes precedence over inheritance for a missing required
//     slot (what was said wins over memory);
//   - which unmatched words form the literal: all of them, only when they are
//     one contiguous run. Function words next to a name are included; the
//     read-back check then judges the result.

#include <cstddef>

#include "common/types.h"
#include "context/context.h"
#include "lang/extract.h"
#include "lang/pack.h"
#include "tier1/frame.h"
#include "tier1/head.h"
#include "tier2/subword.h"

namespace itantra {

struct SenderPolicy {
    u8   max_inherit_age   = static_cast<u8>(kAgeMax - 1u);
    bool allow_inheritance = true;
};

enum class SlotOutcome : u8 {
    Ok,
    AmbiguousSlot,          // an expected slot stays Ambiguous under the intent (language §8.2)
    UnrepresentableValue,   // a scanner matched a value it cannot store
    ValueKindCollision,     // a number equal to a concept ID of the same slot
    RequiredSlotMissing,
    LiteralNotPlaceable,    // unknown words that are not one contiguous run
    LiteralTooLong,         // more than kMaxLiteralTokens subword tokens
};

// The original-byte range of the clause's unmatched words when they form one
// contiguous run — the only span a literal is ever taken from (and the span the
// read-back check exempts, tier1/readback.h R1). False when there are none or
// they are not contiguous.
bool literal_source_span(const ClauseExtraction& clause, SourceSpan& span) noexcept;

// Builds `frame` for `intent`. `head` is null for an adjacency answer (no head).
// `input` is the original utterance the clause's source spans index.
SlotOutcome resolve_slots(const CommonPack& common, const SubwordVocabulary& vocabulary, const IntentInfo& intent,
                          const HeadSelection* head, const ClauseExtraction& clause, const u8* input,
                          std::size_t input_length, const Context& context, const SenderPolicy& policy,
                          Tier1Frame& frame);

}  // namespace itantra
