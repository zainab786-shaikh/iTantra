// Unit tests — Tier 2 (native/src/tier2). Implementation plan Phase 7.
//
//   unit  all 256 byte values present in the vocabulary
//   unit  every token has p > 0 in every context — backoff reaches a floor
//   unit  table resets at every clause boundary
//   unit  boost applied only when hash_present == 1
//   unit  unboosted encoding decodes with ANY context state
//
// Plus: the tokenizer rule against a naive reference, table loader validation
// and frequency bounds, Kneser-Ney estimation, what the boost reads,
// symbol_count = token count, the ASM_TOO_LONG boundary, argument handling, and
// the clause-input partition.
//
//   tier2_test --packs <compiled fixture dir> --fixtures <fixture source dir>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "coder/static_model.h"
#include "context/context.h"
#include "packet/assemble.h"
#include "packet/parse.h"
#include "tier2/decode.h"
#include "tier2/encode.h"
#include "tier2_fixture.h"
#include "itest.h"

using namespace itantra;

namespace {

t2fx::Fixture& fx() {
    static t2fx::Fixture f;
    return f;
}

std::vector<u8> payload_of(const std::vector<u8>& file, PackKind kind) {
    const u8*   payload = nullptr;
    std::size_t length  = 0u;
    std::string why;
    if (!unwrap_container(file, kind, payload, length, why)) return {};
    return std::vector<u8>(payload, payload + length);
}

bool load_vocab(const std::vector<std::string>& subwords, SubwordVocabulary& v) {
    const std::vector<u8> payload = payload_of(serialize_subwords(subwords), PackKind::Subwords);
    std::string why;
    return v.load(payload.data(), payload.size(), why);
}

bool load_ngram(const NgramSource& source, u32 vocab_size, NgramTable& table) {
    const std::vector<u8> payload = payload_of(serialize_ngram(source), PackKind::Ngram);
    std::string why;
    return table.load(payload.data(), payload.size(), vocab_size, why);
}

bool load_boost(const BoostSource& source, u32 vocab_size, BoostTable& table) {
    const std::vector<u8> payload = payload_of(serialize_boost(source), PackKind::Boost);
    std::string why;
    return table.load(payload.data(), payload.size(), vocab_size, why);
}

std::vector<Symbol> tokens_of(const SubwordVocabulary& v, const std::string& text) {
    std::vector<Symbol> t;
    v.tokenize(reinterpret_cast<const u8*>(text.data()), text.size(), t);
    return t;
}

Tier2Status encode(const std::string& text, const Context* ctx, NativePayload& out, LangId language = 1u) {
    Tier2Message m;
    m.seq           = 7u;
    m.language      = language;
    m.boost_context = ctx;
    return tier2_encode(fx().tables, reinterpret_cast<const u8*>(text.data()), text.size(), m, out);
}

Tier2Status decode(const NativePayload& p, const Context* ctx, Tier2Decoded& out) {
    return tier2_decode(fx().tables, p.bytes, p.len, ctx, out);
}

bool same_payload(const NativePayload& a, const NativePayload& b) {
    return a.len == b.len && std::memcmp(a.bytes, b.bytes, a.len) == 0;
}

// p(a) > p(b), by cross-multiplication.
bool more_probable(const SymbolRange& a, const SymbolRange& b) {
    return u64{a.high - a.low} * b.total > u64{b.high - b.low} * a.total;
}

// A uniform-ish valid source for a vocabulary of `v` tokens.
NgramSource simple_source(u32 v) {
    NgramSource s;
    s.vocab_size     = v;
    s.default_lambda = kContextMassLimit;
    s.unigram.assign(v, 0u);
    s.unigram[0] = kUnigramTotal;
    return s;
}

class FixedModel final : public PayloadModel {
public:
    explicit FixedModel(const Model& m) : m_(m) {}
    const Model& model_at(u32, const Symbol*) const noexcept override { return m_; }

private:
    const Model& m_;
};

Context context_with(std::initializer_list<std::pair<SlotId, const char*>> concepts) {
    Context ctx;
    init_context(ctx);
    CommitPayload p{};
    p.seq = 1u;
    for (const auto& c : concepts) {
        p.slots[c.first].op    = SlotOp::Write;
        p.slots[c.first].value = fx().lang.concepts.at(c.second);
    }
    commit(ctx, p);
    return ctx;
}

}  // namespace

// ---------------------------------------------------------------------------
// Vocabulary and tokenizer — tier §6.2, T3, T3a
// ---------------------------------------------------------------------------

