#include "tier1/slots.h"

#include <vector>

namespace itantra {

namespace {

bool inside(const SourceSpan& x, u32 begin, u32 end) noexcept {
    return x.begin >= begin && x.end <= end;
}

bool fresh(const Slot& slot, const SenderPolicy& policy) noexcept {
    return slot.age <= policy.max_inherit_age;
}

}  // namespace

bool literal_source_span(const ClauseExtraction& c, SourceSpan& span) noexcept {
    if (c.unmatched.empty()) return false;
    const u32 begin = c.unmatched.front().source.begin;
    const u32 end   = c.unmatched.back().source.end;
    // The unmatched tokens form one run: nothing matched lies between the first
    // and the last of them.
    for (const ConceptMatch& m : c.concepts) {
        if (inside(m.source, begin, end)) return false;
    }
    for (const TypedValue& v : c.values) {
        if (inside(v.source, begin, end)) return false;
    }
    for (const SourceSpan& n : c.negations) {
        if (inside(n, begin, end)) return false;
    }
    span = SourceSpan{begin, end};
    return true;
}

SlotOutcome resolve_slots(const CommonPack& common, const SubwordVocabulary& vocabulary, const IntentInfo& intent,
                          const HeadSelection* head, const ClauseExtraction& clause, const u8* input,
                          std::size_t input_length, const Context& context, const SenderPolicy& policy,
                          Tier1Frame& frame) {
    frame        = Tier1Frame{};
    frame.intent = intent.id;
    if (clause.unrepresentable_value) return SlotOutcome::UnrepresentableValue;

    // Ambiguity resolved with the intent's expected slots (language §8.2).
    ClauseExtraction c = clause;
    assign_slots(c, intent.expected_slots);
    if ((c.ambiguous_slots & intent.expected_slots) != 0u) return SlotOutcome::AmbiguousSlot;

    const bool has_head = head != nullptr && head->status == HeadStatus::Found;
    u8 missing = 0u;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (((intent.expected_slots >> s) & 1u) == 0u) continue;
        FrameSlot& fs = frame.slots[s];

        if (has_head && head->slot == s) {
            if (!intent.head_implied) {
                fs.mode  = SlotMode::Id;
                fs.value = head->concept_id;
            }
            continue;
        }

        if (c.slots[s].state == SlotState::Value) {
            const u16 v = c.slots[s].value;
            bool said_concept = false;
            for (const ConceptMatch& m : c.concepts) {
                said_concept = said_concept || (m.concept_id == v && m.slot == s);
            }
            const ValueKind said = said_concept ? ValueKind::Concept : ValueKind::Number;
            if (value_kind(common, static_cast<u8>(s), v) != said) return SlotOutcome::ValueKindCollision;

            const Slot& known = context.slots[s];
            if (policy.allow_inheritance && s != SLOT_TIME && known.current == v && fresh(known, policy)) {
                fs.mode = SlotMode::Inherit;
            } else {
                fs.mode  = SlotMode::Id;
                fs.value = v;
            }
            continue;
        }

        if (((intent.required_slots >> s) & 1u) != 0u) missing = static_cast<u8>(missing | (1u << s));
    }
    if (missing == 0u) return SlotOutcome::Ok;

    u32 missing_count = 0u;
    u32 missing_slot  = 0u;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (((missing >> s) & 1u) != 0u) {
            ++missing_count;
            missing_slot = s;
        }
    }

    // An unknown word for the one missing slot (tier §5.6 "Concept not in the
    // codebook → LITERAL").
    if (missing_count == 1u && !c.unmatched.empty()) {
        SourceSpan span{0u, 0u};
        if (!literal_source_span(c, span)) return SlotOutcome::LiteralNotPlaceable;
        if (input == nullptr || span.end > input_length || span.begin >= span.end) return SlotOutcome::LiteralNotPlaceable;
        FrameSlot& fs = frame.slots[missing_slot];
        fs.mode = SlotMode::Literal;
        fs.literal.assign(reinterpret_cast<const char*>(input + span.begin), span.end - span.begin);
        std::vector<Symbol> tokens;
        vocabulary.tokenize(input + span.begin, span.end - span.begin, tokens);
        if (tokens.size() > kMaxLiteralTokens) return SlotOutcome::LiteralTooLong;
        return SlotOutcome::Ok;
    }

    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (((missing >> s) & 1u) == 0u) continue;
        const Slot& known = context.slots[s];
        if (!policy.allow_inheritance || s == SLOT_TIME || known.current == 0u || !fresh(known, policy)) {
            return SlotOutcome::RequiredSlotMissing;
        }
        frame.slots[s].mode = SlotMode::Inherit;
    }
    return SlotOutcome::Ok;
}

}  // namespace itantra
