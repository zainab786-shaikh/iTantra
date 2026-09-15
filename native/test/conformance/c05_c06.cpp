// Conformance — C-05, C-06 (validation-benchmark-contract §5.2). Implementation plan Phase 7.
//
//   C-05 [H]  Tier 2: encode → decode every corpus utterance.
//             Pass: exact string equality — byte for byte, including whitespace
//             and script.
//   C-06 [H]  Tier 2 adversarial: random bytes, empty input, mixed scripts,
//             characters absent from all training data.
//             Pass: a decodable payload EVERY time, and an exact round trip.
//             Proves byte fallback + non-zero floor (tier §6.7).
//
// No threshold anywhere. "C-06 has no threshold. It either always produces a
// decodable payload or the safety floor has a hole in it" (contract §5.2). Every
// case encodes, decodes and compares bytes; the failure count is printed and must
// be zero.
//
// Both boost states are exercised: unboosted (hash_present = 0) and boosted from
// a pre-message context the receiver also holds (hash_present = 1).
//
// The corpus is the Phase 6 synthetic fixture (corpus/utterances.tsv). The Tier 2
// tables were not trained on it (test/fixtures/synthetic/README.md).
//
//   c05_c06_test --packs <compiled fixture dir> --fixtures <fixture source dir>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "context/context.h"
#include "lang/normalize.h"
#include "tier2/decode.h"
#include "tier2/encode.h"
#include "tier2_fixture.h"
#include "itest.h"

using namespace itantra;
using langfx::cps;