ITEST(all_256_byte_values_are_vocabulary_entries) {
    const SubwordVocabulary& v = fx().tables.vocabulary();
    ITEST_TRUE(v.size() > kByteTokenCount);
    std::vector<Symbol> t;
    for (u32 b = 0u; b < 256u; ++b) {
        const std::string_view bytes = v.token_bytes(b);
        ITEST_EQ(bytes.size(), 1u);
        ITEST_EQ(bytes.empty() ? 0x100u : u32{static_cast<u8>(bytes[0])}, b);
        const u8 one = static_cast<u8>(b);
        v.tokenize(&one, 1u, t);
        ITEST_EQ(t.size(), 1u);
        ITEST_EQ(t.empty() ? 0x100u : t[0], b);
        std::string back;
        ITEST_TRUE(v.detokenize(t.data(), t.size(), back) && back.size() == 1u && static_cast<u8>(back[0]) == b);
    }
    // No subword shadows a byte token: each is at least two bytes of valid UTF-8.
    for (u32 id = kByteTokenCount; id < v.size(); ++id) {
        const std::string_view s = v.token_bytes(id);
        ITEST_TRUE(s.size() >= kSubwordMinBytes && subword_fault(std::string(s)) == nullptr);
    }
    ITEST_TRUE(v.token_bytes(v.size()).empty());

    // A vocabulary with no subwords at all still has every byte.
    SubwordVocabulary bare;
    ITEST_TRUE(load_vocab({}, bare));
    ITEST_EQ(bare.size(), 256u);
    for (u32 b = 0u; b < 256u; ++b) ITEST_EQ(bare.token_bytes(b).size(), 1u);
}

ITEST(tokenizer_takes_the_longest_subword_then_bytes) {
    SubwordVocabulary v;
    ITEST_TRUE(load_vocab({"ab", "abc", "cd", "de", "xyz"}, v));
    const Symbol ab = 256u, abc = 257u, cd = 258u, de = 259u, xyz = 260u;
    const auto is = [&v](const std::string& text, const std::vector<Symbol>& want) { return tokens_of(v, text) == want; };

    ITEST_TRUE((is("abcde", {abc, de})));
    ITEST_TRUE((is("abcd", {abc, 'd'})));       // longest at each position, not a global optimum ([ab][cd])
    ITEST_TRUE((is("xabx", {'x', ab, 'x'})));
    ITEST_TRUE((is("abz", {ab, 'z'})));
    ITEST_TRUE((is("xy", {'x', 'y'})));         // a prefix of a subword is not a match
    ITEST_TRUE((is("xyzxyz", {xyz, xyz})));
    ITEST_TRUE((is("cde", {cd, 'e'})));
    ITEST_TRUE((is("", {})));
}

ITEST(tokenizer_agrees_with_a_naive_longest_match_and_is_lossless) {
    t2fx::XorShift32 rng{0xC0FFEEu};
    const char alphabet[] = {'a', 'b', 'c', ' '};
    u32 compared = 0u;
    for (u32 round = 0u; round < 200u; ++round) {
        std::set<std::string> words;
        const u32 count = 1u + rng.next() % 12u;
        while (words.size() < count) {
            std::string w;
            const u32 len = 2u + rng.next() % 4u;
            for (u32 k = 0u; k < len; ++k) w.push_back(alphabet[rng.next() % 4u]);
            words.insert(w);
        }
        const std::vector<std::string> list(words.begin(), words.end());
        SubwordVocabulary v;
        ITEST_TRUE(load_vocab(list, v));
        for (u32 t = 0u; t < 20u; ++t) {
            std::string text;
            const u32 len = rng.next() % 40u;
            for (u32 k = 0u; k < len; ++k) text.push_back(alphabet[rng.next() % 4u]);

            std::vector<Symbol> want;
            for (std::size_t i = 0u; i < text.size();) {
                std::size_t best = 0u;
                Symbol      id   = 0u;
                for (std::size_t l = 2u; l <= 5u && i + l <= text.size(); ++l) {
                    const auto it = std::find(list.begin(), list.end(), text.substr(i, l));
                    if (it != list.end()) {
                        best = l;
                        id   = kByteTokenCount + static_cast<u32>(it - list.begin());
                    }
                }
                if (best != 0u) {
                    want.push_back(id);
                    i += best;
                } else {
                    want.push_back(static_cast<u8>(text[i]));
                    ++i;
                }
            }
            const std::vector<Symbol> got = tokens_of(v, text);
            ITEST_TRUE(got == want);
            std::string back;
            ITEST_TRUE(v.detokenize(got.data(), got.size(), back) && back == text);
            ++compared;
        }
    }

    // Lossless on arbitrary bytes, invalid UTF-8 included, with the fixture vocabulary.
    const SubwordVocabulary& fv = fx().tables.vocabulary();
    for (u32 round = 0u; round < 2000u; ++round) {
        std::string text;
        const u32 len = rng.next() % 64u;
        for (u32 k = 0u; k < len; ++k) text.push_back(static_cast<char>(static_cast<u8>(rng.next())));
        const std::vector<Symbol> t = tokens_of(fv, text);
        std::string back;
        ITEST_TRUE(fv.detokenize(t.data(), t.size(), back) && back == text);
        ITEST_TRUE(t.size() <= text.size());
    }
    const Symbol bad = fv.size();
    std::string back;
    ITEST_TRUE(!fv.detokenize(&bad, 1u, back));
    std::printf("  tokenizer: %u texts compared with the naive rule, 2000 random byte strings lossless\n", compared);
}

