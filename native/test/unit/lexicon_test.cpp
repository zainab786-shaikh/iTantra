// Unit tests — lang/lexicon and slot assignment. Implementation plan Phase 6.
//
//   unit  longest-match wins; leftmost on ties
//   unit  byte-level match rejected if it splits a codepoint
//   unit  ambiguity leaves the slot UNCHANGED, never guesses
//
// Plus: the automaton against a naive search, rejection of corrupted
// automata, the token-boundary rule, and matching on the real fixture packs.
//
//   lexicon_test --packs <compiled fixture dir> --fixtures <fixture source dir>

#include "context/context.h"
#include "lang/extract.h"
#include "lang/lexicon.h"
#include "lang_fixture.h"
#include "itest.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace itantra;

namespace {

langfx::Fixture& fx() {
    static langfx::Fixture f;
    return f;
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

std::vector<u8> build(const std::vector<std::string>& patterns) {
    AutomatonBuilder b;
    for (const std::string& p : patterns) b.add(p);
    std::vector<u8> bytes;
    b.serialize(bytes);
    return bytes;
}

bool attaches(const std::vector<u8>& bytes) {
    Automaton a;
    std::size_t used = 0u;
    std::string error;
    return a.attach(bytes.data(), bytes.size(), used, error);
}

void put32(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v >> 24);
    b[at + 1u] = static_cast<u8>(v >> 16);
    b[at + 2u] = static_cast<u8>(v >> 8);
    b[at + 3u] = static_cast<u8>(v);
}

MatchCandidate cand(u32 begin, u32 end, u32 codepoints, u8 kind = 0u, u32 index = 0u) {
    return MatchCandidate{begin, end, codepoints, kind, index};
}

}  // namespace

// ---------------------------------------------------------------------------
// Automaton
// ---------------------------------------------------------------------------

ITEST(automaton_finds_exactly_the_occurrences_a_naive_search_finds) {
    XorShift32 rng{0x5EEDF00Du};
    const char alphabet[] = {'a', 'b', 'c', ' '};
    for (u32 round = 0u; round < 300u; ++round) {
        std::vector<std::string> patterns;
        for (u32 i = 0u; i < 20u; ++i) {
            std::string p(1u + rng.next() % 4u, 'a');
            for (char& ch : p) ch = alphabet[rng.next() % 4u];
            patterns.push_back(p);
        }
        AutomatonBuilder builder;
        for (const std::string& p : patterns) builder.add(p);
        std::vector<u8> bytes;
        builder.serialize(bytes);
        Automaton a;
        std::size_t used = 0u;
        std::string error;
        ITEST_TRUE(a.attach(bytes.data(), bytes.size(), used, error));
        ITEST_EQ(used, bytes.size());

        std::string text(60u, 'a');
        for (char& ch : text) ch = alphabet[rng.next() % 4u];

        std::vector<Automaton::Hit> hits;
        a.find_all(reinterpret_cast<const u8*>(text.data()), text.size(), hits);
        std::set<std::tuple<u32, u32, u32>> got;
        for (const Automaton::Hit& h : hits) got.insert(std::make_tuple(h.pattern, h.begin, h.end));

        std::set<std::tuple<u32, u32, u32>> want;
        for (const std::string& p : patterns) {
            const u32 id = builder.add(p);   // existing id
            for (std::size_t at = text.find(p); at != std::string::npos; at = text.find(p, at + 1u)) {
                want.insert(std::make_tuple(id, static_cast<u32>(at), static_cast<u32>(at + p.size())));
            }
        }
        ITEST_TRUE(got == want);
        ITEST_EQ(hits.size(), got.size());   // no duplicates
    }
}

ITEST(automaton_rejects_corrupted_tables) {
    const std::vector<u8> good = build({"north gate", "gate", "no"});
    ITEST_TRUE(attaches(good));
    for (std::size_t len = 0u; len < good.size(); ++len) {
        ITEST_TRUE(!attaches(std::vector<u8>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len))));
    }
    const u32 nodes = (u32{good[0]} << 24) | (u32{good[1]} << 16) | (u32{good[2]} << 8) | u32{good[3]};
    const std::size_t edges_at = 8u + 18u * nodes;

    std::vector<u8> b = good;
    put32(b, 0u, 0u);                              // no root
    ITEST_TRUE(!attaches(b));

    b = good;
    put32(b, edges_at + 1u, 0u);                   // first edge points back at the root
    ITEST_TRUE(!attaches(b));

    b = good;
    put32(b, 4u + 18u * 1u + 6u, nodes - 1u);     // node 1's failure link points forward
    ITEST_TRUE(!attaches(b));

    b = good;
    std::swap(b[edges_at], b[edges_at + 5u]);     // root edges out of order (if root has two)
    const bool root_has_two = ((u32{good[8]} << 8) | u32{good[9]}) >= 2u;
    ITEST_TRUE(!root_has_two || !attaches(b));

    b = good;
    put32(b, 4u + 18u * (nodes - 1u) + 14u, 7u);  // pattern index out of range
    ITEST_TRUE(!attaches(b));
}

