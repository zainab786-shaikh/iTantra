// Unit tests — lang/normalize, lang/clause, lang/json. Implementation plan Phase 6.
//
//   unit  normalisation order exactly §6.1 — punctuation survives to step 4
//
// Plus: Unicode 16.0.0 NFC conformance (subset of NormalizationTest.txt; the
// full file is run separately), digit unification, whitespace collapse,
// strip rules, case folding, punctuation stripping, source-byte mapping,
// invalid UTF-8, the normalize.json parser, the JSON reader, and clause
// segmentation.
//
//   normalize_test --nfc-subset <NormalizationTest-16.0.0-subset.txt>

#include "lang/clause.h"
#include "lang/json.h"
#include "lang/normalize.h"
#include "lang/utf8.h"
#include "lang_fixture.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace itantra;
using langfx::cps;

namespace {

std::string& nfc_subset_path() {
    static std::string p;
    return p;
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

NormalizeRules rules_from(const std::string& json) {
    NormalizeRules r;
    std::string error;
    const bool ok = parse_normalize_rules(reinterpret_cast<const u8*>(json.data()), json.size(), r, error);
    ITEST_TRUE(ok);
    if (!ok) std::printf("  rules: %s\n", error.c_str());
    return r;
}

bool rules_rejected(const std::string& json) {
    NormalizeRules r;
    std::string error;
    return !parse_normalize_rules(reinterpret_cast<const u8*>(json.data()), json.size(), r, error) && !error.empty();
}

// Devanagari digits, ". , ! ? DANDA" as clause punctuation, "and" and the
// Hindi conjunction U+0914 U+0930 as conjunctions.
NormalizeRules basic_rules() {
    const std::string digits = cps({0x0966, 0x0967, 0x0968, 0x0969, 0x096A, 0x096B, 0x096C, 0x096D, 0x096E, 0x096F});
    return rules_from("{\"digit_sets\":[\"" + digits + "\"],\"strip_codepoints\":[],"
                      "\"clause_punctuation\":[\".\",\",\",\"!\",\"?\",\"" + cps({0x0964}) + "\"],"
                      "\"clause_conjunctions\":[\"and\",\"" + cps({0x0914, 0x0930}) + "\"]}");
}

NormalizeRules no_rules() {
    return rules_from("{\"digit_sets\":[],\"strip_codepoints\":[],\"clause_punctuation\":[],\"clause_conjunctions\":[]}");
}

MappedText utter(const std::string& s, const NormalizeRules& r) {
    return normalize_utterance(reinterpret_cast<const u8*>(s.data()), s.size(), r);
}

std::vector<std::string> clauses_of(const std::string& s, const NormalizeRules& r) {
    const MappedText u = utter(s, r);
    std::vector<std::string> out;
    for (const ClauseSpan& c : segment_clauses(u, r)) out.push_back(u.text.substr(c.begin, c.end - c.begin));
    return out;
}

std::string seq(const std::string& col) {
    std::istringstream in(col);
    std::string tok, out;
    while (in >> tok) utf8::append(out, static_cast<u32>(std::stoul(tok, nullptr, 16)));
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Unicode
// ---------------------------------------------------------------------------

ITEST(nfc_matches_unicode_16_normalization_test_subset) {
    std::string text;
    ITEST_TRUE(langfx::read_file(nfc_subset_path(), text));
    std::istringstream in(text);
    std::string line;
    u32 lines = 0u, failures = 0u;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == '@') continue;
        const std::vector<std::string> c = langfx::split(line, ";");
        if (c.size() < 5u) continue;
        const std::string s1 = seq(c[0]), s2 = seq(c[1]), s3 = seq(c[2]), s4 = seq(c[3]), s5 = seq(c[4]);
        const bool ok = s2 == unicode::nfc(s1) && s2 == unicode::nfc(s2) && s2 == unicode::nfc(s3) &&
                        s4 == unicode::nfc(s4) && s4 == unicode::nfc(s5);
        if (!ok && failures++ < 5u) std::printf("  NFC mismatch: %s\n", line.c_str());
        ++lines;
    }
    ITEST_EQ(failures, 0u);
    ITEST_TRUE(lines > 3000u);
}

ITEST(unicode_properties_come_from_the_ucd_tables) {
    const u32 spaces[] = {0x0020, 0x0009, 0x000A, 0x000D, 0x00A0, 0x2003, 0x3000};
    for (u32 cp : spaces) ITEST_TRUE(unicode::is_white_space(cp));
    ITEST_TRUE(!unicode::is_white_space(0x200D));   // ZERO WIDTH JOINER is not whitespace
    ITEST_TRUE(!unicode::is_white_space(0x200B));
    ITEST_TRUE(!unicode::is_white_space('a'));

    const u32 punctuation[] = {'.', ',', '!', '?', '\'', '-', 0x0964, 0x0965, 0x3002};
    for (u32 cp : punctuation) ITEST_TRUE(unicode::is_punctuation(cp));
    ITEST_TRUE(!unicode::is_punctuation('$'));      // Sc, not P*
    ITEST_TRUE(!unicode::is_punctuation(0x0915));
    ITEST_TRUE(!unicode::is_punctuation(0x1F691));  // emoji: So

    ITEST_EQ(unicode::simple_case_fold('A'), static_cast<u32>('a'));
    ITEST_EQ(unicode::simple_case_fold(0x00C9), 0x00E9u);
    ITEST_EQ(unicode::simple_case_fold(0x0130), 0x0130u);   // status T only: unchanged
    ITEST_EQ(unicode::simple_case_fold(0x0915), 0x0915u);

    ITEST_EQ(unicode::combining_class(0x093C), 7u);   // nukta
    ITEST_EQ(unicode::combining_class(0x094D), 9u);   // virama
    ITEST_EQ(unicode::combining_class(0x0915), 0u);
}

ITEST(nfc_composes_and_decomposes_indic_script_specifics) {
    // Tamil vowel sign O: U+0BC6 U+0BBE → U+0BCA (both starters, class 0)
    ITEST_TRUE(unicode::nfc(cps({0x0B95, 0x0BC6, 0x0BBE})) == cps({0x0B95, 0x0BCA}));
    // Devanagari U+0958..095F are composition exclusions: NFC DECOMPOSES them
    ITEST_TRUE(unicode::nfc(cps({0x095B})) == cps({0x091C, 0x093C}));
    // U+0929 = U+0928 U+093C is a primary composite
    ITEST_TRUE(unicode::nfc(cps({0x0928, 0x093C})) == cps({0x0929}));
    // Kannada two-step: U+0CCA U+0CD5 → U+0CCB
    ITEST_TRUE(unicode::nfc(cps({0x0CC6, 0x0CC2, 0x0CD5})) == cps({0x0CCB}));
    // canonical ordering: nukta (7) before virama (9) whatever the input order
    ITEST_TRUE(unicode::nfc(cps({0x0915, 0x094D, 0x093C})) == unicode::nfc(cps({0x0915, 0x093C, 0x094D})));
    ITEST_TRUE(unicode::nfd(cps({0x00E9})) == cps({0x0065, 0x0301}));
    ITEST_TRUE(unicode::nfc(unicode::nfd(cps({0x0BAA, 0x0BCB}))) == cps({0x0BAA, 0x0BCB}));
}

// ---------------------------------------------------------------------------
// §6.1 order
// ---------------------------------------------------------------------------

ITEST(normalisation_order_punctuation_survives_until_clause_segmentation) {
    const NormalizeRules r = basic_rules();
    const std::string input = "Fire at gate,send help";

    // Steps 1–3 leave punctuation (and case) alone.
    const MappedText u = utter(input, r);
    ITEST_TRUE(u.text == "Fire at gate,send help");

    // Step 4 can therefore see the boundary.
    const std::vector<ClauseSpan> spans = segment_clauses(u, r);
    ITEST_EQ(spans.size(), 2u);
    ITEST_TRUE(u.text.substr(spans[0].begin, spans[0].end - spans[0].begin) == "Fire at gate,");
    ITEST_TRUE(u.text.substr(spans[1].begin, spans[1].end - spans[1].begin) == "send help");

    // Steps 5–6 per clause.
    ITEST_TRUE(normalize_clause(u, spans[0].begin, spans[0].end).text == "fire at gate");
    ITEST_TRUE(normalize_clause(u, spans[1].begin, spans[1].end).text == "send help");

    // Stripping punctuation first — the order the spec forbids — loses it.
    const std::string stripped = normalize_surface(reinterpret_cast<const u8*>(input.data()), input.size(), r);
    ITEST_TRUE(stripped == "fire at gate send help");
    ITEST_EQ(clauses_of(stripped, r).size(), 1u);
}

ITEST(step_2_unifies_the_packs_digits_only) {
    const NormalizeRules r = basic_rules();
    ITEST_TRUE(utter(cps({0x0969, 0x096A}) + " x", r).text == "34 x");
    ITEST_TRUE(utter(cps({0x0BE9}), r).text == cps({0x0BE9}));   // Tamil digit: not in these rules
    ITEST_EQ(r.primary_digits.size(), 10u);
    ITEST_EQ(r.primary_digits[7], 0x096Du);
}

ITEST(step_3_collapses_every_white_space_run_and_trims) {
    const NormalizeRules r = no_rules();
    ITEST_TRUE(utter("\t  a " + cps({0x00A0, 0x3000}) + " b  \n", r).text == "a b");
    const std::string zwj = "a" + cps({0x200D}) + "b";
    ITEST_TRUE(utter(zwj, r).text == zwj);   // ZWJ is not whitespace and is kept
    ITEST_TRUE(utter("   ", r).text.empty());
    ITEST_TRUE(utter("", r).text.empty());
}

ITEST(strip_codepoints_rule_removes_after_nfc) {
    const NormalizeRules r = rules_from("{\"digit_sets\":[],\"strip_codepoints\":[\"" + cps({0x200D}) +
                                        "\"],\"clause_punctuation\":[],\"clause_conjunctions\":[]}");
    ITEST_TRUE(utter("a" + cps({0x200D}) + "b", r).text == "ab");
}

ITEST(steps_5_and_6_fold_case_and_turn_punctuation_into_spaces) {
    const NormalizeRules r = basic_rules();
    MappedText u = utter("Police,MOVE now", r);
    ITEST_TRUE(normalize_clause(u, 0u, static_cast<u32>(u.text.size())).text == "police move now");
    u = utter("e-mail o'clock (now)", r);
    ITEST_TRUE(normalize_clause(u, 0u, static_cast<u32>(u.text.size())).text == "e mail o clock now");
    u = utter(cps({0x00C9}) + "COLE", r);
    ITEST_TRUE(normalize_clause(u, 0u, static_cast<u32>(u.text.size())).text == cps({0x00E9}) + "cole");
}

ITEST(every_output_byte_maps_back_to_the_input_bytes_it_came_from) {
    const NormalizeRules r = basic_rules();
    // "cafe" + U+0301 (NFD) → "caf" + U+00E9: the é covers input bytes 3..6
    const std::string input = "cafe" + cps({0x0301}) + "  " + cps({0x0969}) + " X";
    const MappedText u = utter(input, r);
    ITEST_TRUE(u.text == "caf" + cps({0x00E9}) + " 3 X");
    ITEST_EQ(u.source.size(), u.text.size());
    SourceSpan s = u.source_of(3u, 5u);
    ITEST_EQ(s.begin, 3u);
    ITEST_EQ(s.end, 6u);
    s = u.source_of(5u, 6u);               // collapsed "  "
    ITEST_EQ(s.begin, 6u);
    ITEST_EQ(s.end, 8u);
    s = u.source_of(6u, 7u);               // '3' from the 3-byte Devanagari digit
    ITEST_EQ(s.begin, 8u);
    ITEST_EQ(s.end, 11u);

    const MappedText c = normalize_clause(u, 0u, static_cast<u32>(u.text.size()));
    ITEST_TRUE(c.text == "caf" + cps({0x00E9}) + " 3 x");
    s = c.source_of(0u, 5u);
    ITEST_TRUE(input.substr(s.begin, s.end - s.begin) == "cafe" + cps({0x0301}));   // exact original bytes
}

ITEST(invalid_utf8_passes_through_unchanged_and_never_crashes) {
    const NormalizeRules r = basic_rules();
    const std::string bad = {static_cast<char>(0xFF), 'A', static_cast<char>(0xC3), ' ',
                             static_cast<char>(0xE0), static_cast<char>(0xA4)};
    const MappedText u = utter(bad, r);
    ITEST_TRUE(u.text == bad);
    for (u32 i = 0u; i < u.source.size(); ++i) {
        ITEST_EQ(u.source[i].begin, i);
        ITEST_EQ(u.source[i].end, i + 1u);
    }
    const MappedText c = normalize_clause(u, 0u, static_cast<u32>(u.text.size()));
    const std::string folded = {static_cast<char>(0xFF), 'a', static_cast<char>(0xC3), ' ',
                                static_cast<char>(0xE0), static_cast<char>(0xA4)};
    ITEST_TRUE(c.text == folded);

    XorShift32 rng{0xBADBEEFu};
    for (u32 round = 0u; round < 3000u; ++round) {
        std::string s(rng.next() % 200u, '\0');
        for (char& ch : s) ch = static_cast<char>(rng.next());
        const MappedText m = utter(s, r);
        ITEST_EQ(m.source.size(), m.text.size());
        for (const SourceSpan& span : m.source) ITEST_TRUE(span.begin < span.end && span.end <= s.size());
        u32 last = 0u;
        for (const ClauseSpan& cl : segment_clauses(m, r)) {
            ITEST_TRUE(cl.begin >= last && cl.begin < cl.end && cl.end <= m.text.size());
            last = cl.end;
            const MappedText n = normalize_clause(m, cl.begin, cl.end);
            ITEST_EQ(n.source.size(), n.text.size());
        }
    }
}

// ---------------------------------------------------------------------------
// normalize.json and JSON
// ---------------------------------------------------------------------------

ITEST(normalize_rules_reject_malformed_packs) {
    const std::string ok_tail = "\"strip_codepoints\":[],\"clause_punctuation\":[],\"clause_conjunctions\":[]";
    ITEST_TRUE(!rules_rejected("{\"digit_sets\":[]," + ok_tail + "}"));
    ITEST_TRUE(rules_rejected("{" + ok_tail + "}"));                                     // missing key
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[]," + ok_tail + ",\"extra\":[]}"));      // unknown key
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[\"012345678\"]," + ok_tail + "}"));      // nine digits
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[\"0123456789\",\"0123456789\"]," + ok_tail + "}"));   // duplicate
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[1]," + ok_tail + "}"));                  // not a string
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[],\"strip_codepoints\":[],\"clause_punctuation\":[\"ab\"],"
                              "\"clause_conjunctions\":[]}"));                            // not one codepoint
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[],\"strip_codepoints\":[],\"clause_punctuation\":[\".\",\".\"],"
                              "\"clause_conjunctions\":[]}"));                            // duplicate
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[],\"strip_codepoints\":[],\"clause_punctuation\":[],"
                              "\"clause_conjunctions\":[\"and then\"]}"));                // two tokens
    ITEST_TRUE(rules_rejected("{\"digit_sets\":[],\"strip_codepoints\":[],\"clause_punctuation\":[],"
                              "\"clause_conjunctions\":[\"  \"]}"));                      // empty token
    ITEST_TRUE(rules_rejected("[]"));
}