ITEST(vocabulary_loader_refuses_malformed_files) {
    SubwordVocabulary v;
    ITEST_TRUE(!load_vocab({"ab", "ab"}, v));                      // duplicate
    ITEST_TRUE(!v.loaded());
    ITEST_TRUE(!load_vocab({"a"}, v));                             // one byte: that is a byte token
    ITEST_TRUE(!load_vocab({std::string(65u, 'a')}, v));           // too long
    ITEST_TRUE(!load_vocab({t2fx::bytes({0xC3u, 0x28u})}, v));     // invalid UTF-8
    ITEST_TRUE(!load_vocab({t2fx::bytes({0xC0u, 0x80u})}, v));     // overlong
    ITEST_TRUE(!load_vocab({t2fx::bytes({0xE0u, 0xA4u})}, v));     // truncated codepoint
    ITEST_TRUE(!load_vocab({t2fx::bytes({0xEDu, 0xA0u, 0x80u})}, v));   // surrogate
    ITEST_TRUE(load_vocab({std::string(64u, 'a'), "ab"}, v));
    ITEST_TRUE(v.loaded());

    std::string why;
    std::vector<u8> payload = payload_of(serialize_subwords({"ab"}), PackKind::Subwords);
    payload[3] = 2u;                                               // tokenizer version 2
    ITEST_TRUE(!v.load(payload.data(), payload.size(), why));
    payload = payload_of(serialize_subwords({"ab"}), PackKind::Subwords);
    payload.push_back(0u);                                         // trailing byte
    ITEST_TRUE(!v.load(payload.data(), payload.size(), why));
    payload.resize(payload.size() - 2u);                           // truncated
    ITEST_TRUE(!v.load(payload.data(), payload.size(), why));

    // Every table file is refused if one byte is corrupted (container CRC), and
    // the set is refused if the n-gram table was built for another vocabulary.
    PackFiles clean;
    std::string error;
    ITEST_TRUE(read_pack_directory(fx().lang.packs_dir + "/tier2", Tier2Tables::file_names(), clean, error));
    for (std::size_t f = 0u; f < clean.size(); ++f) {
        PackFiles broken = clean;
        u8& victim = broken[f].second[broken[f].second.size() / 2u];
        victim = static_cast<u8>(victim ^ 0x01u);
        Tier2Tables t;
        ITEST_TRUE(!t.load(broken, error) && !t.loaded());
    }
    PackFiles mismatched = clean;
    for (PackFile& f : mismatched) {
        if (f.first == "ngram.bin") f.second = serialize_ngram(simple_source(fx().tables.vocabulary().size() + 1u));
    }
    Tier2Tables t;
    ITEST_TRUE(!t.load(mismatched, error));
    ITEST_TRUE(t.load(clean, error) && t.loaded());
}

// ---------------------------------------------------------------------------
// Model — tier §6.3, T3b
// ---------------------------------------------------------------------------

ITEST(every_token_has_nonzero_probability_in_every_context) {
    const Tier2Tables& t = fx().tables;
    const u32 v = t.vocabulary().size();

    const Context rich = context_with({{SLOT_LOCATION, "north_gate"},
                                       {SLOT_OBJECT, "ambulance"},
                                       {SLOT_ACTOR, "police"},
                                       {SLOT_STATE, "injured"},
                                       {SLOT_SEVERITY, "urgent"}});
    ContextBoost boost;
    boost.build(t.boost(), rich);
    ITEST_TRUE(boost.size() != 0u);

    u64 checked = 0u;
    u32 zero = 0u;
    u32 broken_tiling = 0u;
    for (const ContextBoost* b : {static_cast<const ContextBoost*>(nullptr), static_cast<const ContextBoost*>(&boost)}) {
        const Tier2Model model(t.ngram(), b);
        Symbol prefix[1] = {0u};
        for (u32 h = 0u; h <= v; ++h) {                    // every token as history, and BOS
            prefix[0] = h;
            const Model& m = h == v ? model.model_at(0u, prefix) : model.model_at(1u, prefix);
            const u32 total = m.total();
            ITEST_TRUE(total >= v && total <= kModelMaxTotal);
            u32 expect_low = 0u;
            for (u32 s = 0u; s < v; ++s) {
                const SymbolRange r = m.range_of(s);
                if (r.high <= r.low) ++zero;
                if (r.low != expect_low || r.total != total) ++broken_tiling;
                expect_low = r.high;
                ++checked;
            }
            ITEST_EQ(expect_low, total);
            ITEST_EQ(m.range_of(v).high, m.range_of(v).low);   // outside the alphabet: p = 0
        }
    }
    ITEST_EQ(zero, 0u);
    ITEST_EQ(broken_tiling, 0u);
    std::printf("  floor: %llu (history, token) pairs over %u histories x 2 boost states, none zero\n",
                static_cast<unsigned long long>(checked), v + 1u);
}