namespace {

t2fx::Fixture& fx() {
    static t2fx::Fixture f;
    return f;
}

struct Tally {
    u32 cases    = 0u;
    u32 boosted  = 0u;
    u32 failures = 0u;
    u64 bytes    = 0u;
    u64 tokens   = 0u;
};

// Encode under `context` (null: unboosted), decode with the receiver holding the
// same pre-message context, compare bytes. True only for an exact round trip.
bool round_trip(const std::string& text, const Context* context, Tally& tally, LangId language = 1u) {
    Tier2Message m;
    m.seq           = static_cast<u8>(tally.cases);
    m.language      = language;
    m.boost_context = context;
    NativePayload p;
    Tier2Decoded  d;
    const bool ok =
        tier2_encode(fx().tables, reinterpret_cast<const u8*>(text.data()), text.size(), m, p) == Tier2Status::Ok &&
        p.len != 0u && tier2_decode(fx().tables, p.bytes, p.len, context, d) == Tier2Status::Ok && d.text == text &&
        d.metadata.tier == Tier::Tier2 && d.metadata.language == language &&
        d.metadata.hash_present == (context != nullptr);
    ++tally.cases;
    if (context != nullptr) ++tally.boosted;
    tally.bytes += text.size();
    tally.tokens += d.metadata.symbol_count;
    if (!ok) {
        if (tally.failures < 5u) std::printf("  FAILED: round trip of %zu bytes\n", text.size());
        ++tally.failures;
    }
    return ok;
}

void report(const char* what, const Tally& t) {
    std::printf("  %s: %u cases (%u boosted), %llu bytes, %llu tokens, %u failures\n", what, t.cases, t.boosted,
                static_cast<unsigned long long>(t.bytes), static_cast<unsigned long long>(t.tokens), t.failures);
}

// A pre-message context with five entity slots set, so the boost is not empty.
const Context& busy_context() {
    static const Context ctx = [] {
        Context c;
        init_context(c);
        CommitPayload p{};
        p.seq = 1u;
        const std::pair<SlotId, const char*> entries[] = {{SLOT_LOCATION, "north_gate"},
                                                          {SLOT_OBJECT, "ambulance"},
                                                          {SLOT_ACTOR, "police"},
                                                          {SLOT_STATE, "injured"},
                                                          {SLOT_SEVERITY, "urgent"}};
        for (const auto& e : entries) {
            p.slots[e.first].op    = SlotOp::Write;
            p.slots[e.first].value = fx().lang.concepts.at(e.second);
        }
        commit(c, p);
        return c;
    }();
    return ctx;
}

std::string random_bytes(t2fx::XorShift32& rng, std::size_t n) {
    std::string s;
    for (std::size_t i = 0u; i < n; ++i) s.push_back(static_cast<char>(static_cast<u8>(rng.next())));
    return s;
}

// Short pieces of many scripts and awkward code points, built from code points so
// the test source holds no hand-typed script text.
std::vector<std::string> script_fragments() {
    return {
        cps({0x0906, 0x0917}),                                   // Devanagari
        cps({0x0BA4, 0x0BC0}),                                   // Tamil
        "north gate",                                            // Latin
        cps({0x0985, 0x09AE, 0x09BF, 0x09A4}),                   // Bengali
        cps({0x0C39, 0x0C46, 0x0C32, 0x0C4D, 0x0C2A, 0x0C4D}),   // Telugu
        cps({0x0D38, 0x0D39, 0x0D3E, 0x0D2F, 0x0D02}),           // Malayalam
        cps({0x0B38, 0x0B39, 0x0B3E, 0x0B2F}),                   // Odia
        cps({0x0A86, 0x0A97}),                                   // Gujarati
        cps({0x0C85, 0x0CAA, 0x0CBE, 0x0CAF}),                   // Kannada
        cps({0x674E, 0x660E}),                                   // CJK
        cps({0x0645, 0x0627, 0x0621}),                           // Arabic, right to left
        cps({0x05E9, 0x05DC, 0x05D5, 0x05DD}),                   // Hebrew
        cps({0x041F, 0x0440, 0x0438}),                           // Cyrillic
        cps({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}),        // emoji ZWJ sequence
        cps({0x1F44D, 0x1F3FD}),                                 // emoji with skin tone
        cps({0x1F1EE, 0x1F1F3}),                                 // regional indicators
        cps({0x0061, 0x0301, 0x0323, 0x0308}),                   // stacked combining marks
        cps({0x0915, 0x094D, 0x200D, 0x0937}),                   // ZWJ inside a conjunct
        cps({0x0915, 0x094D, 0x200C, 0x0937}),                   // ZWNJ inside a conjunct
        cps({0xFEFF}),                                           // byte order mark
        std::string(1u, '\0'),                                   // NUL
        "\t",
        "\r\n",
        cps({0x00A0}),                                           // no-break space
        cps({0x2028}),                                           // line separator
        cps({0x3000}),                                           // ideographic space
        cps({0x0969, 0x0BE9, 0x0A69}),                           // native digits
        cps({0xFFFD}),                                           // replacement character
        cps({0x10FFFF}),                                         // last code point
        cps({0x0378}),                                           // unassigned
        cps({0xE000}),                                           // private use
        t2fx::bytes({0xFF}),                                     // never valid in UTF-8
        t2fx::bytes({0xC0, 0x80}),                               // overlong
        t2fx::bytes({0xED, 0xA0, 0x80}),                         // surrogate
        t2fx::bytes({0x80}),                                     // lone continuation byte
        t2fx::bytes({0xF4, 0x90, 0x80, 0x80}),                   // above U+10FFFF
        t2fx::bytes({0xE0, 0xA4}),                               // truncated sequence
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// C-05
// ---------------------------------------------------------------------------

ITEST(c05_every_corpus_utterance_round_trips_exactly) {
    Tally whole;
    Tally clauses;
    u32 nonempty_boosts = 0u;
    u32 hash_agreements = 0u;
    std::map<std::string, Context> sender;
    std::map<std::string, Context> receiver;
    for (const std::string& code : langfx::fixture_languages()) {
        init_context(sender[code]);
        init_context(receiver[code]);
    }

    u8 seq = 0u;
    for (const t2fx::Utterance& u : fx().corpus) {
        const LangId language = t2fx::test_language_id(u.lang);

        // The whole utterance as one Tier 2 input, unboosted.
        ITEST_TRUE(round_trip(u.text, nullptr, whole, language));

        // Clause by clause, as the sender streams them (tier §4.1), boosted from
        // a session context both phones hold, then unboosted.
        const UtteranceExtraction x = fx().lang.extract(u.lang, u.text);
        const std::vector<SourceSpan> spans = tier2_clause_inputs(x, u.text.size());
        std::string rebuilt;
        for (const SourceSpan& span : spans) {
            const std::string clause = u.text.substr(span.begin, span.end - span.begin);
            Context& s = sender[u.lang];
            Context& r = receiver[u.lang];
            ++seq;

            ContextBoost boost;
            boost.build(fx().tables.boost(), s);
            if (boost.size() != 0u) ++nonempty_boosts;

            Tier2Message m;
            m.seq           = seq;
            m.language      = language;
            m.boost_context = &s;
            NativePayload p;
            Tier2Decoded  d;
            const bool ok =
                tier2_encode(fx().tables, reinterpret_cast<const u8*>(clause.data()), clause.size(), m, p) ==
                    Tier2Status::Ok &&
                tier2_decode(fx().tables, p.bytes, p.len, &r, d) == Tier2Status::Ok && d.text == clause &&
                d.metadata.language == language && d.metadata.hash_present;
            ITEST_TRUE(ok);
            ++clauses.cases;
            ++clauses.boosted;
            clauses.bytes += clause.size();
            clauses.tokens += d.metadata.symbol_count;
            if (!ok) ++clauses.failures;
            rebuilt += d.text;

            ITEST_TRUE(round_trip(clause, nullptr, clauses, language));

            // Both phones then commit from the text each holds (same language,
            // tier §11.2–11.3), so the next clause is boosted from the same state.
            ITEST_TRUE(t2fx::commit_extraction(s, fx().lang.extract(u.lang, clause), seq));
            ITEST_TRUE(t2fx::commit_extraction(r, fx().lang.extract(u.lang, d.text), seq));
            ITEST_EQ(context_hash(s), context_hash(r));
            if (context_hash(s) == context_hash(r)) ++hash_agreements;
        }
        ITEST_TRUE(rebuilt == u.text);
    }

    report("C-05 whole utterances", whole);
    report("C-05 clauses", clauses);
    std::printf("  C-05: %u clauses coded with a non-empty boost; sender and receiver hashes agreed after %u of %u clauses\n",
                nonempty_boosts, hash_agreements, clauses.boosted);
    ITEST_EQ(whole.cases, fx().corpus.size());
    ITEST_EQ(whole.failures, 0u);
    ITEST_EQ(clauses.failures, 0u);
    ITEST_TRUE(nonempty_boosts != 0u);
}

ITEST(c05_whitespace_script_and_unicode_form_are_kept_byte_for_byte) {
    Tally t;
    u32 normalisation_would_change = 0u;
    for (const t2fx::Utterance& u : fx().corpus) {
        const LangId language = t2fx::test_language_id(u.lang);
        std::string doubled;
        for (char c : u.text) {
            doubled.push_back(c);
            if (c == ' ') doubled.push_back(' ');
        }
        const std::vector<std::string> variants = {
            u.text, "  " + u.text + "  ", doubled, u.text + "\r\n", "\t" + u.text, unicode::nfd(u.text), u.text + "!!",
        };
        for (const std::string& v : variants) {
            ITEST_TRUE(round_trip(v, nullptr, t, language));
            ITEST_TRUE(round_trip(v, &busy_context(), t, language));
            // What the language layer's §6.1 normalisation would change, Tier 2 kept.
            const MappedText n =
                normalize_utterance(reinterpret_cast<const u8*>(v.data()), v.size(), fx().lang.pack(u.lang).rules());
            if (n.text != v) ++normalisation_would_change;
        }
    }
    report("C-05 whitespace / script / Unicode form variants", t);
    std::printf("  C-05: %u variants differ from their normalised form; every one came back exactly as sent\n",
                normalisation_would_change);
    ITEST_EQ(t.failures, 0u);
    ITEST_TRUE(normalisation_would_change != 0u);
}

// ---------------------------------------------------------------------------
// C-06
// ---------------------------------------------------------------------------

ITEST(c06_empty_input) {
    Tally t;
    ITEST_TRUE(round_trip(std::string(), nullptr, t));
    ITEST_TRUE(round_trip(std::string(), &busy_context(), t));

    NativePayload p;
    Tier2Message  m;
    ITEST_TRUE(tier2_encode(fx().tables, nullptr, 0u, m, p) == Tier2Status::Ok);
    Tier2Decoded d;
    ITEST_TRUE(tier2_decode(fx().tables, p.bytes, p.len, nullptr, d) == Tier2Status::Ok && d.text.empty() &&
               d.metadata.symbol_count == 0u);
    ITEST_EQ(p.len, 3u);   // 21 metadata bits + 2 flush bits
    report("C-06 empty input", t);
    ITEST_EQ(t.failures, 0u);
}

ITEST(c06_random_bytes) {
    t2fx::XorShift32 rng{0x0C06C06u};
    Tally t;
    for (u32 i = 0u; i < 2000u; ++i) {
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(random_bytes(rng, rng.next() % 49u), ctx, t));
    }
    for (u32 i = 0u; i < 200u; ++i) {
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(random_bytes(rng, 49u + rng.next() % 352u), ctx, t));
    }
    for (u32 i = 0u; i < 10u; ++i) {                      // the longest input that always fits
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(random_bytes(rng, kMaxSymbolCount), ctx, t));
    }
    for (u32 i = 0u; i < 500u; ++i) {                     // random bytes spliced into real text
        std::string s = fx().corpus[rng.next() % fx().corpus.size()].text;
        // Separate statements: argument evaluation order differs between
        // compilers, and every build must generate the same inputs.
        const std::string noise = random_bytes(rng, 1u + rng.next() % 8u);
        const std::size_t at = rng.next() % (s.size() + 1u);
        s.insert(at, noise);
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(s, ctx, t));
    }
    report("C-06 random bytes", t);
    ITEST_EQ(t.failures, 0u);
}

