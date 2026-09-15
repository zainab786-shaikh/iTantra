#include "tier1/encode.h"

#include <memory>

#include "tier1/decode.h"
#include "tier1/head.h"

namespace itantra {

const char* tier1_outcome_name(Tier1Outcome outcome) noexcept {
    switch (outcome) {
        case Tier1Outcome::Ok: return "Ok";
        case Tier1Outcome::InvalidArgument: return "InvalidArgument";
        case Tier1Outcome::LowConfidence: return "LowConfidence";
        case Tier1Outcome::NegationAmbiguous: return "NegationAmbiguous";
        case Tier1Outcome::NoHead: return "NoHead";
        case Tier1Outcome::TwoTopClass: return "TwoTopClass";
        case Tier1Outcome::AmbiguousHead: return "AmbiguousHead";
        case Tier1Outcome::NoRule: return "NoRule";
        case Tier1Outcome::AmbiguousRuleSlot: return "AmbiguousRuleSlot";
        case Tier1Outcome::NegationWithoutRule: return "NegationWithoutRule";
        case Tier1Outcome::AmbiguousSlot: return "AmbiguousSlot";
        case Tier1Outcome::UnrepresentableValue: return "UnrepresentableValue";
        case Tier1Outcome::ValueKindCollision: return "ValueKindCollision";
        case Tier1Outcome::RequiredSlotMissing: return "RequiredSlotMissing";
        case Tier1Outcome::LiteralNotPlaceable: return "LiteralNotPlaceable";
        case Tier1Outcome::LiteralTooLong: return "LiteralTooLong";
        case Tier1Outcome::ReadbackUnrenderable: return "ReadbackUnrenderable";
        case Tier1Outcome::ReadbackUnrenderedSlot: return "ReadbackUnrenderedSlot";
        case Tier1Outcome::ReadbackUnexplainedWord: return "ReadbackUnexplainedWord";
        case Tier1Outcome::ReadbackBelowBar: return "ReadbackBelowBar";
        case Tier1Outcome::TooLong: return "TooLong";
        case Tier1Outcome::SelfCheckFailed: return "SelfCheckFailed";
    }
    return "Unknown";
}

namespace {

Tier1Outcome outcome_of(SlotOutcome s) noexcept {
    switch (s) {
        case SlotOutcome::Ok: return Tier1Outcome::Ok;
        case SlotOutcome::AmbiguousSlot: return Tier1Outcome::AmbiguousSlot;
        case SlotOutcome::UnrepresentableValue: return Tier1Outcome::UnrepresentableValue;
        case SlotOutcome::ValueKindCollision: return Tier1Outcome::ValueKindCollision;
        case SlotOutcome::RequiredSlotMissing: return Tier1Outcome::RequiredSlotMissing;
        case SlotOutcome::LiteralNotPlaceable: return Tier1Outcome::LiteralNotPlaceable;
        case SlotOutcome::LiteralTooLong: return Tier1Outcome::LiteralTooLong;
    }
    return Tier1Outcome::InvalidArgument;
}

}  // namespace

Tier1Encoding tier1_encode(const Tier1Tables& tables, const Tier1Request& request) {
    Tier1Encoding e;
    if (tables.common == nullptr || tables.pack == nullptr || tables.rules == nullptr || tables.subwords == nullptr ||
        !tables.rules->loaded() || !tables.subwords->loaded() || request.clause == nullptr ||
        request.context == nullptr || (request.input == nullptr && request.input_length != 0u)) {
        return e;
    }
    const CommonPack&       common = *tables.common;
    const LanguagePack&     pack   = *tables.pack;
    const ClauseExtraction& clause = *request.clause;

    // §5.8 "Low input confidence disables Tier 1 outright."
    if (request.stt_confidence < pack.stt_confidence_threshold()) {
        e.outcome = Tier1Outcome::LowConfidence;
        return e;
    }
    if (clause.negations.size() > 1u) {
        e.outcome = Tier1Outcome::NegationAmbiguous;
        return e;
    }
    e.negated = clause.negated();

    // §5.2 adjacency first, then §5.3 head and §5.4 rules.
    const IntentInfo* intent = nullptr;
    HeadSelection     head;
    AdjacencyAnswer   answer;
    if (request.adjacency != nullptr) answer = adjacency_answer(*request.adjacency, *tables.rules, clause);
    if (answer.answered) {
        e.answered = true;
        intent     = common.intent(answer.intent);
    } else {
        head = select_head(clause);
        switch (head.status) {
            case HeadStatus::Found:
                break;
            case HeadStatus::NoHead:
                e.outcome = Tier1Outcome::NoHead;
                return e;
            case HeadStatus::TwoTopClass:
                e.outcome = Tier1Outcome::TwoTopClass;
                return e;
            case HeadStatus::Ambiguous:
            default:
                e.outcome = Tier1Outcome::AmbiguousHead;
                return e;
        }
        e.head = head.concept_id;
        const Rule* rule = nullptr;
        switch (match_rule(*tables.rules, common, head.concept_id, clause, rule)) {
            case RuleMatch::Matched:
                break;
            case RuleMatch::NoRule:
                e.outcome = Tier1Outcome::NoRule;
                return e;
            case RuleMatch::AmbiguousSlot:
            default:
                e.outcome = Tier1Outcome::AmbiguousRuleSlot;
                return e;
        }
        if (e.negated && !rule_tests_negation(*rule)) {
            e.outcome = Tier1Outcome::NegationWithoutRule;
            return e;
        }
        intent = common.intent(rule->intent);
    }
    if (intent == nullptr) return e;   // unreachable: the rule table was validated against common

    const SlotOutcome slots =
        resolve_slots(common, tables.subwords->vocabulary(), *intent, e.answered ? nullptr : &head, clause,
                      request.input, request.input_length, *request.context, request.policy, e.frame);
    if (slots != SlotOutcome::Ok) {
        e.outcome = outcome_of(slots);
        return e;
    }

    // Language §11.2 / packet §11.1: either path raises it; neither lowers it.
    e.priority = (intent->is_alert || request.manual_critical) ? Priority::Critical : Priority::Normal;

    // The literal, if any, was taken from exactly this span (tier1/slots.h).
    bool has_literal = false;
    for (const FrameSlot& fs : e.frame.slots) has_literal = has_literal || fs.mode == SlotMode::Literal;
    SourceSpan literal_span{0u, 0u};
    const bool span_known = has_literal && literal_source_span(clause, literal_span);

    e.readback = readback_check(pack, common, e.frame, *request.context, clause, e.priority,
                                span_known ? &literal_span : nullptr);
    if (e.readback.unrendered_slots != 0u) {
        e.outcome = Tier1Outcome::ReadbackUnrenderedSlot;
        return e;
    }
    if (!e.readback.rendered) {
        e.outcome = Tier1Outcome::ReadbackUnrenderable;
        return e;
    }
    if (!e.readback.covered) {
        e.outcome = Tier1Outcome::ReadbackUnexplainedWord;
        return e;
    }
    if (!e.readback.passed) {
        e.outcome = Tier1Outcome::ReadbackBelowBar;
        return e;
    }

    const FrameFault fault = frame_to_symbols(common, tables.subwords->vocabulary(), e.frame, e.symbols);
    if (fault != FrameFault::None) {
        e.outcome = fault == FrameFault::LiteralTooLong ? Tier1Outcome::LiteralTooLong : Tier1Outcome::SelfCheckFailed;
        return e;
    }
    if (e.symbols.size() > kMaxSymbolCount) {
        e.outcome = Tier1Outcome::TooLong;
        return e;
    }

    const Tier1Model model(common, tables.subwords->ngram());
    if (!model.valid()) return e;
    AssemblyInput in;
    in.tier         = Tier::Tier1;
    in.symbols      = e.symbols.data();
    in.symbol_count = static_cast<u16>(e.symbols.size());
    in.model        = &model;
    in.seq          = request.seq;
    in.priority     = e.priority;
    in.negation     = e.negated;
    in.language     = 0u;
    in.hash_present = frame_uses_context(e.frame);
    in.context_hash = in.hash_present ? wire_context_hash(context_hash(*request.context)) : u16{0};

    const auto payload = std::make_unique<NativePayload>();
    const AsmResult assembled = assemble(in, *payload);
    if (assembled == AsmResult::TooLong) {
        e.outcome = Tier1Outcome::TooLong;
        return e;
    }
    if (assembled != AsmResult::Ok) {
        e.outcome = Tier1Outcome::SelfCheckFailed;
        return e;
    }

    // The sender simulates the receiver's decode of its own bytes.
    Tier1Decoded check;
    if (tier1_decode(common, *tables.subwords, payload->bytes, payload->len, check) != Tier1DecodeStatus::Ok ||
        check.frame != e.frame || check.metadata.negation != e.negated || check.metadata.priority != e.priority) {
        e.outcome = Tier1Outcome::SelfCheckFailed;
        return e;
    }

    e.payload.assign(payload->bytes, payload->bytes + payload->len);
    e.metadata_bits = payload->metadata_bits;
    e.commit        = frame_commit_payload(e.frame, request.seq);
    e.outcome       = Tier1Outcome::Ok;
    return e;
}

}  // namespace itantra