ITEST(floor_holds_whatever_the_trained_numbers_are) {
    // Every trained mass on token 'A'; after 'A' the row backs off nowhere
    // (lambda 0) and puts its whole mass on 'B'. Every other token still has
    // frequency 1 — and still codes.
    const u32 v = 300u;
    NgramSource src = simple_source(v);
    src.unigram.assign(v, 0u);
    src.unigram['A'] = kUnigramTotal;
    NgramSource::Row row;
    row.history = 'A';
    row.lambda  = 0u;
    row.entries.push_back(NgramEntry{'B', kContextMassLimit});
    src.rows.push_back(row);

    NgramTable table;
    ITEST_TRUE(load_ngram(src, v, table));
    const Tier2Model model(table, nullptr);
    const Symbol after_a[1] = {'A'};
    const Model& m = model.model_at(1u, after_a);
    u32 floor_only = 0u;
    for (u32 s = 0u; s < v; ++s) {
        const SymbolRange r = m.range_of(s);
        ITEST_TRUE(r.high > r.low);
        if (r.high - r.low == 1u) ++floor_only;
    }
    ITEST_EQ(floor_only, v - 1u);

    const Symbol symbols[] = {'A', 0xFFu, 'A', 0x00u, 299u, 'B'};
    AssemblyInput in;
    in.tier         = Tier::Tier2;
    in.symbols      = symbols;
    in.symbol_count = 6u;
    in.model        = &model;
    NativePayload p;
    ITEST_TRUE(assemble(in, p) == AsmResult::Ok);
    Metadata md;
    u32 offset = 0u;
    ITEST_TRUE(parse_metadata(p.bytes, p.len, md, offset) == ParseStatus::Ok);
    Symbol out[6] = {};
    ITEST_TRUE(decode_symbols(p.bytes, p.len, offset, 6u, model, out, 6u) == ParseStatus::Ok);
    ITEST_TRUE(std::equal(symbols, symbols + 6, out));
}

ITEST(ngram_loader_enforces_the_frequency_bounds) {
    const u32 v = 300u;
    NgramTable table;
    NgramSource base = simple_source(v);
    ITEST_TRUE(load_ngram(base, v, table));
    ITEST_TRUE(!load_ngram(base, v + 1u, table));                  // built for another vocabulary

    NgramSource s = base;
    s.rows.push_back(NgramSource::Row{5u, kContextMassLimit - 1u, {NgramEntry{7u, 1u}}});
    ITEST_TRUE(load_ngram(s, v, table));                           // exactly at the mass limit
    s.rows.back().entries[0].weight = 2u;
    ITEST_TRUE(!load_ngram(s, v, table));                          // one over

    s = base;
    s.default_lambda = kContextMassLimit + 1u;
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.unigram[1] = 1u;                                             // sums to 2^16 + 1
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{5u, 1u, {NgramEntry{v, 1u}}});   // token out of range
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{5u, 1u, {NgramEntry{7u, 0u}}});  // zero weight
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{5u, 1u, {NgramEntry{8u, 1u}, NgramEntry{7u, 1u}}});   // unsorted
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{5u, 1u, {}});
    s.rows.push_back(NgramSource::Row{5u, 1u, {}});                // duplicate history
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{v + 1u, 1u, {}});            // beyond BOS
    ITEST_TRUE(!load_ngram(s, v, table));
    s = base;
    s.rows.push_back(NgramSource::Row{v, 1u, {}});                 // BOS itself is a valid history
    ITEST_TRUE(load_ngram(s, v, table));
}

ITEST(kneser_ney_estimation_is_integer_normalised_and_prefers_what_it_saw) {
    const u32 v = 260u;
    std::vector<std::vector<Symbol>> training = {{'a', 'b'}, {'a', 'b'}, {'a', 'c'}, {'x', 'b'}, {257u}};
    NgramSource src;
    std::string why;
    ITEST_TRUE(estimate_kneser_ney(v, training, src, why));
    u64 sum = 0u;
    for (u32 u : src.unigram) sum += u;
    ITEST_EQ(sum, kUnigramTotal);
    for (const NgramSource::Row& row : src.rows) {
        u64 mass = row.lambda;
        for (const NgramEntry& e : row.entries) mass += e.weight;
        ITEST_TRUE(mass <= kContextMassLimit);
    }
    NgramTable table;
    ITEST_TRUE(load_ngram(src, v, table));

    // After 'a': 'b' (seen twice) > 'c' (once) > 'z' (never).
    const Tier2Model model(table, nullptr);
    const Symbol after_a[1] = {'a'};
    const Model& m = model.model_at(1u, after_a);
    const SymbolRange b = m.range_of('b');
    const SymbolRange c = m.range_of('c');
    const SymbolRange z = m.range_of('z');
    ITEST_TRUE(more_probable(b, c) && more_probable(c, z));
    // 'b' follows two distinct histories, 'c' one: continuation counts, not raw counts.
    ITEST_TRUE(table.unigrams()['b'] > table.unigrams()['c']);

    ITEST_TRUE(!estimate_kneser_ney(v, {}, src, why));
    ITEST_TRUE(!estimate_kneser_ney(v, {{v}}, src, why));
    ITEST_TRUE(!estimate_kneser_ney(255u, training, src, why));
}