ITEST(json_reader_is_strict_and_integer_only) {
    auto parses = [](const std::string& s, JsonValue& v) {
        std::string error;
        return parse_json(reinterpret_cast<const u8*>(s.data()), s.size(), v, error);
    };
    JsonValue v;
    ITEST_TRUE(parses("{\"a\": [1, -2, 0, true, false, null, \"x\"], \"b\": {}}", v));
    ITEST_TRUE(v.kind == JsonValue::Kind::Object);
    ITEST_EQ(v.member("a")->items.size(), 7u);
    ITEST_TRUE(v.member("a")->items[1].integer == -2);
    ITEST_TRUE(v.member("missing") == nullptr);

    ITEST_TRUE(!parses("1.5", v));            // integer-only
    ITEST_TRUE(!parses("1e3", v));
    ITEST_TRUE(!parses("01", v));             // leading zero
    ITEST_TRUE(!parses("+1", v));
    ITEST_TRUE(!parses("{\"a\":1,\"a\":2}", v));   // duplicate key
    ITEST_TRUE(!parses("[1,]", v));
    ITEST_TRUE(!parses("{} x", v));           // trailing content
    ITEST_TRUE(!parses("\"a\tb\"", v));        // raw control character
    ITEST_TRUE(!parses(std::string("\"") + static_cast<char>(0xC0) + static_cast<char>(0x80) + "\"", v));   // overlong
    ITEST_TRUE(!parses("99999999999999999999", v));

    const std::string bs(1u, static_cast<char>(92));
    ITEST_TRUE(parses("\"" + bs + "u0041" + bs + "n\"", v));
    ITEST_TRUE(v.string == "A\n");
    ITEST_TRUE(parses("\"" + bs + "uD83D" + bs + "uDE91\"", v));   // surrogate pair → U+1F691
    ITEST_TRUE(v.string == cps({0x1F691}));
    ITEST_TRUE(!parses("\"" + bs + "uD83D\"", v));                 // lone surrogate
    ITEST_TRUE(!parses("\"" + bs + "q\"", v));

    std::string deep(100u, '[');
    deep += std::string(100u, ']');
    ITEST_TRUE(!parses(deep, v));             // nesting limit
}

