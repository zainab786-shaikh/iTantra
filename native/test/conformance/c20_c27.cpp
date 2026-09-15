// Conformance — C-20 … C-24, C-27. Implementation plan Phase 6.
//
//   C-20 [H]  fixture languages, no crash, every utterance handled
//   C-21 [H]  code-mixed: loanword and native word → same concept ID
//   C-22 [H]  digits, native digits, number words → correct value; exact round trip
//   C-23 [H]  inflected forms → correct concept, correct output form
//   C-24 [H]  unknown names → literal, exact round-trip, any script
//   C-27 [H]  two NFC forms of one word → same lexicon entry
//
// Phase 6 scope, per the plan: these run on the SYNTHETIC fixture (Hindi,
// Tamil, English). The contract's wording of C-20 ("all 10 languages, full
// corpus … every utterance produces a decodable packet") also needs the real
// corpus and the Tier 2 encoder (Phase 7); C-25 and C-26 need tier selection.
//
// Also here:
//   - the Phase 6 exit criterion "zero language-specific code paths — differences
//     live in data only", checked over native/src/lang
//   - cross-language Tier 1 rendering on the host: a frame extracted in one
//     language renders in each listener's language. C-08 itself is [P] (Phase 12).
//
//   c20_c27_test --packs <compiled fixture dir> --fixtures <fixture source dir>
//                --lang-sources <native/src/lang files...>

#include "lang/extract.h"
#include "lang/languages.h"
#include "lang/normalize.h"
#include "lang/render.h"
#include "lang_fixture.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace itantra;
using langfx::cps;