// ---------------------------------------------------------------------------
// Boost — tier §6.4, §6.5, T5
// ---------------------------------------------------------------------------

ITEST(boost_loader_enforces_the_mass_bound) {
    const u32 v = 300u;
    BoostSource b;
    b.vocab_size = v;
    const auto run = [](u32 first, u32 n) {
        std::vector<u32> t;
        for (u32 k = 0u; k < n; ++k) t.push_back(first + k);
        return t;
    };
    b.entries = {{SLOT_ACTOR, 1u, run(256u, 10u)}, {SLOT_LOCATION, 5u, run(260u, 20u)}, {SLOT_LOCATION, 6u, run(270u, 3u)}};
    const u32 largest_sum = 10u + 20u;   // one current value per slot
    BoostTable table;
    b.magnitude = kBoostMassLimit / largest_sum;
    ITEST_TRUE(load_boost(b, v, table));
    b.magnitude += 1u;
    ITEST_TRUE(!load_boost(b, v, table));
    b.magnitude = 1u;
    ITEST_TRUE(!load_boost(b, v + 1u, table));

    BoostSource s = b;
    s.magnitude = 0u;
    ITEST_TRUE(!load_boost(s, v, table));
    s = b;
    s.entries.push_back({SLOT_LAST_REF, 1u, {256u}});               // LAST_REF is not an entity
    ITEST_TRUE(!load_boost(s, v, table));
    s = b;
    s.entries.push_back({SLOT_STATE, 0u, {256u}});                  // 0 means empty
    ITEST_TRUE(!load_boost(s, v, table));
    s = b;
    s.entries.push_back({SLOT_STATE, 9u, {}});                      // no tokens
    ITEST_TRUE(!load_boost(s, v, table));
    s = b;
    s.entries.push_back({SLOT_ACTOR, 1u, {256u}});                  // not ascending
    ITEST_TRUE(!load_boost(s, v, table));
    s = b;
    s.entries.push_back({SLOT_STATE, 9u, {v}});                     // token out of range
    ITEST_TRUE(!load_boost(s, v, table));
}

ITEST(boost_reads_only_current_of_actor_to_state) {
    const Tier2Tables& t = fx().tables;
    const u16 north_gate = fx().lang.concepts.at("north_gate");

    Context ctx = context_with({{SLOT_LOCATION, "north_gate"}});
    ContextBoost base;
    base.build(t.boost(), ctx);
    u32 count = 0u;
    const u32* expected = t.boost().tokens(SLOT_LOCATION, north_gate, count);
    ITEST_TRUE(count != 0u && base.size() == count);
    ITEST_TRUE(count != 0u && std::equal(expected, expected + count, base.tokens()));

    // Unhashed fields change nothing.
    Context other = ctx;
    other.slots[SLOT_LOCATION].recent[0] = fx().lang.concepts.at("hospital");
    other.slots[SLOT_OBJECT].recent[1]   = fx().lang.concepts.at("ambulance");
    other.slots[SLOT_LOCATION].age       = 200u;
    other.slots[SLOT_LOCATION].ver       = 99u;
    other.context_id                     = 1234u;
    other.seq                            = 77u;
    other.hash                           = 0xBEEFu;
    other.slots[SLOT_LAST_REF].current   = encode_last_ref(SLOT_LOCATION);
    ContextBoost same;
    same.build(t.boost(), other);
    ITEST_TRUE(same.size() == base.size() && std::equal(base.tokens(), base.tokens() + base.size(), same.tokens()));

    // The key includes the slot: a QUANTITY or TIME equal to a concept ID boosts nothing of that concept.
    Context numbers;
    init_context(numbers);
    numbers.slots[SLOT_QUANTITY].current = north_gate;
    numbers.slots[SLOT_TIME].current     = north_gate;
    ContextBoost none;
    none.build(t.boost(), numbers);
    ITEST_EQ(none.size(), 0u);

    // An empty context boosts nothing.
    Context empty;
    init_context(empty);
    none.build(t.boost(), empty);
    ITEST_EQ(none.size(), 0u);
}

