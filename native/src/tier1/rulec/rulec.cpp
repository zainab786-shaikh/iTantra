#include "tier1/rulec/rulec.h"

#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "lang/pack.h"
#include "tier1/rules.h"

namespace itantra {
namespace rulec {

namespace {

struct Row {
    std::vector<std::string> cols;
    std::string              where;
};

bool read_tsv(const std::string& path, std::size_t columns, std::vector<Row>& rows, std::string& error) {
    rows.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t start   = 0u;
    std::size_t line_no = 0u;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1u;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        Row row;
        row.where = path + ":" + std::to_string(line_no);
        std::size_t s = 0u;
        for (;;) {
            const std::size_t tab = line.find('\t', s);
            row.cols.push_back(line.substr(s, tab == std::string::npos ? std::string::npos : tab - s));
            if (tab == std::string::npos) break;
            s = tab + 1u;
        }
        if (row.cols.size() != columns) {
            error = row.where + ": expected " + std::to_string(columns) + " tab-separated columns";
            return false;
        }
        rows.push_back(std::move(row));
    }
    return true;
}

bool parse_uint(const std::string& s, u32 max, u32& out) {
    if (s.empty() || s.size() > 10u) return false;
    u64 v = 0u;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10u + static_cast<u64>(c - '0');
    }
    if (v > max) return false;
    out = static_cast<u32>(v);
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0u;
    for (;;) {
        const std::size_t at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1u;
    }
    return out;
}

std::string trim(const std::string& s) {
    std::size_t b = 0u;
    std::size_t e = s.size();
    while (b < e && s[b] == ' ') ++b;
    while (e > b && s[e - 1u] == ' ') --e;
    return s.substr(b, e - b);
}

struct ConceptRow {
    u16          id;
    u8           slot;
    ConceptClass cls;
};

struct IntentRow {
    u16  id;
    u8   expected;
    u8   required;
    bool head_implied;
};

bool slot_mask(const std::string& list, u8& mask) {
    mask = 0u;
    if (list == "-") return true;
    for (const std::string& name : split(list, ',')) {
        u8 slot = 0u;
        if (!slot_from_name(name, slot)) return false;
        mask = static_cast<u8>(mask | (1u << slot));
    }
    return true;
}

const char* class_name(ConceptClass c) {
    switch (c) {
        case ConceptClass::Action: return "ACTION";
        case ConceptClass::Event: return "EVENT";
        case ConceptClass::State: return "STATE";
        case ConceptClass::Entity: return "ENTITY";
        case ConceptClass::Modifier: return "MODIFIER";
    }
    return "?";
}

CompileResult failure(const std::string& why) {
    CompileResult r;
    r.error = "rulec: " + why;
    return r;
}

}  // namespace

