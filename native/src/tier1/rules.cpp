#include "tier1/rules.h"

#include <algorithm>

#include "tier2/wire.h"

namespace itantra {

namespace {

bool fail(std::string& error, const char* why) {
    error = std::string("rules.bin: ") + why;
    return false;
}

bool rule_order(const Rule& a, const Rule& b) noexcept {
    return a.head_concept != b.head_concept ? a.head_concept < b.head_concept : a.rule_priority > b.rule_priority;
}

u32 answer_key(u16 query, u8 slot) noexcept {
    return (u32{query} << 8) | slot;
}

bool condition_holds(const CommonPack& common, const Condition& c, const ClauseExtraction& clause, bool& ambiguous) {
    const ConditionOp op = static_cast<ConditionOp>(c.op);
    if (op == ConditionOp::NegSet) return clause.negated();
    if (op == ConditionOp::NegClear) return !clause.negated();

    const SlotCandidate& slot = clause.slots[c.slot];
    if (slot.state == SlotState::Ambiguous) {
        ambiguous = true;
        return false;
    }
    const bool present = slot.state == SlotState::Value;
    switch (op) {
        case ConditionOp::Present:
            return present;
        case ConditionOp::Absent:
            return !present;
        case ConditionOp::Eq:
            return present && slot.value == c.operand;
        case ConditionOp::InCategory: {
            if (!present) return false;
            const ConceptInfo* info = common.concept_info(slot.value);
            return info != nullptr && info->slot == c.slot && (info->categories & c.operand) != 0u;
        }
        default:
            return false;
    }
}

}  // namespace

const std::vector<std::string>& RuleTable::file_names() {
    static const std::vector<std::string> names = {"rules.bin"};
    return names;
}

bool RuleTable::load(const PackFiles& files, const CommonPack& common, std::string& error) {
    rules_.clear();
    answers_.clear();
    loaded_ = false;

    const std::vector<u8>* file = nullptr;
    for (const PackFile& f : files) {
        if (f.first == "rules.bin") file = &f.second;
    }
    if (file == nullptr) return fail(error, "missing");
    const u8*   payload = nullptr;
    std::size_t length  = 0u;
    std::string why;
    if (!unwrap_container(*file, PackKind::Rules, payload, length, why)) return fail(error, why.c_str());

    tier2wire::Reader r(payload, length);
    u32 version = 0u;
    u32 count   = 0u;
    if (!r.read32(version) || version != kRuleTableVersion) return fail(error, "unsupported table version");
    if (!r.read32(count) || count > r.remaining() / 22u) return fail(error, "truncated rules");

    std::vector<Rule> rules(count);
    for (u32 i = 0u; i < count; ++i) {
        Rule& rule = rules[i];
        if (!r.read16(rule.head_concept) || !r.read8(rule.rule_priority) || !r.read16(rule.intent) ||
            !r.read8(rule.cond_count)) {
            return fail(error, "truncated rules");
        }
        if (common.concept_info(rule.head_concept) == nullptr) return fail(error, "unknown head concept");
        if (common.intent(rule.intent) == nullptr) return fail(error, "unknown intent");
        if (rule.cond_count > kMaxConditions) return fail(error, "more than four conditions");
        for (u32 k = 0u; k < kMaxConditions; ++k) {
            Condition& c = rule.conds[k];
            if (!r.read8(c.slot) || !r.read8(c.op) || !r.read16(c.operand)) return fail(error, "truncated rules");
            if (k >= rule.cond_count) {
                if (c.slot != 0u || c.op != 0u || c.operand != 0u) return fail(error, "unused condition not zero");
                continue;
            }
            if (c.op >= kConditionOpCount) return fail(error, "unknown condition op");
            const ConditionOp op = static_cast<ConditionOp>(c.op);
            if (op == ConditionOp::NegSet || op == ConditionOp::NegClear) {
                if (c.slot != kNoConditionSlot || c.operand != 0u) return fail(error, "negation condition with a slot");
                continue;
            }
            if (c.slot >= kConceptSlotCount) return fail(error, "condition slot is not ACTOR ... STATE");
            if ((op == ConditionOp::Present || op == ConditionOp::Absent) && c.operand != 0u) {
                return fail(error, "PRESENT / ABSENT with an operand");
            }
            if (op == ConditionOp::Eq) {
                const ConceptInfo* info = common.concept_info(c.operand);
                if (info == nullptr || info->slot != c.slot) return fail(error, "EQ operand is not a concept of that slot");
            }
            if (op == ConditionOp::InCategory && c.operand == 0u) return fail(error, "IN_CATEGORY with an empty mask");
        }
        if (i != 0u) {
            const Rule& prev = rules[i - 1u];
            if (prev.head_concept == rule.head_concept && prev.rule_priority == rule.rule_priority) {
                return fail(error, "two rules in one bucket share a rule_priority");
            }
            if (!rule_order(prev, rule)) return fail(error, "rules not in bucket order");
        }
    }

    u32 answer_count = 0u;
    if (!r.read32(answer_count) || answer_count > r.remaining() / 5u) return fail(error, "truncated answers");
    std::vector<AnswerRule> answers(answer_count);
    for (u32 i = 0u; i < answer_count; ++i) {
        AnswerRule& a = answers[i];
        if (!r.read16(a.query_intent) || !r.read8(a.slot) || !r.read16(a.answer_intent)) {
            return fail(error, "truncated answers");
        }
        if (common.intent(a.query_intent) == nullptr || common.intent(a.answer_intent) == nullptr) {
            return fail(error, "answer names an unknown intent");
        }
        if (a.slot >= kConceptSlotCount) return fail(error, "answer slot is not ACTOR ... STATE");
        if (i != 0u && answer_key(answers[i - 1u].query_intent, answers[i - 1u].slot) >= answer_key(a.query_intent, a.slot)) {
            return fail(error, "answers not strictly ascending");
        }
    }
    if (r.remaining() != 0u) return fail(error, "trailing bytes");

    rules_   = std::move(rules);
    answers_ = std::move(answers);
    loaded_  = true;
    return true;
}

const Rule* RuleTable::bucket(u16 head, u32& count) const noexcept {
    count = 0u;
    const auto first = std::lower_bound(rules_.begin(), rules_.end(), head,
                                        [](const Rule& rule, u16 h) { return rule.head_concept < h; });
    auto last = first;
    while (last != rules_.end() && last->head_concept == head) ++last;
    count = static_cast<u32>(last - first);
    return count == 0u ? nullptr : &*first;
}

const AnswerRule* RuleTable::answer(u16 query_intent, u8 slot) const noexcept {
    const u32 key = answer_key(query_intent, slot);
    for (const AnswerRule& a : answers_) {
        if (answer_key(a.query_intent, a.slot) == key) return &a;
    }
    return nullptr;
}

bool RuleTable::is_query(u16 intent) const noexcept {
    for (const AnswerRule& a : answers_) {
        if (a.query_intent == intent) return true;
    }
    return false;
}

std::vector<u8> serialize_rules(std::vector<Rule> rules, std::vector<AnswerRule> answers) {
    std::stable_sort(rules.begin(), rules.end(), rule_order);
    std::sort(answers.begin(), answers.end(), [](const AnswerRule& a, const AnswerRule& b) {
        return answer_key(a.query_intent, a.slot) < answer_key(b.query_intent, b.slot);
    });
    std::vector<u8> payload;
    tier2wire::put32(payload, kRuleTableVersion);
    tier2wire::put32(payload, static_cast<u32>(rules.size()));
    for (const Rule& rule : rules) {
        tier2wire::put16(payload, rule.head_concept);
        tier2wire::put8(payload, rule.rule_priority);
        tier2wire::put16(payload, rule.intent);
        tier2wire::put8(payload, rule.cond_count);
        for (u32 k = 0u; k < kMaxConditions; ++k) {
            const Condition c = k < rule.cond_count ? rule.conds[k] : Condition{0u, 0u, 0u};
            tier2wire::put8(payload, c.slot);
            tier2wire::put8(payload, c.op);
            tier2wire::put16(payload, c.operand);
        }
    }
    tier2wire::put32(payload, static_cast<u32>(answers.size()));
    for (const AnswerRule& a : answers) {
        tier2wire::put16(payload, a.query_intent);
        tier2wire::put8(payload, a.slot);
        tier2wire::put16(payload, a.answer_intent);
    }
    return wrap_container(PackKind::Rules, payload);
}

RuleMatch match_rule(const RuleTable& table, const CommonPack& common, u16 head, const ClauseExtraction& clause,
                     const Rule*& matched) {
    matched = nullptr;
    u32 count = 0u;
    const Rule* bucket = table.bucket(head, count);
    for (u32 i = 0u; i < count; ++i) {   // highest rule_priority first
        const Rule& rule = bucket[i];
        bool holds     = true;
        bool ambiguous = false;
        for (u32 k = 0u; k < rule.cond_count && holds; ++k) {
            holds = condition_holds(common, rule.conds[k], clause, ambiguous);
        }
        if (ambiguous) return RuleMatch::AmbiguousSlot;
        if (holds) {
            matched = &rule;
            return RuleMatch::Matched;
        }
    }
    return RuleMatch::NoRule;
}

bool rule_tests_negation(const Rule& rule) noexcept {
    for (u32 k = 0u; k < rule.cond_count && k < kMaxConditions; ++k) {
        if (static_cast<ConditionOp>(rule.conds[k].op) == ConditionOp::NegSet) return true;
    }
    return false;
}

}  // namespace itantra