// ---------------------------------------------------------------------------
// Step 4 — clause segmentation
// ---------------------------------------------------------------------------

ITEST(clause_segmentation_follows_the_fixed_rule) {
    const NormalizeRules r = basic_rules();
    auto is = [](const std::vector<std::string>& got, const std::vector<std::string>& want) { return got == want; };

    ITEST_TRUE(is(clauses_of("fire at gate, send help", r), {"fire at gate,", "send help"}));
    ITEST_TRUE(is(clauses_of("bridge blocked and send water", r), {"bridge blocked", "and send water"}));
    ITEST_TRUE(is(clauses_of("gate AND water", r), {"gate", "AND water"}));   // case-folded comparison
    ITEST_TRUE(is(clauses_of("Fire!!", r), {"Fire!!"}));                     // empty piece joins the clause before
    ITEST_TRUE(is(clauses_of("!!Fire", r), {"!!Fire"}));                     // ...or after, when first
    ITEST_TRUE(is(clauses_of("and send", r), {"and send"}));                 // a leading conjunction is not a cut
    ITEST_TRUE(is(clauses_of("sandy and", r), {"sandy", "and"}));            // whole tokens only
    ITEST_TRUE(is(clauses_of("!?", r), {"!?"}));
    ITEST_TRUE(clauses_of("", r).empty());
    ITEST_TRUE(clauses_of("   ", r).empty());

    // DANDA and the Hindi conjunction come from the rules, not the code.
    const std::string fire = cps({0x0906, 0x0917});
    const std::string water = cps({0x092A, 0x093E, 0x0928, 0x0940});
    const std::string aur = cps({0x0914, 0x0930});
    ITEST_TRUE(is(clauses_of(fire + cps({0x0964}) + " " + water, r), {fire + cps({0x0964}), water}));
    ITEST_TRUE(is(clauses_of(fire + " " + aur + " " + water, r), {fire, aur + " " + water}));
    ITEST_TRUE(is(clauses_of(fire + " " + aur + " " + water, no_rules()), {fire + " " + aur + " " + water}));
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--nfc-subset") == 0) nfc_subset_path() = argv[a + 1];
    }
    return ::itest::run_all("unit.normalize");
}
