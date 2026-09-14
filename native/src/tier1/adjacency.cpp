#include "tier1/adjacency.h"

namespace itantra {

void AdjacencyFsm::on_query(u16 query_intent, u64 now, u64 timeout) noexcept {
    state_    = AdjacencyState::AwaitingAnswer;
    pending_  = query_intent;
    deadline_ = (timeout > ~u64{0} - now) ? ~u64{0} : now + timeout;
}

void AdjacencyFsm::on_message_sent() noexcept {
    state_   = AdjacencyState::Neutral;
    pending_ = 0u;
}

void AdjacencyFsm::tick(u64 now) noexcept {
    if (state_ == AdjacencyState::AwaitingAnswer && now >= deadline_) on_message_sent();
}

AdjacencyAnswer adjacency_answer(const AdjacencyFsm& fsm, const RuleTable& rules, const ClauseExtraction& clause) {
    AdjacencyAnswer a;
    if (fsm.state() != AdjacencyState::AwaitingAnswer) return a;
    if (!clause.concepts.empty() || clause.negated() || clause.ambiguous_slots != 0u) return a;

    u32 values = 0u;
    u8  slot   = 0u;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (clause.slots[s].state == SlotState::Value) {
            ++values;
            slot = static_cast<u8>(s);
        }
    }
    if (values != 1u) return a;

    const AnswerRule* rule = rules.answer(fsm.pending_query(), slot);
    if (rule == nullptr) return a;
    a.answered = true;
    a.intent   = rule->answer_intent;
    a.slot     = slot;
    a.value    = clause.slots[slot].value;
    return a;
}

}  // namespace itantra