CompileResult compile(const std::string& common_dir, const std::string& rules_tsv, const std::string& answers_tsv) {
    std::string error;
    std::vector<Row> rows;

    // ---- common names ----
    std::map<std::string, u32> categories;
    if (!read_tsv(common_dir + "/categories.tsv", 2u, rows, error)) return failure(error);
    for (const Row& r : rows) {
        u32 bit = 0u;
        if (!parse_uint(r.cols[0], 31u, bit)) return failure(r.where + ": invalid category bit");
        categories[r.cols[1]] = bit;
    }

    std::map<std::string, ConceptRow> concepts;
    std::map<u16, std::string>        concept_names;
    if (!read_tsv(common_dir + "/concepts.tsv", 5u, rows, error)) return failure(error);
    static const std::map<std::string, ConceptClass> kClasses = {{"ACTION", ConceptClass::Action},
                                                                 {"EVENT", ConceptClass::Event},
                                                                 {"STATE", ConceptClass::State},
                                                                 {"ENTITY", ConceptClass::Entity},
                                                                 {"MODIFIER", ConceptClass::Modifier}};
    for (const Row& r : rows) {
        u32 id = 0u;
        if (!parse_uint(r.cols[0], 0xFFFFu, id) || id == 0u) return failure(r.where + ": invalid concept id");
        u8 slot = kNoSlot;
        if (r.cols[2] != "-" && !slot_from_name(r.cols[2], slot)) return failure(r.where + ": unknown slot");
        const auto cls = kClasses.find(r.cols[3]);
        if (cls == kClasses.end()) return failure(r.where + ": unknown class");
        concepts[r.cols[1]] = ConceptRow{static_cast<u16>(id), slot, cls->second};
        concept_names[static_cast<u16>(id)] = r.cols[1];
    }

    std::map<std::string, IntentRow> intents;
    if (!read_tsv(common_dir + "/intents.tsv", 6u, rows, error)) return failure(error);
    for (const Row& r : rows) {
        u32 id = 0u;
        u32 implied = 0u;
        IntentRow row{};
        if (!parse_uint(r.cols[0], 0xFFFFu, id) || id == 0u) return failure(r.where + ": invalid intent id");
        if (!slot_mask(r.cols[2], row.expected) || !slot_mask(r.cols[3], row.required)) {
            return failure(r.where + ": invalid slot list");
        }
        if (!parse_uint(r.cols[4], 1u, implied)) return failure(r.where + ": head_implied must be 0 or 1");
        row.id           = static_cast<u16>(id);
        row.head_implied = implied == 1u;
        intents[r.cols[1]] = row;
    }

    // ---- rules ----
    std::vector<Rule>                    rules;
    std::set<std::pair<u16, u32>>        priorities;   // (head, rule_priority)
    std::set<u16>                        reachable;
    std::set<u16>                        heads_with_rules;
    if (!read_tsv(rules_tsv, 4u, rows, error)) return failure(error);
    for (const Row& r : rows) {
        const auto head = concepts.find(r.cols[0]);
        if (head == concepts.end()) return failure(r.where + ": unknown head concept '" + r.cols[0] + "'");
        u32 priority = 0u;
        if (!parse_uint(r.cols[1], 255u, priority)) return failure(r.where + ": rule_priority must be 0 ... 255");
        const auto intent = intents.find(r.cols[2]);
        if (intent == intents.end()) return failure(r.where + ": unknown intent '" + r.cols[2] + "'");

        // §12.1 item 1
        if (!priorities.insert(std::make_pair(head->second.id, priority)).second) {
            return failure(r.where + ": duplicate rule_priority " + std::to_string(priority) + " in bucket " +
                           r.cols[0] + " (tier 12.1 item 1)");
        }

        Rule rule{};
        rule.head_concept  = head->second.id;
        rule.rule_priority = static_cast<u8>(priority);
        rule.intent        = intent->second.id;
        if (trim(r.cols[3]) != "-") {
            for (const std::string& raw : split(r.cols[3], ';')) {
                const std::vector<std::string> t = split(trim(raw), ' ');
                if (rule.cond_count == kMaxConditions) return failure(r.where + ": more than four conditions");
                Condition c{kNoConditionSlot, 0u, 0u};
                if (t.size() == 1u && (t[0] == "NEG_SET" || t[0] == "NEG_CLEAR")) {
                    c.op = static_cast<u8>(t[0] == "NEG_SET" ? ConditionOp::NegSet : ConditionOp::NegClear);
                } else {
                    if (t.size() < 2u || !slot_from_name(t[0], c.slot)) {
                        return failure(r.where + ": invalid condition '" + trim(raw) + "'");
                    }
                    // §12.1 item 3
                    if (((intent->second.expected >> c.slot) & 1u) == 0u) {
                        return failure(r.where + ": condition on " + t[0] + ", which intent " + r.cols[2] +
                                       " does not declare (tier 12.1 item 3)");
                    }
                    if (t.size() == 2u && t[1] == "PRESENT") {
                        c.op = static_cast<u8>(ConditionOp::Present);
                    } else if (t.size() == 2u && t[1] == "ABSENT") {
                        c.op = static_cast<u8>(ConditionOp::Absent);
                    } else if (t.size() == 3u && t[1] == "EQ") {
                        const auto operand = concepts.find(t[2]);
                        if (operand == concepts.end() || operand->second.slot != c.slot) {
                            return failure(r.where + ": EQ needs a concept of slot " + t[0]);
                        }
                        c.op      = static_cast<u8>(ConditionOp::Eq);
                        c.operand = operand->second.id;
                    } else if (t.size() == 3u && t[1] == "IN_CATEGORY") {
                        u32 mask = 0u;
                        for (const std::string& name : split(t[2], ',')) {
                            const auto cat = categories.find(name);
                            if (cat == categories.end()) return failure(r.where + ": unknown category '" + name + "'");
                            if (cat->second > 15u) return failure(r.where + ": category bit above 15 in a rule");
                            mask |= u32{1} << cat->second;
                        }
                        c.op      = static_cast<u8>(ConditionOp::InCategory);
                        c.operand = static_cast<u16>(mask);
                    } else {
                        return failure(r.where + ": invalid condition '" + trim(raw) + "'");
                    }
                }
                rule.conds[rule.cond_count++] = c;
            }
        }

        const u8 head_slot = head->second.slot;
        if (!intent->second.head_implied &&
            (head_slot == kNoSlot || ((intent->second.expected >> head_slot) & 1u) == 0u)) {
            return failure(r.where + ": intent " + r.cols[2] + " has head_implied 0 but does not expect the slot of head " +
                           r.cols[0] + " (tier 5.5)");
        }
        if (intent->second.head_implied && head_slot != kNoSlot && ((intent->second.required >> head_slot) & 1u) != 0u) {
            return failure(r.where + ": intent " + r.cols[2] + " requires the slot of its implied head " + r.cols[0]);
        }
        rules.push_back(rule);
        reachable.insert(rule.intent);
        heads_with_rules.insert(rule.head_concept);
    }

    // ---- answers ----
    std::vector<AnswerRule>       answers;
    std::set<std::pair<u16, u8>>  answer_keys;
    if (!read_tsv(answers_tsv, 3u, rows, error)) return failure(error);
    for (const Row& r : rows) {
        const auto query  = intents.find(r.cols[0]);
        const auto answer = intents.find(r.cols[2]);
        u8 slot = 0u;
        if (query == intents.end() || answer == intents.end()) return failure(r.where + ": unknown intent");
        if (!slot_from_name(r.cols[1], slot)) return failure(r.where + ": unknown slot");
        if (((answer->second.expected >> slot) & 1u) == 0u) {
            return failure(r.where + ": answer intent " + r.cols[2] + " does not expect " + r.cols[1]);
        }
        if (!answer_keys.insert(std::make_pair(query->second.id, slot)).second) return failure(r.where + ": duplicate answer");
        answers.push_back(AnswerRule{query->second.id, slot, answer->second.id});
        reachable.insert(answer->second.id);
    }

    // §12.1 item 2
    for (const auto& kv : intents) {
        if (reachable.count(kv.second.id) == 0u) {
            return failure(rules_tsv + ": intent " + kv.first + " is not reachable by any rule (tier 12.1 item 2)");
        }
    }
    // §12.1 item 4
    for (const auto& kv : concepts) {
        const ConceptClass cls = kv.second.cls;
        if ((cls == ConceptClass::Action || cls == ConceptClass::Event || cls == ConceptClass::State) &&
            heads_with_rules.count(kv.second.id) == 0u) {
            return failure(rules_tsv + ": " + std::string(class_name(cls)) + " concept " + kv.first +
                           " has no rule (tier 12.1 item 4)");
        }
    }

    CompileResult result;
    result.ok           = true;
    result.rule_count   = static_cast<u32>(rules.size());
    result.answer_count = static_cast<u32>(answers.size());
    result.rules_bin    = serialize_rules(std::move(rules), std::move(answers));
    return result;
}