// ---------------------------------------------------------------------------
// §8.1 selection and boundaries
// ---------------------------------------------------------------------------

ITEST(longest_match_wins_and_leftmost_breaks_ties) {
    // "north gate" (10) beats "gate" (4) and "north" (5)
    std::vector<MatchCandidate> s = select_matches({cand(6u, 10u, 4u), cand(0u, 10u, 10u), cand(0u, 5u, 5u)});
    ITEST_EQ(s.size(), 1u);
    ITEST_EQ(s[0].begin, 0u);
    ITEST_EQ(s[0].end, 10u);

    // equal length, overlapping: "a b" and "b c" over "a b c" → leftmost
    s = select_matches({cand(2u, 5u, 3u), cand(0u, 3u, 3u)});
    ITEST_EQ(s.size(), 1u);
    ITEST_EQ(s[0].begin, 0u);

    // longest first even when it starts later: "p q" (3) vs "q rr" (4)
    s = select_matches({cand(0u, 3u, 3u), cand(2u, 6u, 4u)});
    ITEST_EQ(s.size(), 1u);
    ITEST_EQ(s[0].begin, 2u);

    // same span from two sources: the lower kind wins (concept over number)
    s = select_matches({cand(0u, 3u, 3u, 1u), cand(0u, 3u, 3u, 0u)});
    ITEST_EQ(s.size(), 1u);
    ITEST_EQ(s[0].kind, 0u);

    // non-overlapping matches all survive, returned in text order
    s = select_matches({cand(8u, 12u, 4u), cand(0u, 4u, 4u), cand(4u, 8u, 2u)});
    ITEST_EQ(s.size(), 3u);
    ITEST_TRUE(s[0].begin == 0u && s[1].begin == 4u && s[2].begin == 8u);

    // length is in codepoints, not bytes: 2 Devanagari codepoints (6 bytes)
    // lose to 3 Latin codepoints (3 bytes)
    s = select_matches({cand(0u, 6u, 2u), cand(0u, 3u, 3u)});
    ITEST_EQ(s[0].end, 3u);
}

ITEST(byte_level_match_that_splits_a_codepoint_is_rejected) {
    // U+0924 is E0 A4 A4. A raw byte pattern A4 A4 occurs at bytes 1–3,
    // inside the character.
    const std::string ta = langfx::cps({0x0924});
    const std::string raw = {static_cast<char>(0xA4), static_cast<char>(0xA4)};
    AutomatonBuilder b;
    const u32 split_pattern = b.add(raw);
    const u32 whole_pattern = b.add(ta);
    std::vector<u8> bytes;
    b.serialize(bytes);
    Automaton a;
    std::size_t used = 0u;
    std::string error;
    ITEST_TRUE(a.attach(bytes.data(), bytes.size(), used, error));

    const auto* text = reinterpret_cast<const u8*>(ta.data());
    std::vector<Automaton::Hit> hits;
    a.find_all(text, ta.size(), hits);
    const std::vector<bool> starts = codepoint_starts(text, ta.size());
    bool saw_split = false, saw_whole = false;
    for (const Automaton::Hit& h : hits) {
        if (h.pattern == split_pattern) {
            saw_split = true;
            ITEST_EQ(h.begin, 1u);
            ITEST_TRUE(!on_codepoint_boundaries(starts, h.begin, h.end));   // rejected
        }
        if (h.pattern == whole_pattern) {
            saw_whole = true;
            ITEST_TRUE(on_codepoint_boundaries(starts, h.begin, h.end));    // accepted
        }
    }
    ITEST_TRUE(saw_split && saw_whole);
}

ITEST(matches_must_sit_on_token_boundaries) {
    const std::string text = "injured in gateway gate";
    const auto* t = reinterpret_cast<const u8*>(text.data());
    ITEST_TRUE(!on_token_boundaries(t, text.size(), 0u, 2u));    // "in" inside "injured"
    ITEST_TRUE(on_token_boundaries(t, text.size(), 8u, 10u));    // "in"
    ITEST_TRUE(!on_token_boundaries(t, text.size(), 11u, 15u));  // "gate" inside "gateway"
    ITEST_TRUE(on_token_boundaries(t, text.size(), 19u, 23u));   // "gate"
    ITEST_TRUE(on_token_boundaries(t, text.size(), 0u, 23u));
    ITEST_TRUE(!on_token_boundaries(t, text.size(), 3u, 3u));

    // Through a real pack: "fire" does not match inside "firewood".
    const UtteranceExtraction e = fx().extract("en", "firewood");
    ITEST_EQ(e.clauses.size(), 1u);
    ITEST_TRUE(e.clauses[0].concepts.empty());
}

