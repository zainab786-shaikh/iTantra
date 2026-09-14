#pragma once

// Tier 1 rule table — tier §5.4, §12.1 (T7). SENDER-ONLY (tier §3.4): the
// receiver never loads, links or evaluates it; it decodes an intent ID and
// renders (receiver §4, handoff "sender-only decisions remain sender-only").
//
// Buckets keyed by head concept, each sorted by DESCENDING rule_priority; first
// match wins. rule_priority is rule evaluation order only — it has nothing to do
// with NORMAL / CRITICAL (tier §5.4, packet §11).
//
// rules.bin (lang/pack.h container, kind Rules), big-endian:
//
//   u32 table_version       = kRuleTableVersion
//   u32 rule_count
//   rule × rule_count       u16 head_concept, u8 rule_priority, u16 intent,
//                           u8 cond_count (0 … 4),
//                           condition × 4: u8 slot, u8 op, u16 operand
//                           (unused conditions all zero)
//   u32 answer_count
//   answer × answer_count   u16 query_intent, u8 slot, u16 answer_intent
//
//   Rules sorted by head ascending, then rule_priority strictly descending: no
//   two rules in a bucket share a rule_priority (§12.1 item 1), so the order is
//   total. Answers sorted by (query_intent, slot), unique.
//
// The remaining §12.1 build-time checks — every intent reachable, conditions
// only on slots the intent declares, a rule for every ACTION / EVENT / STATE
// concept — are enforced by the rule compiler (tier1/rulec/rulec.h), not at
// runtime.
//
// DECIDED in Phase 8 (tier spec implementation resolutions):
//   Condition operand   u16, as the spec's struct: a concept ID (EQ) or a
//                       category mask (IN_CATEGORY), so rule categories use bits
//                       0 … 15.
//   Condition inputs    the clause's own extracted slot candidates (what was
//                       said), never context. A condition that reads a slot left
//                       Ambiguous (language §8.2) stops evaluation: no intent.
//   NEG_SET / NEG_CLEAR the clause's negation flag; slot field 0xFF.
//   Answers             the adjacency table (tier §5.2): which intent a bare
//                       value in `slot` gives while `query_intent` awaits an
//                       answer. An intent is a QUERY iff it appears here.

#include <string>
#include <vector>

#include "common/types.h"
#include "lang/extract.h"
#include "lang/pack.h"

namespace itantra {

constexpr u32 kRuleTableVersion = 1u;
constexpr u32 kMaxConditions    = 4u;
constexpr u8  kNoConditionSlot  = 0xFFu;

enum class ConditionOp : u8 {
    Present    = 0,
    Absent     = 1,
    Eq         = 2,
    InCategory = 3,
    NegSet     = 4,
    NegClear   = 5,
};
constexpr u32 kConditionOpCount = 6u;

struct Condition {
    u8  slot;      // ACTOR … STATE, or kNoConditionSlot for NEG_SET / NEG_CLEAR
    u8  op;        // ConditionOp
    u16 operand;   // concept ID (EQ), category mask (IN_CATEGORY), else 0
};

struct Rule {
    u16       head_concept;
    u8        rule_priority;   // evaluation order ONLY — not message priority
    u16       intent;
    u8        cond_count;
    Condition conds[kMaxConditions];
};

struct AnswerRule {
    u16 query_intent;
    u8  slot;
    u16 answer_intent;
};

class RuleTable {
public:
    static const std::vector<std::string>& file_names();   // sender/rules.bin

    // Structural validation against `common`; on failure nothing is loaded.
    bool load(const PackFiles& files, const CommonPack& common, std::string& error);

    bool loaded() const noexcept { return loaded_; }

    // The bucket for `head`, highest rule_priority first; null and 0 if none.
    const Rule*       bucket(u16 head, u32& count) const noexcept;
    const AnswerRule* answer(u16 query_intent, u8 slot) const noexcept;
    bool              is_query(u16 intent) const noexcept;

    const std::vector<Rule>&       rules() const noexcept { return rules_; }
    const std::vector<AnswerRule>& answers() const noexcept { return answers_; }

private:
    std::vector<Rule>       rules_;
    std::vector<AnswerRule> answers_;
    bool                    loaded_ = false;
};

// A complete rules.bin container (sorts rules and answers into file order).
std::vector<u8> serialize_rules(std::vector<Rule> rules, std::vector<AnswerRule> answers);

enum class RuleMatch : u8 {
    Matched,
    NoRule,          // no rule in the head's bucket holds → INTENT_NONE
    AmbiguousSlot,   // a condition read an Ambiguous slot → INTENT_NONE
};

// First rule of the head's bucket whose conditions all hold for `clause`
// (tier §5.4). `matched` is set only for Matched.
RuleMatch match_rule(const RuleTable& table, const CommonPack& common, u16 head, const ClauseExtraction& clause,
                     const Rule*& matched);

// True if the rule has a NEG_SET condition — it selects a negated intent.
bool rule_tests_negation(const Rule& rule) noexcept;

}  // namespace itantra