ITEST(c06_mixed_scripts) {
    const std::vector<std::string> fragments = script_fragments();
    Tally t;
    std::string all;
    for (const std::string& f : fragments) {
        ITEST_TRUE(round_trip(f, nullptr, t));
        ITEST_TRUE(round_trip(f, &busy_context(), t));
        all += f;
    }
    ITEST_TRUE(round_trip(all, nullptr, t));
    ITEST_TRUE(round_trip(all, &busy_context(), t));

    t2fx::XorShift32 rng{0x5C4197u};
    const char* const separators[] = {"", " ", ", ", "|", "\n"};
    for (u32 i = 0u; i < 1000u; ++i) {
        std::string s;
        const u32 parts = 1u + rng.next() % 8u;
        for (u32 k = 0u; k < parts; ++k) {
            if (k != 0u) s += separators[rng.next() % 5u];
            if (rng.next() % 3u == 0u) {
                s += fx().corpus[rng.next() % fx().corpus.size()].text;
            } else {
                s += fragments[rng.next() % fragments.size()];
            }
        }
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(s, ctx, t));
    }

    // Two Unicode forms of one word are two different messages, each kept exactly.
    const std::string nfc = cps({'c', 'a', 'f', 0x00E9});
    const std::string nfd = cps({'c', 'a', 'f', 'e', 0x0301});
    ITEST_TRUE(nfc != nfd);
    ITEST_TRUE(round_trip(nfc, nullptr, t));
    ITEST_TRUE(round_trip(nfd, nullptr, t));

    report("C-06 mixed scripts", t);
    ITEST_EQ(t.failures, 0u);
}

