#include "tier2/commit.h"

namespace itantra {

CommitPayload tier2_text_commit(const UtteranceExtraction& extraction, u8 seq) noexcept {
    CommitPayload payload{};
    payload.seq = seq;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        bool conflict = false;
        u16  value    = 0u;
        for (const ClauseExtraction& clause : extraction.clauses) {
            const SlotCandidate& candidate = clause.slots[s];
            if (candidate.state == SlotState::Ambiguous) {
                conflict = true;
            } else if (candidate.state == SlotState::Value) {
                if (candidate.value == 0u || (value != 0u && value != candidate.value)) {
                    conflict = true;
                } else {
                    value = candidate.value;
                }
            }
        }
        if (!conflict && value != 0u) {
            payload.slots[s].op    = SlotOp::Write;
            payload.slots[s].value = value;
        }
    }
    return payload;
}

CommitPayload tier2_text_commit(const LanguagePack& pack, const CommonPack& common, const u8* text, std::size_t length,
                                u8 seq) {
    UtteranceExtraction extraction;
    extract_utterance(pack, common, text, length, extraction);
    return tier2_text_commit(extraction, seq);
}

}  // namespace itantra