ITEST(boost_applied_only_when_hash_present) {
    const Tier2Tables& t = fx().tables;
    const std::string text = fx().corpus.at(0).text;   // h1: the north gate
    const Context ctx = context_with({{SLOT_LOCATION, "north_gate"}});

    NativePayload boosted;
    NativePayload plain;
    ITEST_TRUE(encode(text, &ctx, boosted) == Tier2Status::Ok);
    ITEST_TRUE(encode(text, nullptr, plain) == Tier2Status::Ok);

    Metadata md;
    u32 offset = 0u;
    ITEST_TRUE(parse_metadata(boosted.bytes, boosted.len, md, offset) == ParseStatus::Ok);
    ITEST_TRUE(md.hash_present);
    ITEST_EQ(md.context_hash, wire_context_hash(context_hash(ctx)));
    ITEST_TRUE(parse_metadata(plain.bytes, plain.len, md, offset) == ParseStatus::Ok);
    ITEST_TRUE(!md.hash_present);
    ITEST_EQ(md.context_hash, 0u);

    // hash_present = 0 is coded under exactly the unboosted table.
    Tier2Clause clause(t);
    Tier2Message m;
    ITEST_TRUE(clause.prepare(reinterpret_cast<const u8*>(text.data()), text.size(), m) == Tier2Status::Ok);
    ITEST_TRUE(!clause.assembly_input().hash_present);
    const std::vector<Symbol> tokens = clause.tokens();
    const Tier2Model reference(t.ngram(), nullptr);
    u32 differing = 0u;
    for (u32 i = 0u; i < tokens.size(); ++i) {
        const SymbolRange a = clause.assembly_input().model->model_at(i, tokens.data()).range_of(tokens[i]);
        const SymbolRange b = reference.model_at(i, tokens.data()).range_of(tokens[i]);
        if (a.low != b.low || a.high != b.high || a.total != b.total) ++differing;
    }
    ITEST_EQ(differing, 0u);

    // hash_present = 1: the boost is real — the entity's subword is more probable.
    ContextBoost boost;
    boost.build(t.boost(), ctx);
    ITEST_TRUE(!tokens.empty() && std::binary_search(boost.tokens(), boost.tokens() + boost.size(), tokens[0]));
    const Tier2Model with_boost(t.ngram(), &boost);
    ITEST_TRUE(more_probable(with_boost.model_at(0u, tokens.data()).range_of(tokens[0]),
                             reference.model_at(0u, tokens.data()).range_of(tokens[0])));

    // Receiver side.
    Tier2Decoded d;
    ITEST_TRUE(decode(boosted, &ctx, d) == Tier2Status::Ok && d.text == text);
    ITEST_TRUE(decode(boosted, nullptr, d) == Tier2Status::ContextRequired && d.text.empty());
    Context stale;
    init_context(stale);
    ITEST_TRUE(decode(boosted, &stale, d) == Tier2Status::ContextMismatch && d.text.empty());
    ITEST_TRUE(d.metadata.seq == 7u && d.metadata.hash_present);   // metadata still reported
    // The unboosted resend is the recovery path (§6.5).
    ITEST_TRUE(decode(plain, &stale, d) == Tier2Status::Ok && d.text == text);
    ITEST_TRUE(decode(plain, nullptr, d) == Tier2Status::Ok && d.text == text);

    // Why the gate exists: the boosted bytes under the unboosted table desynchronise.
    ITEST_TRUE(parse_metadata(boosted.bytes, boosted.len, md, offset) == ParseStatus::Ok);
    std::vector<Symbol> wrong(md.symbol_count);
    ITEST_TRUE(decode_symbols(boosted.bytes, boosted.len, offset, md.symbol_count, reference, wrong.data(),
                              static_cast<u32>(wrong.size())) == ParseStatus::Ok);
    ITEST_TRUE(wrong != tokens);
}

ITEST(unboosted_encoding_decodes_with_any_context_state) {
    t2fx::XorShift32 rng{0xA11CE5u};
    std::vector<Context> contexts;
    Context start;
    init_context(start);
    contexts.push_back(start);
    for (u32 i = 0u; i < 64u; ++i) {                       // arbitrary field values
        Context r;
        for (u32 s = 0u; s < kSlotCount; ++s) {
            r.slots[s].current   = static_cast<u16>(rng.next());
            r.slots[s].recent[0] = static_cast<u16>(rng.next());
            r.slots[s].recent[1] = static_cast<u16>(rng.next());
            r.slots[s].ver       = static_cast<u8>(rng.next());
            r.slots[s].age       = static_cast<u8>(rng.next());
        }
        r.context_id = static_cast<u16>(rng.next());
        r.hash       = static_cast<u16>(rng.next());
        r.seq        = static_cast<u8>(rng.next());
        contexts.push_back(r);
    }
    Context walked = start;                                  // states reached through commit()
    for (u32 i = 0u; i < 64u; ++i) {
        CommitPayload p{};
        p.seq = static_cast<u8>(i);
        const u32 slot = rng.next() % kConceptSlotCount;
        p.slots[slot].op    = SlotOp::Write;
        p.slots[slot].value = static_cast<u16>(1u + rng.next() % 100u);
        ITEST_TRUE(commit(walked, p) == CommitResult::Ok);
        contexts.push_back(walked);
    }

    u32 decodes = 0u;
    for (const t2fx::Utterance& u : fx().corpus) {
        NativePayload p;
        ITEST_TRUE(encode(u.text, nullptr, p) == Tier2Status::Ok);
        Tier2Decoded d;
        ITEST_TRUE(decode(p, nullptr, d) == Tier2Status::Ok && d.text == u.text);
        for (const Context& ctx : contexts) {
            const bool ok = decode(p, &ctx, d) == Tier2Status::Ok && d.text == u.text && !d.metadata.hash_present;
            ITEST_TRUE(ok);
            ++decodes;
        }
    }
    std::printf("  unboosted: %zu utterances x %zu context states, %u exact decodes\n", fx().corpus.size(),
                contexts.size(), decodes);
}

