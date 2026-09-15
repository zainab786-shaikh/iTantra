#include "select/select.h"

#include "lang/languages.h"
#include "packet/parse.h"
#include "tier1/frame.h"

namespace itantra {

namespace {

bool valid_language(LangId id) noexcept {
    return language_of_id(id) != nullptr;
}

bool request_valid(const SelectTables& tables, const SelectRequest& r) noexcept {
    if (tables.tier2 == nullptr || !tables.tier2->loaded()) return false;
    if (r.context == nullptr || (r.input == nullptr && r.input_length != 0u)) return false;
    if (r.tier2_text.begin > r.tier2_text.end || r.tier2_text.end > r.input_length) return false;
    if (!valid_language(r.sender_language) || !valid_language(r.listener_language)) return false;
    if (tables.tier1.pack != nullptr) {
        u8 id = kLangIdUnassigned;
        if (!lang_id_of(tables.tier1.pack->language(), id) || id != r.sender_language) return false;
    }
    return true;
}

Tier1Verdict classify(Tier1Outcome outcome, SafetyTrigger& trigger) noexcept {
    trigger = SafetyTrigger::None;
    switch (outcome) {
        case Tier1Outcome::Ok:                      return Tier1Verdict::Safe;
        case Tier1Outcome::InvalidArgument:         return Tier1Verdict::Invalid;
        case Tier1Outcome::TooLong:                 return Tier1Verdict::Unavailable;
        case Tier1Outcome::LowConfidence:           trigger = SafetyTrigger::LowSttConfidence; break;
        case Tier1Outcome::NoHead:
        case Tier1Outcome::AmbiguousHead:           trigger = SafetyTrigger::NoHead; break;
        case Tier1Outcome::TwoTopClass:             trigger = SafetyTrigger::TwoTopClass; break;
        case Tier1Outcome::NoRule:
        case Tier1Outcome::AmbiguousRuleSlot:
        case Tier1Outcome::NegationWithoutRule:     trigger = SafetyTrigger::NoRule; break;
        case Tier1Outcome::RequiredSlotMissing:
        case Tier1Outcome::LiteralNotPlaceable:     trigger = SafetyTrigger::RequiredSlotMissing; break;
        case Tier1Outcome::ReadbackUnrenderable:
        case Tier1Outcome::ReadbackUnrenderedSlot:
        case Tier1Outcome::ReadbackUnexplainedWord:
        case Tier1Outcome::ReadbackBelowBar:        trigger = SafetyTrigger::ReadbackLostMeaning; break;
        case Tier1Outcome::SelfCheckFailed:         trigger = SafetyTrigger::NegationCopiesDisagree; break;
        case Tier1Outcome::NegationAmbiguous:
        case Tier1Outcome::AmbiguousSlot:
        case Tier1Outcome::UnrepresentableValue:
        case Tier1Outcome::ValueKindCollision:
        case Tier1Outcome::LiteralTooLong:          trigger = SafetyTrigger::SenderRefusal; break;
        default:                                    return Tier1Verdict::Invalid;
    }
    return Tier1Verdict::Unsafe;
}

bool tier2_verifies(const SelectRequest& r, const NativePayload& p, Priority priority) noexcept {
    if (p.len == 0u || p.len > kMaxPayloadBytes) return false;
    Metadata m;
    u32 offset = 0u;
    if (parse_metadata(p.bytes, p.len, m, offset) != ParseStatus::Ok) return false;
    const bool boosted = tier2_boosted(r);
    return m.tier == Tier::Tier2 && m.seq == r.seq && m.priority == priority && m.language == r.sender_language &&
           m.hash_present == boosted &&
           (!boosted || m.context_hash == wire_context_hash(context_hash(*r.context))) &&
           offset == p.metadata_bits;
}

}  // namespace

const char* select_outcome_name(SelectOutcome outcome) noexcept {
    switch (outcome) {
        case SelectOutcome::Tier1:           return "Tier1";
        case SelectOutcome::Tier2:           return "Tier2";
        case SelectOutcome::ClauseTooLong:   return "ClauseTooLong";
        case SelectOutcome::NoEncoding:      return "NoEncoding";
        case SelectOutcome::InvalidArgument: return "InvalidArgument";
    }
    return "?";
}

const char* safety_trigger_name(SafetyTrigger trigger) noexcept {
    switch (trigger) {
        case SafetyTrigger::None:                   return "None";
        case SafetyTrigger::LowSttConfidence:       return "LowSttConfidence";
        case SafetyTrigger::NoHead:                 return "NoHead";
        case SafetyTrigger::TwoTopClass:            return "TwoTopClass";
        case SafetyTrigger::NoRule:                 return "NoRule";
        case SafetyTrigger::RequiredSlotMissing:    return "RequiredSlotMissing";
        case SafetyTrigger::ReadbackLostMeaning:    return "ReadbackLostMeaning";
        case SafetyTrigger::NegationCopiesDisagree: return "NegationCopiesDisagree";
        case SafetyTrigger::SenderRefusal:          return "SenderRefusal";
    }
    return "?";
}

Tier1Verdict assess_tier1(const SelectTables& tables, const SelectRequest& r, const Tier1Encoding& e,
                          SafetyTrigger& trigger) noexcept {
    const Tier1Verdict verdict = classify(e.outcome, trigger);
    if (verdict != Tier1Verdict::Safe) return verdict;

    // Ok from the encoder; now the bytes themselves (defence in depth, §8.1).
    if (e.payload.empty() || e.payload.size() > kMaxPayloadBytes || r.clause == nullptr || r.context == nullptr ||
        tables.tier1.common == nullptr) {
        return Tier1Verdict::Invalid;
    }
    Metadata m;
    u32 offset = 0u;
    const ParseStatus status = parse_metadata(e.payload.data(), static_cast<u32>(e.payload.size()), m, offset);
    if (status == ParseStatus::NegationMismatch) {
        trigger = SafetyTrigger::NegationCopiesDisagree;
        return Tier1Verdict::Unsafe;
    }
    const IntentInfo* intent = tables.tier1.common->intent(e.frame.intent);
    const bool uses_context  = frame_uses_context(e.frame);
    const bool critical      = intent != nullptr && (intent->is_alert || r.manual_critical);
    if (status != ParseStatus::Ok || intent == nullptr || m.tier != Tier::Tier1 || offset != e.metadata_bits ||
        m.symbol_count != e.symbols.size() || m.seq != r.seq || m.negation != e.negated ||
        m.negation != r.clause->negated() || m.priority != e.priority ||
        (m.priority == Priority::Critical) != critical || m.hash_present != uses_context ||
        (uses_context && m.context_hash != wire_context_hash(context_hash(*r.context))) ||
        (uses_context && !r.policy.allow_inheritance)) {   // a context-free message never relies on context
        return Tier1Verdict::Invalid;
    }
    return Tier1Verdict::Safe;
}

Priority selection_priority(const SelectRequest& r, const Tier1Encoding& e, Tier1Verdict verdict) noexcept {
    if (verdict == Tier1Verdict::Safe && e.priority == Priority::Critical) return Priority::Critical;
    return r.manual_critical ? Priority::Critical : Priority::Normal;
}

TierSelection choose_tier(const SelectTables& tables, const SelectRequest& r, const Tier1Encoding& e,
                          Tier2Status tier2_status, const NativePayload& tier2) {
    TierSelection s;
    s.tier1 = e;
    s.tier2_status = tier2_status;
    if (!request_valid(tables, r)) return s;

    s.tier1_verdict = assess_tier1(tables, r, e, s.trigger);
    s.priority      = selection_priority(r, e, s.tier1_verdict);
    const bool tier1_safe = s.tier1_verdict == Tier1Verdict::Safe;
    s.tier2_verified = tier2_status == Tier2Status::Ok && tier2_verifies(r, tier2, s.priority);

    if (tier1_safe) s.tier1_packet_bytes = native_packet_bytes(static_cast<u32>(e.payload.size()));
    if (s.tier2_verified) s.tier2_packet_bytes = native_packet_bytes(tier2.len);

    const bool send_tier1 =
        tier1_safe && (!s.tier2_verified || tier1_packet_is_smaller(s.tier1_packet_bytes, s.tier2_packet_bytes));
    if (send_tier1) {
        s.outcome        = SelectOutcome::Tier1;
        s.payload        = e.payload;
        s.metadata_bits  = e.metadata_bits;
        s.context_update = ContextUpdate::FromFrame;
        s.commit         = e.commit;
    } else if (s.tier2_verified) {
        s.outcome        = SelectOutcome::Tier2;
        s.payload.assign(tier2.bytes, tier2.bytes + tier2.len);
        s.metadata_bits  = tier2.metadata_bits;
        s.context_update = r.sender_language == r.listener_language ? ContextUpdate::FromText : ContextUpdate::Skip;
    } else {
        s.outcome = tier2_status == Tier2Status::TooLong ? SelectOutcome::ClauseTooLong : SelectOutcome::NoEncoding;
    }
    return s;
}

TierSelection select_tier(const SelectTables& tables, const SelectRequest& r) {
    if (!request_valid(tables, r)) return TierSelection{};

    Tier1Request q;
    q.clause          = r.clause;
    q.input           = r.input;
    q.input_length    = r.input_length;
    q.stt_confidence  = r.stt_confidence;
    q.manual_critical = r.manual_critical;
    q.seq             = r.seq;
    q.context         = r.context;
    q.adjacency       = r.adjacency;
    q.policy          = r.policy;
    const Tier1Encoding e = tier1_encode(tables.tier1, q);

    SafetyTrigger trigger = SafetyTrigger::None;
    const Tier1Verdict verdict = assess_tier1(tables, r, e, trigger);

    Tier2Message m;
    m.seq           = r.seq;
    m.priority      = selection_priority(r, e, verdict);
    m.language      = r.sender_language;
    m.boost_context = tier2_boosted(r) ? r.context : nullptr;
    NativePayload t2;
    t2.len           = 0u;
    t2.metadata_bits = 0u;
    const Tier2Status status = tier2_encode(*tables.tier2, r.input == nullptr ? nullptr : r.input + r.tier2_text.begin,
                                            r.tier2_text.end - r.tier2_text.begin, m, t2);
    return choose_tier(tables, r, e, status, t2);
}

}  // namespace itantra