ITEST(c06_characters_absent_from_all_training_data) {
    const Tier2Tables& tables = fx().tables;
    const std::vector<bool> untrained = t2fx::untrained_tokens(tables.ngram());
    u32 untrained_bytes = 0u;
    for (u32 b = 0u; b < 256u; ++b) {
        if (untrained[b]) ++untrained_bytes;
    }
    ITEST_TRUE(untrained_bytes > 128u);

    const std::vector<std::string> absent = {
        cps({0x1D11E}), cps({0x10FFFF}), cps({0x4E2D}), cps({0x0627}), cps({0x0985}),
        cps({0x1F600}), cps({0xE000}),   cps({0xFFFD}), cps({0x0C85}), cps({0x0D38}),
        t2fx::bytes({0xFF}), t2fx::bytes({0xFE}), t2fx::bytes({0xC0, 0x80}), t2fx::bytes({0x80}),
        t2fx::bytes({0xED, 0xA0, 0x80}),
    };

    Tally t;
    u32 proven_untrained = 0u;
    for (const std::string& a : absent) {
        // Proof from the table itself that the model never saw these tokens: no
        // unigram mass and no bigram entry. Their probability is the floor alone.
        std::vector<Symbol> tokens;
        tables.vocabulary().tokenize(reinterpret_cast<const u8*>(a.data()), a.size(), tokens);
        bool all_untrained = !tokens.empty();
        bool byte_fallback = false;
        for (Symbol s : tokens) {
            all_untrained = all_untrained && untrained[s];
            byte_fallback = byte_fallback || s < kByteTokenCount;
        }
        ITEST_TRUE(all_untrained && byte_fallback);
        if (all_untrained && byte_fallback) ++proven_untrained;

        std::string run;
        for (u32 k = 0u; k < 200u; ++k) run += a;
        const std::string embedded = "Send water to " + a + " now";
        for (const std::string& text : {a, embedded, run}) {
            ITEST_TRUE(round_trip(text, nullptr, t));
            ITEST_TRUE(round_trip(text, &busy_context(), t));
        }
    }

    // Random sequences of nothing but untrained byte tokens.
    std::vector<u32> pool;
    for (u32 b = 0u; b < 256u; ++b) {
        if (untrained[b]) pool.push_back(b);
    }
    t2fx::XorShift32 rng{0xAB5E47u};
    for (u32 i = 0u; i < 300u; ++i) {
        std::string s;
        const u32 n = 1u + rng.next() % 64u;
        for (u32 k = 0u; k < n; ++k) s.push_back(static_cast<char>(static_cast<u8>(pool[rng.next() % pool.size()])));
        const Context* ctx = (i & 1u) != 0u ? &busy_context() : nullptr;
        ITEST_TRUE(round_trip(s, ctx, t));
    }

    report("C-06 untrained characters", t);
    std::printf("  C-06: %u of 256 byte tokens have no trained mass; %u of %zu characters proven untrained\n",
                untrained_bytes, proven_untrained, absent.size());
    ITEST_EQ(t.failures, 0u);
}