// ---------------------------------------------------------------------------
// Clause boundary — tier §6.6, T4
// ---------------------------------------------------------------------------

ITEST(table_resets_at_every_clause_boundary) {
    const Tier2Tables& t = fx().tables;
    const std::vector<t2fx::Utterance>& corpus = fx().corpus;

    std::vector<NativePayload> alone(corpus.size());
    for (std::size_t i = 0u; i < corpus.size(); ++i) ITEST_TRUE(encode(corpus[i].text, nullptr, alone[i]) == Tier2Status::Ok);

    // Whatever came before, in either order, through one reused Tier2Clause: identical payloads.
    Tier2Clause clause(t);
    Tier2Message m;
    m.seq      = 7u;
    m.language = 1u;
    NativePayload p;
    for (u32 pass = 0u; pass < 2u; ++pass) {
        for (std::size_t k = 0u; k < corpus.size(); ++k) {
            const std::size_t i = pass == 0u ? k : corpus.size() - 1u - k;
            const std::string& text = corpus[i].text;
            ITEST_TRUE(clause.prepare(reinterpret_cast<const u8*>(text.data()), text.size(), m) == Tier2Status::Ok);
            ITEST_TRUE(assemble(clause.assembly_input(), p) == AsmResult::Ok && same_payload(p, alone[i]));
        }
    }

    // The first position never sees a previous token: model_at(0, ·) ignores the prefix.
    const Tier2Model model(t.ngram(), nullptr);
    const u32 v = t.vocabulary().size();
    std::vector<u32> first;
    const Symbol x[1] = {'x'};
    const Symbol y[1] = {v - 1u};
    {
        const Model& m0 = model.model_at(0u, x);
        for (u32 s = 0u; s < v; ++s) first.push_back(m0.range_of(s).high);
    }
    const Model& m1 = model.model_at(0u, y);
    u32 differing = 0u;
    for (u32 s = 0u; s < v; ++s) {
        if (m1.range_of(s).high != first[s]) ++differing;
    }
    ITEST_EQ(differing, 0u);

    // Nothing is learned: every distribution is the same before and after coding the corpus repeatedly.
    const auto snapshot = [&model, v]() {
        std::vector<u64> sums;
        Symbol h[1] = {0u};
        for (u32 history = 0u; history < v; history += 3u) {
            h[0] = history;
            const Model& mm = model.model_at(1u, h);
            u64 acc = mm.total();
            for (u32 s = 0u; s < v; s += 5u) acc = acc * 1000003u + mm.range_of(s).high;
            sums.push_back(acc);
        }
        return sums;
    };
    const std::vector<u64> before = snapshot();
    Tier2Decoded d;
    for (u32 round = 0u; round < 5u; ++round) {
        for (const t2fx::Utterance& u : corpus) {
            ITEST_TRUE(encode(u.text, nullptr, p) == Tier2Status::Ok && decode(p, nullptr, d) == Tier2Status::Ok &&
                       d.text == u.text);
        }
    }
    ITEST_TRUE(snapshot() == before);
}

// ---------------------------------------------------------------------------
// symbol_count, bounds, arguments
// ---------------------------------------------------------------------------

ITEST(symbol_count_is_the_token_count) {
    const SubwordVocabulary& v = fx().tables.vocabulary();
    std::vector<std::string> texts;
    for (const t2fx::Utterance& u : fx().corpus) texts.push_back(u.text);
    texts.push_back(fx().corpus.at(0).text + " " + fx().corpus.at(26).text + " " + std::string(40u, 'q'));
    texts.push_back(std::string());

    u32 escaped = 0u;
    for (const std::string& text : texts) {
        NativePayload p;
        ITEST_TRUE(encode(text, nullptr, p) == Tier2Status::Ok);
        Metadata md;
        u32 offset = 0u;
        ITEST_TRUE(parse_metadata(p.bytes, p.len, md, offset) == ParseStatus::Ok);
        const std::size_t tokens = tokens_of(v, text).size();
        ITEST_EQ(md.symbol_count, tokens);
        if (tokens > 30u) ++escaped;
    }
    ITEST_TRUE(escaped != 0u);   // the 11-bit escape form is exercised too
}