bool check_templates(const CommonPack& common, const std::vector<std::pair<std::string, const LanguagePack*>>& packs,
                     std::string& error) {
    std::vector<TemplatePiece> pieces;
    for (const IntentInfo& intent : common.intents()) {
        bool        seen       = false;
        u32         first_mask = 0u;
        std::string first_language;
        for (const auto& entry : packs) {
            if (entry.second == nullptr) continue;
            std::string_view text;
            // Coverage (Phase 10): a missing template is an error, never skipped —
            // a Tier 1 message for this intent could not be rendered in this
            // language at all.
            if (!entry.second->template_text(intent.id, text)) {
                error = "rulec: intent " + std::to_string(intent.id) + " has no template in " + entry.first +
                        " (every language must render every intent)";
                return false;
            }
            if (!parse_template(text, pieces)) {
                error = "rulec: template for intent " + std::to_string(intent.id) + " in " + entry.first + " does not parse";
                return false;
            }
            u32 mask = 0u;
            for (const TemplatePiece& p : pieces) {
                if (p.placeholder) mask |= u32{1} << p.slot;
            }
            const u32 stray = mask & ~u32{intent.required_slots};
            if (stray != 0u) {
                u8 slot = 0u;
                while (((stray >> slot) & 1u) == 0u) ++slot;
                error = "rulec: template for intent " + std::to_string(intent.id) + " in " + entry.first + " uses slot " +
                        slot_name(slot) + ", which the intent does not require (read-back R2)";
                return false;
            }
            if (!seen) {
                seen           = true;
                first_mask     = mask;
                first_language = entry.first;
            } else if (mask != first_mask) {
                error = "rulec: templates for intent " + std::to_string(intent.id) + " use different slots in " +
                        first_language + " and " + entry.first + " (read-back R2)";
                return false;
            }
            // Coverage (Phase 10): every concept that can fill a named-form
            // placeholder has that form in this language, and "native" digits
            // have a digit set. What remains possible — a number in a named-form
            // slot, a concept in a digits slot — is the receiver's render_fail.
            for (const TemplatePiece& p : pieces) {
                if (!p.placeholder) continue;
                if (p.text == "digits") continue;
                if (p.text == "native") {
                    if (entry.second->rules().primary_digits.size() != 10u) {
                        error = "rulec: template for intent " + std::to_string(intent.id) + " in " + entry.first +
                                " uses native digits, but the pack has no digit set";
                        return false;
                    }
                    continue;
                }
                for (const ConceptInfo& c : common.concepts()) {
                    std::string_view form;
                    if (c.slot != p.slot || entry.second->form(c.id, p.text, form)) continue;
                    error = "rulec: concept " + std::to_string(c.id) + " (" + slot_name(p.slot) + ") has no form \"" +
                            std::string(p.text) + "\" in " + entry.first + ", which the template for intent " +
                            std::to_string(intent.id) + " needs";
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace rulec
}  // namespace itantra