ITEST(fixture_pack_prefers_the_longest_entry) {
    const UtteranceExtraction e = fx().extract("en", "fire brigade at relief camp");
    ITEST_EQ(e.clauses.size(), 1u);
    const ClauseExtraction& c = e.clauses[0];
    ITEST_EQ(c.concepts.size(), 2u);   // not "fire", not the "relief" homograph
    ITEST_EQ(c.concepts[0].concept_id, fx().concepts.at("fire_brigade"));
    ITEST_EQ(c.concepts[1].concept_id, fx().concepts.at("relief_camp"));
    ITEST_TRUE(c.slots[SLOT_LOCATION].state == SlotState::Value);
    ITEST_EQ(c.ambiguous_slots, 0u);
}

// ---------------------------------------------------------------------------
// §8.2 ambiguity
// ---------------------------------------------------------------------------

ITEST(ambiguity_leaves_the_slot_unchanged_and_never_guesses) {
    const u16 relief_camp = fx().concepts.at("relief_camp");

    // "relief" is a homograph in the fixture: LOCATION relief_camp and OBJECT food.
    UtteranceExtraction e = fx().extract("en", "send relief");
    ITEST_EQ(e.clauses.size(), 1u);
    ClauseExtraction c = e.clauses[0];
    ITEST_TRUE(c.slots[SLOT_LOCATION].state == SlotState::Ambiguous);
    ITEST_TRUE(c.slots[SLOT_OBJECT].state == SlotState::Ambiguous);
    ITEST_EQ(c.slots[SLOT_LOCATION].value, 0u);
    ITEST_EQ(c.slots[SLOT_OBJECT].value, 0u);
    ITEST_EQ(c.ambiguous_slots, (1u << SLOT_LOCATION) | (1u << SLOT_OBJECT));

    // The detected intent settles it only when its expected slots do.
    const IntentInfo* move = fx().common.intent(fx().intents.at("REQUEST_MOVE"));          // ACTOR, LOCATION
    const IntentInfo* supply = fx().common.intent(fx().intents.at("REQUEST_SUPPLY_AT"));   // OBJECT, LOCATION, QUANTITY
    assign_slots(c, move->expected_slots);
    ITEST_TRUE(c.slots[SLOT_LOCATION].state == SlotState::Value);
    ITEST_EQ(c.slots[SLOT_LOCATION].value, relief_camp);
    ITEST_TRUE(c.slots[SLOT_OBJECT].state == SlotState::None);
    assign_slots(c, supply->expected_slots);
    ITEST_TRUE(c.slots[SLOT_LOCATION].state == SlotState::Ambiguous);
    ITEST_TRUE(c.slots[SLOT_OBJECT].state == SlotState::Ambiguous);

    // Two different locations in one clause.
    e = fx().extract("en", "north gate or south gate");
    ITEST_TRUE(e.clauses[0].slots[SLOT_LOCATION].state == SlotState::Ambiguous);

    // The same concept twice is not ambiguous.
    e = fx().extract("en", "north gate north entrance");
    ITEST_TRUE(e.clauses[0].slots[SLOT_LOCATION].state == SlotState::Value);

    // "Left unchanged" in the context: a committed LOCATION survives a clause
    // whose LOCATION is ambiguous (candidate → commit mapping: Value → Write,
    // anything else → Absent).
    Context ctx;
    init_context(ctx);
    CommitPayload first{};
    for (u32 s = 0u; s < kSlotCount; ++s) first.slots[s] = SlotUpdate{SlotOp::Absent, 0u};
    first.slots[SLOT_LOCATION] = SlotUpdate{SlotOp::Write, fx().concepts.at("bridge")};
    ITEST_TRUE(commit(ctx, first) == CommitResult::Ok);

    e = fx().extract("en", "north gate or south gate");
    CommitPayload next{};
    for (u32 s = 0u; s < kSlotCount; ++s) next.slots[s] = SlotUpdate{SlotOp::Absent, 0u};
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (e.clauses[0].slots[s].state == SlotState::Value) {
            next.slots[s] = SlotUpdate{SlotOp::Write, e.clauses[0].slots[s].value};
        }
    }
    ITEST_TRUE(commit(ctx, next) == CommitResult::Ok);
    ITEST_EQ(ctx.slots[SLOT_LOCATION].current, fx().concepts.at("bridge"));
    ITEST_EQ(ctx.slots[SLOT_LOCATION].ver, 1u);
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--packs") == 0) packs = argv[a + 1];
        if (std::strcmp(argv[a], "--fixtures") == 0) fixtures = argv[a + 1];
    }
    if (!fx().load(packs, fixtures)) {
        std::printf("fixture not loaded: %s\n", fx().error.c_str());
        return 1;
    }
    return ::itest::run_all("unit.lexicon");
}