namespace {

langfx::Fixture& fx() {
    static langfx::Fixture f;
    return f;
}

std::vector<std::string>& lang_sources() {
    static std::vector<std::string> files;
    return files;
}

struct XorShift32 {
    u32 s;
    u32 next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

struct ClauseExpect {
    std::map<u8, u16>        concepts;
    std::map<u8, u32>        values;
    u8                       ambiguous = 0u;
    std::vector<std::string> literals;
    bool                     none = false;
    bool                     unrep = false;
};

struct CorpusRow {
    std::string               id;
    std::string               lang;
    std::vector<std::string>  flags;
    std::string               group;
    std::string               text;
    std::vector<ClauseExpect> expect;
};

u8 slot_named(const std::string& name) {
    u8 s = 0xFFu;
    slot_from_name(name, s);
    return s;
}

std::vector<CorpusRow> corpus() {
    std::vector<CorpusRow> rows;
    for (const auto& cols : langfx::read_tsv(fx().src_dir + "/corpus/utterances.tsv")) {
        CorpusRow r;
        r.id = cols.at(0);
        r.lang = cols.at(1);
        r.flags = langfx::split(cols.at(2), ",");
        r.group = cols.at(3);
        r.text = cols.at(4);
        for (const std::string& clause : langfx::split(cols.at(5), " | ")) {
            ClauseExpect e;
            for (const std::string& item : langfx::split(clause, "; ")) {
                if (item == "none") {
                    e.none = true;
                } else if (item == "unrep") {
                    e.unrep = true;
                } else if (item.rfind("amb=", 0u) == 0u) {
                    e.ambiguous = static_cast<u8>(e.ambiguous | (1u << slot_named(item.substr(4u))));
                } else if (item.rfind("lit=", 0u) == 0u) {
                    e.literals.push_back(item.substr(4u));
                } else if (item.find('#') != std::string::npos) {
                    const std::size_t at = item.find('#');
                    e.values[slot_named(item.substr(0u, at))] = static_cast<u32>(std::stoul(item.substr(at + 1u)));
                } else {
                    const std::size_t at = item.find('=');
                    e.concepts[slot_named(item.substr(0u, at))] = fx().concepts.at(item.substr(at + 1u));
                }
            }
            r.expect.push_back(e);
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

bool has_flag(const CorpusRow& r, const char* flag) {
    for (const std::string& f : r.flags) {
        if (f == flag) return true;
    }
    return false;
}

// Some run of consecutive unmatched tokens covers exactly `literal` in the input.
bool literal_present(const ClauseExtraction& c, const std::string& input, const std::string& literal) {
    for (std::size_t i = 0u; i < c.unmatched.size(); ++i) {
        u32 begin = c.unmatched[i].source.begin;
        u32 end = c.unmatched[i].source.end;
        for (std::size_t j = i; j < c.unmatched.size(); ++j) {
            begin = std::min(begin, c.unmatched[j].source.begin);
            end = std::max(end, c.unmatched[j].source.end);
            if (input.compare(begin, end - begin, literal) == 0) return true;
        }
    }
    return false;
}

std::string describe(const ClauseExtraction& c) {
    std::string s = "[" + c.match_text + "]";
    for (u32 slot = 0u; slot < kConceptSlotCount; ++slot) {
        if (c.slots[slot].state == SlotState::Value) {
            s += std::string(" ") + slot_name(static_cast<u8>(slot)) + "=" + std::to_string(c.slots[slot].value);
        } else if (c.slots[slot].state == SlotState::Ambiguous) {
            s += std::string(" ") + slot_name(static_cast<u8>(slot)) + "=?";
        }
    }
    return s;
}

bool clause_matches(const ClauseExtraction& c, const ClauseExpect& e, const std::string& input) {
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const u8 slot = static_cast<u8>(s);
        const SlotCandidate& got = c.slots[s];
        if (((e.ambiguous >> s) & 1u) != 0u) {
            if (got.state != SlotState::Ambiguous) return false;
        } else if (e.concepts.count(slot) != 0u) {
            if (got.state != SlotState::Value || got.value != e.concepts.at(slot)) return false;
        } else if (e.values.count(slot) != 0u) {
            if (got.state != SlotState::Value || got.value != e.values.at(slot)) return false;
        } else if (got.state != SlotState::None) {
            return false;
        }
    }
    if (e.none != c.nothing_matched()) return false;
    if (e.unrep != c.unrepresentable_value) return false;
    for (const std::string& lit : e.literals) {
        if (!literal_present(c, input, lit)) return false;
    }
    return true;
}

bool row_matches(const CorpusRow& r) {
    const UtteranceExtraction x = fx().extract(r.lang, r.text);
    bool ok = x.clauses.size() == r.expect.size();
    for (std::size_t i = 0u; ok && i < x.clauses.size(); ++i) ok = clause_matches(x.clauses[i], r.expect[i], r.text);
    if (!ok) {
        std::printf("  %s (%s): got %zu clause(s):", r.id.c_str(), r.lang.c_str(), x.clauses.size());
        for (const ClauseExtraction& c : x.clauses) std::printf(" %s", describe(c).c_str());
        std::printf("\n");
    }
    return ok;
}

// Structural guarantees for ANY input: spans in bounds, clauses ordered and
// disjoint, everything a clause reports lies inside it, blank input → no clause.
bool structure_ok(const UtteranceExtraction& x, const std::string& input) {
    u32 last = 0u;
    for (const ClauseExtraction& c : x.clauses) {
        if (c.source.begin < last || c.source.begin >= c.source.end || c.source.end > input.size()) return false;
        last = c.source.end;
        for (const ConceptMatch& m : c.concepts) {
            if (m.source.begin < c.source.begin || m.source.end > c.source.end || m.begin >= m.end) return false;
        }
        for (const TextToken& t : c.unmatched) {
            if (t.source.begin < c.source.begin || t.source.end > c.source.end || t.text.empty()) return false;
        }
        for (const TypedValue& v : c.values) {
            if (v.source.begin < c.source.begin || v.source.end > c.source.end || v.slot >= kConceptSlotCount) return false;
        }
        for (u32 s = 0u; s < kConceptSlotCount; ++s) {
            const bool amb = ((c.ambiguous_slots >> s) & 1u) != 0u;
            if (amb != (c.slots[s].state == SlotState::Ambiguous)) return false;
        }
    }
    bool blank = true;
    for (char ch : input) blank = blank && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    return !blank || x.clauses.empty();
}

// Lexicon source rows of a fixture language: surface, concept, form_class, origin.
std::vector<std::vector<std::string>> lexicon_rows(const std::string& lang) {
    return langfx::read_tsv(fx().src_dir + "/lang/" + lang + "/lexicon.tsv");
}

std::string surface_for(const std::string& lang, const std::string& concept_name) {
    for (const auto& row : lexicon_rows(lang)) {
        if (row.at(1) == concept_name) return row.at(0);
    }
    return std::string();
}

bool extracts_concept(const std::string& lang, const std::string& text, u16 concept_id, std::string* match_text = nullptr) {
    const UtteranceExtraction x = fx().extract(lang, text);
    if (x.clauses.size() != 1u) return false;
    if (match_text != nullptr) *match_text = x.clauses[0].match_text;
    for (const ConceptMatch& m : x.clauses[0].concepts) {
        if (m.concept_id == concept_id && m.begin == 0u && m.end == x.clauses[0].match_text.size()) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

struct RenderRow {
    std::string intent;
    std::string slots_spec;
    std::string lang;
    std::string expected;
};

std::vector<RenderRow> renderings() {
    std::vector<RenderRow> rows;
    for (const auto& cols : langfx::read_tsv(fx().src_dir + "/corpus/renderings.tsv")) {
        rows.push_back(RenderRow{cols.at(0), cols.at(1), cols.at(2), cols.at(3)});
    }
    return rows;
}

void slots_from_spec(const std::string& spec, RenderSlot slots[kConceptSlotCount]) {
    for (u32 s = 0u; s < kConceptSlotCount; ++s) slots[s] = RenderSlot{};
    for (const std::string& item : langfx::split(spec, "; ")) {
        std::size_t at = item.find_first_of("=#~");
        const u8 slot = slot_named(item.substr(0u, at));
        const std::string value = item.substr(at + 1u);
        if (item[at] == '=') {
            slots[slot].kind = RenderKind::Concept;
            slots[slot].concept_id = fx().concepts.at(value);
        } else if (item[at] == '#') {
            slots[slot].kind = RenderKind::Number;
            slots[slot].number = static_cast<u32>(std::stoul(value));
        } else {
            slots[slot].kind = RenderKind::Literal;
            slots[slot].literal = value;
        }
    }
}

bool is_value_slot(u32 slot) {
    return slot == SLOT_QUANTITY || slot == SLOT_TIME;
}

// The spec string of an extracted clause, restricted to `mask`, in slot order.
std::string spec_of(const ClauseExtraction& c, u8 mask) {
    std::string spec;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (((mask >> s) & 1u) == 0u || c.slots[s].state != SlotState::Value) continue;
        if (!spec.empty()) spec += "; ";
        spec += slot_name(static_cast<u8>(s));
        if (is_value_slot(s)) {
            spec += "#" + std::to_string(c.slots[s].value);
        } else {
            for (const auto& kv : fx().concepts) {
                if (kv.second == c.slots[s].value) spec += "=" + kv.first;
            }
        }
    }
    return spec;
}

}  // namespace

// ---------------------------------------------------------------------------
// C-20
// ---------------------------------------------------------------------------

ITEST(C20_every_fixture_utterance_is_handled_in_every_fixture_pack) {
    const std::vector<CorpusRow> rows = corpus();
    ITEST_TRUE(rows.size() >= 40u);
    u32 runs = 0u;
    for (const CorpusRow& r : rows) {
        for (const std::string& lang : langfx::fixture_languages()) {   // including the "wrong" language (§11)
            const UtteranceExtraction x = fx().extract(lang, r.text);
            ITEST_TRUE(structure_ok(x, r.text));
            ITEST_TRUE(!x.clauses.empty());
            ++runs;
        }
    }
    std::printf("  C-20: %zu utterances x 3 packs = %u extractions, all structurally sound\n", rows.size(), runs);
}

ITEST(C20_adversarial_input_never_crashes_and_is_handled) {
    std::vector<std::string> inputs = {
        "", " ", "\t\n  \r", "!!!", ",", ".,;!?", cps({0x0964, 0x0964}), "a", "and", "and and and",
        std::string(1u, static_cast<char>(0xFF)),
        std::string{static_cast<char>(0xC0), static_cast<char>(0x80)},                          // overlong
        std::string{static_cast<char>(0xE0), static_cast<char>(0xA4)},                          // truncated
        std::string{static_cast<char>(0x80), static_cast<char>(0x80)},                          // stray continuation
        std::string{static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80)},  // surrogate
        std::string("a\0b", 3u),
        cps({0x0301, 0x0301, 0x093C}),                                                          // marks only
        cps({0x200D, 0x200C, 0xFEFF}),
        cps({0x1F691}) + " " + cps({0x706B}) + " " + cps({0x0627, 0x0644}),
        std::string(5000u, 'a'),
    };
    std::string repeated;
    for (u32 i = 0u; i < 2000u; ++i) repeated += cps({0x0906, 0x0917}) + ", ";
    inputs.push_back(repeated);

    for (const std::string& lang : langfx::fixture_languages()) {
        for (const std::string& s : inputs) ITEST_TRUE(structure_ok(fx().extract(lang, s), s));
    }

    XorShift32 rng{0xC20C20u};
    for (const std::string& lang : langfx::fixture_languages()) {
        for (u32 round = 0u; round < 2000u; ++round) {
            std::string s(rng.next() % 160u, '\0');
            for (char& ch : s) ch = static_cast<char>(rng.next());
            ITEST_TRUE(structure_ok(fx().extract(lang, s), s));
        }
    }
}

ITEST(corpus_utterances_extract_exactly_as_expected) {
    u32 checked = 0u;
    for (const CorpusRow& r : corpus()) {
        ITEST_TRUE(row_matches(r));
        ++checked;
    }
    std::printf("  corpus: %u utterances checked against their expectations\n", checked);
}

// ---------------------------------------------------------------------------
// C-21
// ---------------------------------------------------------------------------

ITEST(C21_code_mixed_loanwords_resolve_to_the_same_concept_ids_as_native_words) {
    const std::vector<CorpusRow> rows = corpus();
    u32 mixed = 0u;
    for (const CorpusRow& r : rows) {
        if (!has_flag(r, "code_mixed")) continue;
        ITEST_TRUE(row_matches(r));
        ++mixed;
    }
    ITEST_TRUE(mixed >= 5u);

    // Same group → same first-clause slots, across languages and scripts.
    std::map<std::string, std::string> group_spec;
    for (const CorpusRow& r : rows) {
        if (r.group == "-") continue;
        const UtteranceExtraction x = fx().extract(r.lang, r.text);
        ITEST_TRUE(!x.clauses.empty());
        if (x.clauses.empty()) continue;
        const std::string spec = spec_of(x.clauses[0], kConceptSlotMask);
        if (group_spec.count(r.group) == 0u) group_spec[r.group] = spec;
        ITEST_TRUE(group_spec[r.group] == spec);
        if (group_spec[r.group] != spec) std::printf("  %s: %s vs %s\n", r.id.c_str(), spec.c_str(), group_spec[r.group].c_str());
    }
    ITEST_TRUE(group_spec.size() >= 3u);

    // Every loanword entry, and a native word for the same concept in the same
    // pack, extract to one concept ID. Coverage: each fixture language has at
    // least one such pair.
    u32 pairs = 0u;
    for (const std::string& lang : langfx::fixture_languages()) {
        u32 lang_pairs = 0u;
        for (const auto& loan : lexicon_rows(lang)) {
            if (loan.at(3) != "loanword") continue;
            const u16 id = fx().concepts.at(loan.at(1));
            ITEST_TRUE(extracts_concept(lang, loan.at(0), id));
            for (const auto& native : lexicon_rows(lang)) {
                if (native.at(1) == loan.at(1) && native.at(3) == "native") {
                    ITEST_TRUE(extracts_concept(lang, native.at(0), id));
                    ++lang_pairs;
                    break;
                }
            }
        }
        ITEST_TRUE(lang_pairs >= 1u);
        if (lang_pairs == 0u) std::printf("  %s has no loanword/native pair\n", lang.c_str());
        pairs += lang_pairs;
    }
    std::printf("  C-21: %u code-mixed utterances, %zu cross-language groups, %u loanword/native pairs\n", mixed,
                group_spec.size(), pairs);
}

// ---------------------------------------------------------------------------
// C-22
// ---------------------------------------------------------------------------

ITEST(C22_digits_native_digits_and_number_words_extract_the_right_value) {
    u32 checked = 0u;
    for (const CorpusRow& r : corpus()) {
        if (has_flag(r, "number")) {
            ITEST_TRUE(row_matches(r));
            ++checked;
        }
    }
    for (const std::string& lang : langfx::fixture_languages()) {
        const LanguagePack& pack = fx().pack(lang);
        auto quantity = [&](const std::string& text, u32& value) {
            const UtteranceExtraction x = fx().extract(lang, text);
            if (x.clauses.size() != 1u || x.clauses[0].slots[SLOT_QUANTITY].state != SlotState::Value) return false;
            value = x.clauses[0].slots[SLOT_QUANTITY].value;
            return true;
        };
        // number words
        for (const auto& row : langfx::read_tsv(fx().src_dir + "/lang/" + lang + "/numbers.tsv")) {
            u32 v = 0u;
            ITEST_TRUE(quantity(row.at(0), v) && v == std::stoul(row.at(1)));
            ++checked;
        }
        // ASCII digits
        for (u32 n = 1u; n <= 300u; ++n) {
            u32 v = 0u;
            ITEST_TRUE(quantity(std::to_string(n), v) && v == n);
            ++checked;
        }
        // native digits, from the pack's own digit set
        if (pack.rules().primary_digits.size() == 10u) {
            const u32 samples[] = {1u, 3u, 9u, 10u, 42u, 100u, 999u, 4096u, 65535u};
            for (u32 n : samples) {
                std::string text;
                for (char d : std::to_string(n)) utf8::append(text, pack.rules().primary_digits[static_cast<std::size_t>(d - '0')]);
                u32 v = 0u;
                ITEST_TRUE(quantity(text, v) && v == n);
                ++checked;
            }
        }
        // out of the representable range: detected, never wrapped or guessed
        for (const char* bad : {"0", "65536", "999999999"}) {
            const UtteranceExtraction x = fx().extract(lang, bad);
            ITEST_TRUE(x.clauses.size() == 1u && x.clauses[0].unrepresentable_value &&
                       x.clauses[0].slots[SLOT_QUANTITY].state == SlotState::None);
        }
    }
    std::printf("  C-22: %u value extractions\n", checked);
}

ITEST(C22_numbers_round_trip_exactly_through_render_and_extract) {
    const u16 answer = fx().intents.at("ANSWER_COUNT");
    const u16 casualty = fx().intents.at("REPORT_CASUALTY_COUNT");
    const u32 samples[] = {1u, 3u, 7u, 10u, 42u, 100u, 999u, 4096u, 65535u};
    u32 trips = 0u;
    for (const std::string& lang : langfx::fixture_languages()) {
        for (u16 intent : {answer, casualty}) {
            for (u32 n : samples) {
                RenderSlot slots[kConceptSlotCount];
                slots[SLOT_QUANTITY].kind = RenderKind::Number;
                slots[SLOT_QUANTITY].number = n;
                std::string text;
                u8 missing = 0u;
                ITEST_TRUE(render_frame(fx().pack(lang), intent, slots, text, missing) == RenderStatus::Ok);
                const UtteranceExtraction x = fx().extract(lang, text);
                ITEST_TRUE(x.clauses.size() == 1u && x.clauses[0].slots[SLOT_QUANTITY].state == SlotState::Value &&
                           x.clauses[0].slots[SLOT_QUANTITY].value == n);
                ++trips;
            }
        }
    }
    std::printf("  C-22: %u render → extract round trips\n", trips);
}

// ---------------------------------------------------------------------------
// C-23
// ---------------------------------------------------------------------------

ITEST(C23_inflected_forms_resolve_to_their_concept) {
    u32 checked = 0u;
    for (const CorpusRow& r : corpus()) {
        if (has_flag(r, "inflected")) {
            ITEST_TRUE(row_matches(r));
            ++checked;
        }
    }
    for (const std::string& lang : langfx::fixture_languages()) {
        for (const auto& row : lexicon_rows(lang)) {
            if (row.at(2) != "inflected") continue;
            ITEST_TRUE(extracts_concept(lang, row.at(0), fx().concepts.at(row.at(1))));
            ++checked;
        }
    }
    ITEST_TRUE(checked >= 20u);
    std::printf("  C-23: %u inflected-form checks\n", checked);
}

ITEST(C23_frames_render_with_the_form_each_slot_position_requires) {
    u32 rendered = 0u;
    for (const RenderRow& row : renderings()) {
        RenderSlot slots[kConceptSlotCount];
        slots_from_spec(row.slots_spec, slots);
        std::string text;
        u8 missing = 0u;
        const RenderStatus status = render_frame(fx().pack(row.lang), fx().intents.at(row.intent), slots, text, missing);
        ITEST_TRUE(status == RenderStatus::Ok);
        ITEST_TRUE(text == row.expected);
        if (text != row.expected) std::printf("  %s/%s: got '%s'\n", row.intent.c_str(), row.lang.c_str(), text.c_str());

        // What was rendered reads back to the same frame in that language.
        const UtteranceExtraction x = fx().extract(row.lang, text);
        ITEST_EQ(x.clauses.size(), 1u);
        if (x.clauses.size() == 1u) {
            for (u32 s = 0u; s < kConceptSlotCount; ++s) {
                if (slots[s].kind == RenderKind::Concept) {
                    ITEST_TRUE(x.clauses[0].slots[s].state == SlotState::Value && x.clauses[0].slots[s].value == slots[s].concept_id);
                } else if (slots[s].kind == RenderKind::Number) {
                    ITEST_TRUE(x.clauses[0].slots[s].state == SlotState::Value && x.clauses[0].slots[s].value == slots[s].number);
                } else if (slots[s].kind == RenderKind::Literal) {
                    ITEST_TRUE(literal_present(x.clauses[0], text, slots[s].literal));
                }
            }
        }
        ++rendered;
    }
    ITEST_TRUE(rendered >= 20u);

    // Missing information is never invented.
    RenderSlot none[kConceptSlotCount];
    std::string text;
    u8 missing = 0u;
    ITEST_TRUE(render_frame(fx().pack("hi"), fx().intents.at("REPORT_FIRE_AT"), none, text, missing) == RenderStatus::MissingSlot);
    ITEST_EQ(missing, 1u << SLOT_LOCATION);
    ITEST_TRUE(text.empty());
    RenderSlot wrong[kConceptSlotCount];
    wrong[SLOT_LOCATION].kind = RenderKind::Concept;
    wrong[SLOT_LOCATION].concept_id = fx().concepts.at("ambulance");   // has no locative form
    ITEST_TRUE(render_frame(fx().pack("ta"), fx().intents.at("REPORT_FIRE_AT"), wrong, text, missing) == RenderStatus::MissingForm);
    wrong[SLOT_LOCATION].kind = RenderKind::Number;
    ITEST_TRUE(render_frame(fx().pack("en"), fx().intents.at("REPORT_FIRE_AT"), wrong, text, missing) == RenderStatus::WrongForm);
    ITEST_TRUE(render_frame(fx().pack("en"), 999u, wrong, text, missing) == RenderStatus::NoTemplate);
    std::printf("  C-23: %u renderings exact and read back\n", rendered);
}

// ---------------------------------------------------------------------------
// C-24
// ---------------------------------------------------------------------------

ITEST(C24_unknown_names_become_literals_that_round_trip_exactly_in_any_script) {
    u32 checked = 0u;
    for (const CorpusRow& r : corpus()) {
        if (has_flag(r, "literal")) {
            ITEST_TRUE(row_matches(r));
            ++checked;
        }
    }
    const std::vector<std::string> names = {
        cps({0x0930, 0x092E, 0x0947, 0x0936}),                           // Devanagari
        cps({0x0BAE, 0x0BC1, 0x0BB0, 0x0BC1, 0x0B95, 0x0BA9, 0x0BCD}),   // Tamil
        cps({0x0985, 0x09AE, 0x09BF, 0x09A4}),                           // Bengali
        cps({0x0AB0, 0x0AAE, 0x0AC7, 0x0AB6}),                           // Gujarati
        cps({0x0C30, 0x0C2E, 0x0C47, 0x0C37, 0x0C4D}),                   // Telugu
        cps({0x0D30, 0x0D2E, 0x0D47, 0x0D36, 0x0D4D}),                   // Malayalam
        cps({0x0CB0, 0x0CAE, 0x0CC7, 0x0CB6, 0x0CCD}),                   // Kannada
        cps({0x0B30, 0x0B2E, 0x0B47, 0x0B36}),                           // Odia
        "Ravi",                                                          // Latin
        "Jos" + cps({0x0065, 0x0301}),                                   // Latin, NFD — kept as spoken
        cps({0x0639, 0x0644, 0x064A}),                                   // Arabic
        cps({0x674E, 0x660E}),                                           // CJK
        cps({0x1F691}),                                                  // emoji
    };
    const u16 move = fx().intents.at("REQUEST_MOVE");
    for (const std::string& lang : langfx::fixture_languages()) {
        const std::string verb = surface_for(lang, "move");
        ITEST_TRUE(!verb.empty());
        for (const std::string& name : names) {
            // speaker says "<name> <move>": the name survives byte for byte
            const std::string spoken = name + " " + verb;
            const UtteranceExtraction x = fx().extract(lang, spoken);
            ITEST_TRUE(x.clauses.size() == 1u && literal_present(x.clauses[0], spoken, name));

            // rendered as a literal in the listener's sentence, still byte for byte
            RenderSlot slots[kConceptSlotCount];
            slots[SLOT_ACTOR].kind = RenderKind::Literal;
            slots[SLOT_ACTOR].literal = name;
            std::string text;
            u8 missing = 0u;
            ITEST_TRUE(render_frame(fx().pack(lang), move, slots, text, missing) == RenderStatus::Ok);
            ITEST_TRUE(text.find(name) != std::string::npos);
            const UtteranceExtraction back = fx().extract(lang, text);
            ITEST_TRUE(back.clauses.size() == 1u && literal_present(back.clauses[0], text, name));
            ++checked;
        }
    }
    std::printf("  C-24: %u literal checks across %zu scripts\n", checked, names.size());
}

// ---------------------------------------------------------------------------
// C-27
// ---------------------------------------------------------------------------

ITEST(C27_nfc_and_nfd_forms_of_every_lexicon_word_match_the_same_entry) {
    u32 words = 0u;
    for (const std::string& lang : langfx::fixture_languages()) {
        for (const auto& row : lexicon_rows(lang)) {
            const u16 id = fx().concepts.at(row.at(1));
            const std::string composed = unicode::nfc(row.at(0));
            const std::string decomposed = unicode::nfd(row.at(0));
            std::string text_nfc, text_nfd;
            ITEST_TRUE(extracts_concept(lang, composed, id, &text_nfc));
            ITEST_TRUE(extracts_concept(lang, decomposed, id, &text_nfd));
            ITEST_TRUE(text_nfc == text_nfd);
            ++words;
        }
    }
    // Encodings that differ on screen-identical text, explicitly.
    const u16 market = fx().concepts.at("market");
    const u16 flood = fx().concepts.at("flood");
    const u16 police = fx().concepts.at("police");
    //   bazaar with U+095B (a composition exclusion) vs U+091C U+093C
    ITEST_TRUE(extracts_concept("hi", cps({0x092C, 0x093E, 0x095B, 0x093E, 0x0930}), market));
    ITEST_TRUE(extracts_concept("hi", cps({0x092C, 0x093E, 0x091C, 0x093C, 0x093E, 0x0930}), market));
    //   flood with U+095D (RHA, a composition exclusion) vs U+0922 U+093C
    ITEST_TRUE(extracts_concept("hi", cps({0x092C, 0x093E, 0x095D}), flood));
    ITEST_TRUE(extracts_concept("hi", cps({0x092C, 0x093E, 0x0922, 0x093C}), flood));
    //   police with U+0BCB vs U+0BC7 U+0BBE
    ITEST_TRUE(extracts_concept("ta", cps({0x0BAA, 0x0BCB, 0x0BB2, 0x0BC0, 0x0BB8, 0x0BCD}), police));
    ITEST_TRUE(extracts_concept("ta", cps({0x0BAA, 0x0BC7, 0x0BBE, 0x0BB2, 0x0BC0, 0x0BB8, 0x0BCD}), police));
    //   cafe with U+00E9 vs e U+0301, and in capitals
    ITEST_TRUE(extracts_concept("en", "caf" + cps({0x00E9}), market));
    ITEST_TRUE(extracts_concept("en", "cafe" + cps({0x0301}), market));
    ITEST_TRUE(extracts_concept("en", "CAFE" + cps({0x0301}), market));
    std::printf("  C-27: %u lexicon words matched in both NFC and NFD, plus explicit variants\n", words);
}

// ---------------------------------------------------------------------------
// Cross-language Tier 1 (host precondition for C-08, which is [P])
// ---------------------------------------------------------------------------

ITEST(frames_extracted_in_one_language_render_in_every_listeners_language) {
    struct Case {
        const char* corpus_id;
        const char* intent;
    };
    const Case cases[] = {{"h1", "REPORT_FIRE_AT"},        {"t1", "REPORT_FIRE_AT"},       {"e1", "REPORT_FIRE_AT"},
                          {"h4", "REQUEST_MEDICAL_AT"},    {"t4", "REQUEST_MEDICAL_AT"},   {"e2", "REQUEST_MEDICAL_AT"},
                          {"h5", "REPORT_CASUALTY_COUNT"}, {"t6", "REPORT_CASUALTY_COUNT"}, {"e3", "REPORT_CASUALTY_COUNT"},
                          {"e7", "REQUEST_MOVE"}};
    const std::vector<CorpusRow> rows = corpus();
    const std::vector<RenderRow> expected = renderings();
    u32 rendered = 0u;
    for (const Case& k : cases) {
        const CorpusRow* row = nullptr;
        for (const CorpusRow& r : rows) {
            if (r.id == k.corpus_id) row = &r;
        }
        ITEST_TRUE(row != nullptr);
        if (row == nullptr) continue;
        const UtteranceExtraction x = fx().extract(row->lang, row->text);
        ITEST_EQ(x.clauses.size(), 1u);
        if (x.clauses.empty()) continue;
        const ClauseExtraction& c = x.clauses[0];
        const IntentInfo* intent = fx().common.intent(fx().intents.at(k.intent));

        // The frame: language-neutral IDs and values only (§2.1).
        RenderSlot slots[kConceptSlotCount];
        for (u32 s = 0u; s < kConceptSlotCount; ++s) {
            if (c.slots[s].state != SlotState::Value) continue;
            if (is_value_slot(s)) {
                slots[s].kind = RenderKind::Number;
                slots[s].number = c.slots[s].value;
            } else {
                slots[s].kind = RenderKind::Concept;
                slots[s].concept_id = c.slots[s].value;
            }
        }
        const std::string spec = spec_of(c, intent->required_slots);
        for (const std::string& listener : langfx::fixture_languages()) {
            std::string text;
            u8 missing = 0u;
            ITEST_TRUE(render_frame(fx().pack(listener), intent->id, slots, text, missing) == RenderStatus::Ok);
            bool found = false;
            for (const RenderRow& e : expected) {
                if (e.intent == k.intent && e.lang == listener && e.slots_spec == spec) {
                    found = true;
                    ITEST_TRUE(text == e.expected);
                }
            }
            ITEST_TRUE(found);
            if (!found) std::printf("  no expected rendering for %s %s [%s]\n", k.intent, listener.c_str(), spec.c_str());
            ++rendered;
        }
    }
    std::printf("  cross-language: %zu frames x 3 listener languages = %u renderings\n",
                sizeof(cases) / sizeof(cases[0]), rendered);
}

// ---------------------------------------------------------------------------
// Exit criterion: zero language-specific code paths (spec L9)
// ---------------------------------------------------------------------------

ITEST(language_layer_source_has_no_language_specific_code_paths) {
    ITEST_TRUE(lang_sources().size() >= 15u);
    std::size_t count = 0u;
    const SupportedLanguage* languages = supported_languages(count);
    ITEST_EQ(count, 10u);
    bool saw_registry = false;
    for (const std::string& path : lang_sources()) {
        std::string text;
        ITEST_TRUE(langfx::read_file(path, text));
        const bool registry = path.size() >= 13u && path.compare(path.size() - 13u, 13u, "languages.cpp") == 0;
        saw_registry = saw_registry || registry;

        // no script text: every language-specific string lives in pack data
        const auto* data = reinterpret_cast<const u8*>(text.data());
        for (std::size_t i = 0u; i < text.size();) {
            u32 cp = 0u;
            bool valid = true;
            i += utf8::decode(data, text.size(), i, cp, valid);
            const bool script_text = (cp >= 0x0370u && cp <= 0x1FFFu) || (cp >= 0x2E80u && cp <= 0xD7FFu) ||
                                     (cp >= 0xF900u && cp <= 0xFFEFu) || cp >= 0x10000u;
            ITEST_TRUE(valid && !script_text);
            if (!valid || script_text) {
                std::printf("  %s: script character U+%04X\n", path.c_str(), cp);
                break;
            }
        }
        // no branching on a language: codes appear only in the registry
        if (!registry) {
            for (std::size_t l = 0u; l < count; ++l) {
                const std::string quoted = std::string("\"") + languages[l].code + "\"";
                ITEST_TRUE(text.find(quoted) == std::string::npos);
                if (text.find(quoted) != std::string::npos) std::printf("  %s mentions %s\n", path.c_str(), quoted.c_str());
            }
        }
    }
    ITEST_TRUE(saw_registry);
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--packs") == 0 && a + 1 < argc) {
            packs = argv[++a];
        } else if (std::strcmp(argv[a], "--fixtures") == 0 && a + 1 < argc) {
            fixtures = argv[++a];
        } else if (std::strcmp(argv[a], "--lang-sources") == 0) {
            while (a + 1 < argc) lang_sources().push_back(argv[++a]);
        }
    }
    if (!fx().load(packs, fixtures)) {
        std::printf("fixture not loaded: %s\n", fx().error.c_str());
        return 1;
    }
    std::printf("C-25 [H] not in Phase 6: low-confidence STT needs tier selection (Phase 9)\n");
    std::printf("C-26 [H] not in Phase 6: priority round trip needs Tier 1 and tier selection\n");
    return ::itest::run_all("conformance.c20_c27");
}