ITEST(c06_every_one_and_two_byte_input) {
    Tally t;
    std::string two(2u, '\0');
    for (u32 a = 0u; a < 256u; ++a) {
        ITEST_TRUE(round_trip(t2fx::bytes({a}), nullptr, t));
        for (u32 b = 0u; b < 256u; ++b) {
            two[0] = static_cast<char>(static_cast<u8>(a));
            two[1] = static_cast<char>(static_cast<u8>(b));
            if (!round_trip(two, nullptr, t)) ITEST_TRUE(false);
        }
    }
    report("C-06 every 1- and 2-byte input", t);
    ITEST_EQ(t.cases, 256u + 256u * 256u);
    ITEST_EQ(t.failures, 0u);
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
    ContextBoost boost;
    boost.build(fx().tables.boost(), busy_context());
    std::printf("tables: %u tokens, %u n-gram rows, %u boost entries (magnitude %u); busy context boosts %u tokens\n",
                fx().tables.vocabulary().size(), fx().tables.ngram().row_count(), fx().tables.boost().entry_count(),
                fx().tables.boost().magnitude(), boost.size());
    if (boost.size() == 0u) {
        std::printf("the boosted cases would be vacuous: the busy context boosts nothing\n");
        return 1;
    }
    return ::itest::run_all("conformance.c05_c06");
}
