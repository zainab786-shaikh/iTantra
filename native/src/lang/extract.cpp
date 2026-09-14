#include "lang/extract.h"

#include <algorithm>

#include "lang/lexicon.h"

namespace itantra {

namespace {

constexpr u8 kKindConcept    = 0u;   // wins exact ties over numbers and negation words
constexpr u8 kKindNumberWord = 1u;
constexpr u8 kKindDigits     = 2u;
constexpr u8 kKindNegation   = 3u;
constexpr u32 kMaxDigitRun   = 9u;

enum class ItemKind : u8 { Concept, Number, Word, Negation };

struct Item {
    ItemKind kind;
    u32      begin;
    u32      end;
    u32      pattern;    // Concept
    u32      value;      // Number
    bool     consumed;
};

bool digit_run(const std::string& text, u32 begin, u32 end, u32& value) {
    if (end <= begin || end - begin > kMaxDigitRun) return false;
    u32 v = 0u;
    for (u32 i = begin; i < end; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return false;
        v = v * 10u + static_cast<u32>(c - '0');
    }
    value = v;
    return true;
}

void add_automaton_candidates(const Automaton& automaton, u8 kind, const u8* text, std::size_t n,
                              const std::vector<bool>& starts, std::vector<MatchCandidate>& candidates) {
    std::vector<Automaton::Hit> hits;
    automaton.find_all(text, n, hits);
    for (const Automaton::Hit& h : hits) {
        if (on_codepoint_boundaries(starts, h.begin, h.end) && on_token_boundaries(text, n, h.begin, h.end)) {
            candidates.push_back(MatchCandidate{h.begin, h.end, automaton.pattern_codepoints(h.pattern), kind, h.pattern});
        }
    }
}

ClauseExtraction extract_clause(const LanguagePack& pack, const CommonPack& common, const MappedText& utterance,
                                const ClauseSpan& span) {
    ClauseExtraction c;
    c.source      = utterance.source_of(span.begin, span.end);
    c.clause_text = utterance.text.substr(span.begin, span.end - span.begin);

    const MappedText m = normalize_clause(utterance, span.begin, span.end);
    c.match_text = m.text;
    const auto*       text = reinterpret_cast<const u8*>(m.text.data());
    const std::size_t n    = m.text.size();
    const std::vector<bool> starts = codepoint_starts(text, n);

    // ---- candidates from every source, one selection ----
    std::vector<MatchCandidate> candidates;
    add_automaton_candidates(pack.lexicon(), kKindConcept, text, n, starts, candidates);
    add_automaton_candidates(pack.numbers(), kKindNumberWord, text, n, starts, candidates);
    for (std::size_t i = 0u; i < n;) {
        if (text[i] == 0x20u) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && text[j] != 0x20u) ++j;
        u32 value = 0u;
        if (digit_run(m.text, static_cast<u32>(i), static_cast<u32>(j), value)) {
            candidates.push_back(MatchCandidate{static_cast<u32>(i), static_cast<u32>(j), static_cast<u32>(j - i),
                                                kKindDigits, value});
        }
        i = j;
    }
    add_automaton_candidates(pack.negations(), kKindNegation, text, n, starts, candidates);
    const std::vector<MatchCandidate> selected = select_matches(std::move(candidates));

    // ---- items in text order: selected matches and the tokens between them ----
    std::vector<Item> items;
    std::size_t next = 0u;
    for (std::size_t i = 0u; i < n;) {
        if (text[i] == 0x20u) {
            ++i;
            continue;
        }
        if (next < selected.size() && selected[next].begin == i) {
            const MatchCandidate& s = selected[next++];
            Item item{ItemKind::Word, s.begin, s.end, kNoIndex, 0u, false};
            if (s.kind == kKindConcept) {
                item.kind    = ItemKind::Concept;
                item.pattern = s.index;
            } else if (s.kind == kKindNegation) {
                item.kind = ItemKind::Negation;
            } else {
                item.kind  = ItemKind::Number;
                item.value = s.kind == kKindNumberWord ? pack.number_value(s.index) : s.index;
            }
            items.push_back(item);
            i = s.end;
            continue;
        }
        std::size_t j = i;
        while (j < n && text[j] != 0x20u) ++j;
        items.push_back(Item{ItemKind::Word, static_cast<u32>(i), static_cast<u32>(j), kNoIndex, 0u, false});
        i = j;
    }