ITEST(more_than_2078_tokens_is_refused_never_truncated) {
    const Context ctx = context_with({{SLOT_LOCATION, "bridge"}});
    const std::string max = t2fx::repeat_byte(kMaxSymbolCount, 0xFFu);
    for (const Context* c : {static_cast<const Context*>(nullptr), &ctx}) {
        NativePayload p;
        ITEST_TRUE(encode(max, c, p) == Tier2Status::Ok);
        Tier2Decoded d;
        ITEST_TRUE(decode(p, c, d) == Tier2Status::Ok && d.text == max && d.metadata.symbol_count == kMaxSymbolCount);
    }

    NativePayload p;
    const std::string over = t2fx::repeat_byte(kMaxSymbolCount + 1u, 0xFFu);
    ITEST_TRUE(encode(over, nullptr, p) == Tier2Status::TooLong && p.len == 0u);

    // The limit is on tokens, not bytes: more than 2078 bytes of subwords still encodes.
    const SubwordVocabulary& v = fx().tables.vocabulary();
    std::string longest;
    for (u32 id = kByteTokenCount; id < v.size(); ++id) {
        if (v.token_bytes(id).size() > longest.size()) longest = std::string(v.token_bytes(id));
    }
    std::string many;
    while (many.size() <= kMaxSymbolCount) many += longest;
    const std::size_t tokens = tokens_of(v, many).size();
    ITEST_TRUE(tokens <= kMaxSymbolCount);
    Tier2Decoded d;
    ITEST_TRUE(encode(many, nullptr, p) == Tier2Status::Ok && decode(p, nullptr, d) == Tier2Status::Ok && d.text == many);
}

ITEST(invalid_arguments_and_non_tier2_payloads) {
    const std::string text = "Send water";
    const auto* bytes = reinterpret_cast<const u8*>(text.data());
    NativePayload p;
    Tier2Message m;
    Tier2Decoded d;

    m.language = 16u;
    ITEST_TRUE(tier2_encode(fx().tables, bytes, text.size(), m, p) == Tier2Status::InvalidArgument && p.len == 0u);
    m.language = 15u;
    ITEST_TRUE(tier2_encode(fx().tables, bytes, text.size(), m, p) == Tier2Status::Ok);
    ITEST_TRUE(decode(p, nullptr, d) == Tier2Status::Ok && d.metadata.language == 15u && d.text == text);
    m.language = 0u;
    ITEST_TRUE(tier2_encode(fx().tables, nullptr, 3u, m, p) == Tier2Status::InvalidArgument);

    Tier2Tables unloaded;
    ITEST_TRUE(tier2_encode(unloaded, bytes, text.size(), m, p) == Tier2Status::InvalidArgument);
    ITEST_TRUE(tier2_encode(fx().tables, bytes, text.size(), m, p) == Tier2Status::Ok);
    ITEST_TRUE(tier2_decode(unloaded, p.bytes, p.len, nullptr, d) == Tier2Status::InvalidArgument);

    // Metadata that cannot be parsed.
    ITEST_TRUE(tier2_decode(fx().tables, p.bytes, 1u, nullptr, d) == Tier2Status::Malformed);
    ITEST_TRUE(tier2_decode(fx().tables, nullptr, 0u, nullptr, d) == Tier2Status::Malformed);

    // A Tier 1 payload is never decoded as Tier 2 text.
    StaticModel sm;
    const u32 freqs[2] = {1u, 1u};
    ITEST_TRUE(sm.assign(freqs, 2u));
    const FixedModel fixed(sm);
    const Symbol one[1] = {1u};
    AssemblyInput in;
    in.tier         = Tier::Tier1;
    in.symbols      = one;
    in.symbol_count = 1u;
    in.model        = &fixed;
    NativePayload tier1;
    ITEST_TRUE(assemble(in, tier1) == AsmResult::Ok);
    ITEST_TRUE(tier2_decode(fx().tables, tier1.bytes, tier1.len, nullptr, d) == Tier2Status::NotTier2);
}

// ---------------------------------------------------------------------------
// Input text — tier §6.1
// ---------------------------------------------------------------------------

ITEST(clause_inputs_partition_the_utterance_at_clause_starts) {
    u32 multi = 0u;
    u32 clauses = 0u;
    for (const t2fx::Utterance& u : fx().corpus) {
        const UtteranceExtraction x = fx().lang.extract(u.lang, u.text);
        const std::vector<SourceSpan> spans = tier2_clause_inputs(x, u.text.size());
        ITEST_EQ(spans.size(), x.clauses.size());
        std::string joined;
        u32 expect_begin = 0u;
        for (std::size_t k = 0u; k < spans.size(); ++k) {
            ITEST_EQ(spans[k].begin, expect_begin);
            ITEST_TRUE(spans[k].end > spans[k].begin);
            if (k != 0u) ITEST_EQ(spans[k].begin, x.clauses[k].source.begin);
            // The clause the language layer found lies inside its Tier 2 input.
            ITEST_TRUE(x.clauses[k].source.begin >= spans[k].begin && x.clauses[k].source.end <= spans[k].end);
            joined += u.text.substr(spans[k].begin, spans[k].end - spans[k].begin);
            expect_begin = spans[k].end;
            ++clauses;
        }
        ITEST_TRUE(joined == u.text);
        if (!spans.empty()) ITEST_EQ(spans.back().end, u.text.size());
        if (spans.size() > 1u) ++multi;
    }
    ITEST_TRUE(multi >= 3u);
    std::printf("  partition: %zu utterances, %u clauses, %u multi-clause\n", fx().corpus.size(), clauses, multi);
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
    return ::itest::run_all("unit.tier2");
}
