#pragma once

// Adjacency check — tier §5.2. SENDER-ONLY. Runs before head selection.
//
//   NEUTRAL ──sent a QUERY intent──→ AWAITING_ANSWER
//   AWAITING_ANSWER ──any message sent, or timeout──→ NEUTRAL
//
//   pending = QUERY_CASUALTY_COUNT, heard = QUANTITY 3 → ANSWER_COUNT, QUANTITY = 3
//
// DECIDED in Phase 8 (tier spec implementation resolutions):
//
//   Whose query     The phone that must answer is the one awaiting: on_query()
//                   is called when a QUERY intent is exchanged that THIS phone's
//                   operator is expected to answer (the "heard" bare value is
//                   this phone's speech). Wiring it to received packets is the
//                   receiver / integration work (Phases 10–11).
//   Timeout         measured in caller-supplied ticks; the FSM never reads a
//                   clock. It is sender-only state, not context (context §13.4
//                   concerns context). The timeout length is a sender setting.
//   Bare value      no concept at all, not negated, and exactly one slot with a
//                   value (none Ambiguous); the answer table (tier1/rules.h)
//                   maps (pending query, that slot) to the answer intent.
//   States          the two drawn above. §5.2's text says "three states"; the
//                   diagram has two — recorded as a spec inconsistency.

#include "common/types.h"
#include "lang/extract.h"
#include "tier1/rules.h"

namespace itantra {

enum class AdjacencyState : u8 { Neutral, AwaitingAnswer };

class AdjacencyFsm {
public:
    // A QUERY intent this phone must answer. Deadline = now + timeout (saturating).
    void on_query(u16 query_intent, u64 now, u64 timeout) noexcept;

    // Any message sent by this phone.
    void on_message_sent() noexcept;

    // Times out once now >= deadline.
    void tick(u64 now) noexcept;

    AdjacencyState state() const noexcept { return state_; }
    u16            pending_query() const noexcept { return state_ == AdjacencyState::AwaitingAnswer ? pending_ : 0u; }

private:
    AdjacencyState state_    = AdjacencyState::Neutral;
    u16            pending_  = 0u;
    u64            deadline_ = 0u;
};

struct AdjacencyAnswer {
    bool answered = false;
    u16  intent   = 0u;
    u8   slot     = 0u;
    u16  value    = 0u;
};

// The answer a bare value in `clause` gives to the pending question, if any.
AdjacencyAnswer adjacency_answer(const AdjacencyFsm& fsm, const RuleTable& rules, const ClauseExtraction& clause);

}  // namespace itantra