    // ---- typed-value scanners, in pack order, first match consumes ----
    for (std::size_t p = 0u; p < items.size();) {
        bool matched = false;
        for (const ValuePattern& vp : pack.patterns()) {
            const std::size_t k = vp.elements.size();
            if (p + k > items.size()) continue;
            u32 numbers[2] = {0u, 0u};
            u32 found = 0u;
            bool ok = true;
            for (std::size_t e = 0u; e < k && ok; ++e) {
                const Item& item = items[p + e];
                const PatternElement& el = vp.elements[e];
                if (el.kind == PatternElementKind::Number) {
                    ok = item.kind == ItemKind::Number && found < 2u;
                    if (ok) numbers[found++] = item.value;
                } else {
                    ok = m.text.compare(item.begin, item.end - item.begin, el.word) == 0;
                }
            }
            if (!ok) continue;

            const i64 value = i64{vp.a} * i64{numbers[0]} + i64{vp.b} * i64{numbers[1]} + i64{vp.c};
            for (std::size_t e = 0u; e < k; ++e) items[p + e].consumed = true;
            if (value >= i64{vp.min} && value <= i64{vp.max}) {
                c.values.push_back(TypedValue{vp.slot, static_cast<u32>(value),
                                              m.source_of(items[p].begin, items[p + k - 1u].end)});
            } else {
                c.unrepresentable_value = true;
            }
            p += k;
            matched = true;
            break;
        }
        if (!matched) ++p;
    }

    // ---- concepts, negation words and unmatched text ----
    u32 group = 0u;
    for (const Item& item : items) {
        if (item.consumed) continue;
        if (item.kind == ItemKind::Concept) {
            u32 count = 0u;
            const LexiconEntry* entries = pack.lexicon_entries(item.pattern, count);
            for (u32 e = 0u; e < count; ++e) {
                const ConceptInfo* info = common.concept_info(entries[e].concept_id);
                if (info == nullptr) continue;   // unreachable: validated at load
                c.concepts.push_back(ConceptMatch{group, info->id, info->slot, info->concept_class, info->categories,
                                                  entries[e].form_class, entries[e].origin, item.begin, item.end,
                                                  m.source_of(item.begin, item.end)});
            }
            ++group;
            continue;
        }
        if (item.kind == ItemKind::Negation) {
            c.negations.push_back(m.source_of(item.begin, item.end));
            continue;
        }
        c.unmatched.push_back(TextToken{m.text.substr(item.begin, item.end - item.begin), item.begin, item.end,
                                        m.source_of(item.begin, item.end)});
    }

    assign_slots(c, kConceptSlotMask);
    return c;
}

}  // namespace

void assign_slots(ClauseExtraction& clause, u8 expected_slots) {
    std::vector<u16> candidates[kConceptSlotCount];
    bool ambiguous[kConceptSlotCount] = {};

    for (std::size_t i = 0u; i < clause.concepts.size();) {
        std::size_t j = i;
        while (j < clause.concepts.size() && clause.concepts[j].group == clause.concepts[i].group) ++j;

        // The options this one surface form offers among the expected slots.
        u8 option_slots = 0u;
        u32 options = 0u;
        u8 slot = 0u;
        u16 value = 0u;
        for (std::size_t k = i; k < j; ++k) {
            const ConceptMatch& m = clause.concepts[k];
            if (m.slot == kNoSlot || ((expected_slots >> m.slot) & 1u) == 0u) continue;
            option_slots = static_cast<u8>(option_slots | (1u << m.slot));
            slot = m.slot;
            value = m.concept_id;
            ++options;
        }
        if (options == 1u) {
            candidates[slot].push_back(value);
        } else if (options > 1u) {
            // A homograph the expected slots do not settle: every slot it could
            // fill is untrustworthy.
            for (u32 s = 0u; s < kConceptSlotCount; ++s) {
                if (((option_slots >> s) & 1u) != 0u) ambiguous[s] = true;
            }
        }
        i = j;
    }
    for (const TypedValue& v : clause.values) {
        if (v.slot < kConceptSlotCount && ((expected_slots >> v.slot) & 1u) != 0u) {
            candidates[v.slot].push_back(static_cast<u16>(v.value));
        }
    }

    clause.ambiguous_slots = 0u;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        std::vector<u16>& values = candidates[s];
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        if (ambiguous[s] || values.size() > 1u) {
            clause.slots[s] = SlotCandidate{SlotState::Ambiguous, 0u};
            clause.ambiguous_slots = static_cast<u8>(clause.ambiguous_slots | (1u << s));
        } else if (values.size() == 1u) {
            clause.slots[s] = SlotCandidate{SlotState::Value, values[0]};
        } else {
            clause.slots[s] = SlotCandidate{SlotState::None, 0u};
        }
    }
}

void extract_utterance(const LanguagePack& pack, const CommonPack& common, const u8* input, std::size_t length,
                       UtteranceExtraction& out) {
    out = UtteranceExtraction{};
    out.normalized = normalize_utterance(input, length, pack.rules());
    for (const ClauseSpan& span : segment_clauses(out.normalized, pack.rules())) {
        out.clauses.push_back(extract_clause(pack, common, out.normalized, span));
    }
}

}  // namespace itantra
