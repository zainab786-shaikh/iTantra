// Unit tests — packet/assemble and packet/parse. Implementation plan Phase 3.
//
//   assemble → parse → identical AssemblyInput
//   padding never read; symbol_count bounds the decode
//   symbol_count escape path (>= 31) round-trips through a whole payload
//
// Plus the complete pre-encryption payload checked bit for bit by hand,
// field-range rejection, parser rejection paths, and the worst-case size.

#include "coder/static_model.h"
#include "packet/assemble.h"
#include "packet/parse.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

using namespace itantra;

namespace {

struct XorShift32 {
    u32 s;
    u32 next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    u32 below(u32 n) { return n == 0u ? 0u : next() % n; }
};

StaticModel table_of(const std::vector<u32>& f) {
    StaticModel m;
    const bool ok = m.assign(f.data(), static_cast<u32>(f.size()));
    ITEST_TRUE(ok);
    return m;
}

// Table for position i = tables[i % n]. Counts model_at() calls.
class CyclicModel final : public PayloadModel {
public:
    explicit CyclicModel(const std::vector<StaticModel>& tables) : tables_(&tables) {}
    const Model& model_at(u32 position, const Symbol*) const noexcept override {
        ++calls_;
        return (*tables_)[position % static_cast<u32>(tables_->size())];
    }
    u32  calls() const { return calls_; }
    void reset() { calls_ = 0u; }

private:
    const std::vector<StaticModel>* tables_;
    mutable u32 calls_ = 0u;
};

// Table depends on the previous symbol — proves the decoder hands the model
// the symbols it has decoded, exactly as the encoder handed it the input.
class DependentModel final : public PayloadModel {
public:
    explicit DependentModel(const std::vector<StaticModel>& tables) : tables_(&tables) {}
    const Model& model_at(u32 position, const Symbol* preceding) const noexcept override {
        if (position == 0u) return (*tables_)[0];
        return (*tables_)[1u + (preceding[position - 1u] % 2u)];
    }

private:
    const std::vector<StaticModel>* tables_;
};

class FixedSelector final : public ModelSelector {
public:
    explicit FixedSelector(const PayloadModel* m) : m_(m) {}
    const PayloadModel* select(const Metadata&) const noexcept override { return m_; }

private:
    const PayloadModel* m_;
};

// Bits up to the end of the flush — everything after is padding.
u32 coded_bit_length(const AssemblyInput& in) {
    std::vector<u8> scratch(kMaxPayloadBytes);
    BitWriter w(scratch.data(), kMaxPayloadBytes);
    write_metadata(w, metadata_of(in));
    ArithmeticEncoder enc(w);
    for (u32 i = 0u; i < in.symbol_count; ++i) enc.encode(in.model->model_at(i, in.symbols), in.symbols[i]);
    enc.finish();
    return w.bit_length();
}

bool round_trips(const AssemblyInput& in) {
    auto payload = std::make_unique<NativePayload>();
    auto parsed  = std::make_unique<ParsedPayload>();
    if (assemble(in, *payload) != AsmResult::Ok) return false;
    const FixedSelector selector(in.model);
    if (parse(payload->bytes, payload->len, selector, *parsed) != ParseStatus::Ok) return false;
    if (parsed->metadata != metadata_of(in)) return false;
    if (parsed->metadata_bits != payload->metadata_bits) return false;
    for (u32 i = 0u; i < in.symbol_count; ++i) {
        if (parsed->symbols[i] != in.symbols[i]) return false;
    }
    return true;
}

std::vector<Symbol> weighted(XorShift32& rng, const PayloadModel& model, u32 count) {
    std::vector<Symbol> out(count);
    for (u32 i = 0u; i < count; ++i) {
        const Model& m = model.model_at(i, out.data());
        SymbolRange r{0u, 0u, 0u};
        out[i] = m.find(rng.below(m.total()), r);
    }
    return out;
}

AssemblyInput input(Tier tier, const std::vector<Symbol>& symbols, const PayloadModel& model) {
    AssemblyInput in;
    in.tier = tier;
    in.symbols = symbols.empty() ? nullptr : symbols.data();
    in.symbol_count = static_cast<u16>(symbols.size());
    in.model = &model;
    return in;
}

}  // namespace

// ---------------------------------------------------------------------------
// The complete pre-encryption payload, by hand
// ---------------------------------------------------------------------------

ITEST(complete_tier1_payload_matches_hand_computed_bytes) {
    //   tier 01 | count 00001 | seq 10100101 | hash_present 0 | priority 1 |
    //   negation 11 | payload 1 ({1,1}, symbol 1) | flush 01 | pad 00
    //   0100 0011  0100 1010  1111 0100    = 43 4A F4
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {1u};
    AssemblyInput in = input(Tier::Tier1, symbols, model);
    in.seq = 0xA5u;
    in.priority = Priority::Critical;
    in.negation = true;

    auto p = std::make_unique<NativePayload>();
    std::memset(p->bytes, 0xFF, sizeof p->bytes);
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
    ITEST_EQ(p->len, 3u);
    ITEST_EQ(p->metadata_bits, 19u);
    ITEST_EQ(p->bytes[0], 0x43u);
    ITEST_EQ(p->bytes[1], 0x4Au);
    ITEST_EQ(p->bytes[2], 0xF4u);
    ITEST_TRUE(round_trips(in));
}

ITEST(complete_tier2_empty_payload_with_hash_matches_hand_computed_bytes) {
    //   tier 10 | count 00000 | seq 0 | hash_present 1 | priority 0 |
    //   language 1111 | hash 1010 1011 1100 | flush 01 | pad 00000
    //   1000 0000  0000 0001  0111 1101  0101 1110  0010 0000   = 80 01 7D 5E 20
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    AssemblyInput in = input(Tier::Tier2, {}, model);
    in.hash_present = true;
    in.language = 0xFu;
    in.context_hash = 0xABCu;

    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
    ITEST_EQ(p->len, 5u);
    ITEST_EQ(p->metadata_bits, 33u);
    const u8 expect[5] = {0x80, 0x01, 0x7D, 0x5E, 0x20};
    for (u32 i = 0u; i < 5u; ++i) ITEST_EQ(p->bytes[i], expect[i]);
    ITEST_TRUE(round_trips(in));
}

ITEST(payload_is_metadata_then_coder_bits_then_two_flush_bits_then_zero_padding) {
    XorShift32 rng{0x7A3B9C1Du};
    for (u32 round = 0u; round < 500u; ++round) {
        std::vector<u32> f(1u + rng.below(200u));
        for (u32& x : f) x = rng.below(50u);
        f[rng.below(static_cast<u32>(f.size()))] = 1u + rng.below(50u);
        const std::vector<StaticModel> tables = {table_of(f)};
        const CyclicModel model(tables);
        const std::vector<Symbol> symbols = weighted(rng, model, rng.below(60u));
        AssemblyInput in = input(rng.below(2u) == 0u ? Tier::Tier1 : Tier::Tier2, symbols, model);
        in.hash_present = rng.below(2u) == 0u;
        in.context_hash = in.hash_present ? static_cast<u16>(rng.below(4096u)) : 0u;

        auto p = std::make_unique<NativePayload>();
        std::memset(p->bytes, 0xFF, sizeof p->bytes);
        ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
        ITEST_EQ(p->metadata_bits, metadata_bit_length(metadata_of(in)));

        // the same bits, produced independently from the parts
        u8 expect[512] = {};
        BitWriter w(expect, 512u);
        ITEST_TRUE(write_metadata(w, metadata_of(in)));
        ArithmeticEncoder enc(w);
        for (u32 i = 0u; i < in.symbol_count; ++i) ITEST_TRUE(enc.encode(tables[0], symbols[i]));
        const u32 committed = enc.committed_bits();
        ITEST_TRUE(enc.finish());
        const u32 end_bits = p->metadata_bits + committed + kCoderFlushBits;
        ITEST_EQ(w.bit_length(), end_bits);
        ITEST_EQ(p->len, (end_bits + 7u) / 8u);
        ITEST_TRUE(std::memcmp(p->bytes, expect, p->len) == 0);

        const u32 pad = p->len * 8u - end_bits;
        ITEST_TRUE(pad < 8u);
        ITEST_EQ(p->bytes[p->len - 1u] & ((1u << pad) - 1u), 0u);   // padding is zeros
    }
}

// ---------------------------------------------------------------------------
// assemble → parse → identical AssemblyInput
// ---------------------------------------------------------------------------

ITEST(assemble_then_parse_returns_the_identical_input) {
    XorShift32 rng{0x0BADF00Du};
    for (u32 round = 0u; round < 3000u; ++round) {
        std::vector<StaticModel> tables;
        const u32 table_count = 1u + rng.below(3u);
        for (u32 t = 0u; t < table_count; ++t) {
            std::vector<u32> f(1u + rng.below(300u));
            for (u32& x : f) x = rng.below(4u) == 0u ? 0u : rng.below(1000u);
            f[rng.below(static_cast<u32>(f.size()))] = 1u + rng.below(1000u);
            tables.push_back(table_of(f));
        }
        const CyclicModel model(tables);

        const u32 size_class = rng.below(100u);
        const u32 count = size_class < 70u ? rng.below(32u)
                        : size_class < 95u ? rng.below(200u)
                                           : rng.below(kMaxSymbolCount + 1u);
        const std::vector<Symbol> symbols = weighted(rng, model, count);

        AssemblyInput in = input(rng.below(2u) == 0u ? Tier::Tier1 : Tier::Tier2, symbols, model);
        in.seq = static_cast<u8>(rng.next());
        in.priority = rng.below(2u) == 0u ? Priority::Normal : Priority::Critical;
        in.hash_present = rng.below(2u) == 0u;
        in.context_hash = in.hash_present ? static_cast<u16>(rng.below(4096u)) : 0u;
        if (in.tier == Tier::Tier1) {
            in.negation = rng.below(2u) == 0u;
        } else {
            in.language = static_cast<LangId>(rng.below(16u));
        }
        ITEST_TRUE(round_trips(in));
    }
}

ITEST(decoder_hands_the_model_the_symbols_it_decoded) {
    const std::vector<StaticModel> tables = {
        table_of({5u, 5u, 5u, 5u}), table_of({1u, 0u, 9u, 3u}), table_of({0u, 7u, 7u, 1u})};
    const DependentModel model(tables);
    XorShift32 rng{0x13579BDFu};
    for (u32 round = 0u; round < 500u; ++round) {
        const std::vector<Symbol> symbols = weighted(rng, model, rng.below(80u));
        AssemblyInput in = input(Tier::Tier1, symbols, model);
        ITEST_TRUE(round_trips(in));
    }
}

ITEST(symbol_count_escape_boundaries_round_trip_through_a_payload) {
    const std::vector<StaticModel> tables = {table_of({3u, 1u, 4u, 1u, 5u, 9u, 2u, 6u})};
    const CyclicModel model(tables);
    XorShift32 rng{0x55AA55AAu};
    const u32 counts[] = {29u, 30u, 31u, 32u, 33u, 1000u, kMaxSymbolCount};
    for (u32 count : counts) {
        for (u32 tier = 1u; tier <= 2u; ++tier) {
            for (u32 hp = 0u; hp < 2u; ++hp) {
                const std::vector<Symbol> symbols = weighted(rng, model, count);
                AssemblyInput in = input(static_cast<Tier>(tier), symbols, model);
                in.hash_present = hp != 0u;
                in.context_hash = hp != 0u ? 0xFFFu : 0u;
                auto p = std::make_unique<NativePayload>();
                ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
                const u32 base = tier == 1u ? (hp ? 31u : 19u) : (hp ? 33u : 21u);
                ITEST_EQ(p->metadata_bits, base + (count >= 31u ? 11u : 0u));
                ITEST_TRUE(round_trips(in));
            }
        }
    }
}

ITEST(more_symbols_than_the_escape_range_is_too_long) {
    const std::vector<StaticModel> tables = {table_of({1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols(kMaxSymbolCount + 1u, 0u);
    AssemblyInput in = input(Tier::Tier2, symbols, model);
    in.symbol_count = static_cast<u16>(kMaxSymbolCount + 1u);
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::TooLong);
    ITEST_EQ(p->len, 0u);
    ITEST_EQ(p->metadata_bits, 0u);
}

ITEST(worst_case_payload_fits_the_native_payload_buffer) {
    // 2078 symbols at p = 2^-24, the largest escape metadata.
    const std::vector<StaticModel> tables = {table_of({1u, kModelMaxTotal - 2u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols(kMaxSymbolCount, 0u);
    AssemblyInput in = input(Tier::Tier2, symbols, model);
    in.hash_present = true;
    in.context_hash = 0xFFFu;
    in.language = 15u;
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
    ITEST_TRUE(p->len <= kMaxPayloadBytes);
    ITEST_TRUE(p->len > (kMaxSymbolCount * 24u) / 8u);
    ITEST_TRUE(round_trips(in));
}

ITEST(identical_input_gives_identical_bytes_whatever_the_buffer_held) {
    const std::vector<StaticModel> tables = {table_of({7u, 1u, 0u, 3u, 12u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {0u, 4u, 4u, 1u, 3u, 0u, 4u};
    AssemblyInput in = input(Tier::Tier1, symbols, model);
    in.seq = 42u;

    auto a = std::make_unique<NativePayload>();
    auto b = std::make_unique<NativePayload>();
    std::memset(a->bytes, 0x00, sizeof a->bytes);
    std::memset(b->bytes, 0xFF, sizeof b->bytes);
    ITEST_TRUE(assemble(in, *a) == AsmResult::Ok);
    ITEST_TRUE(assemble(in, *b) == AsmResult::Ok);
    ITEST_EQ(a->len, b->len);
    ITEST_TRUE(std::memcmp(a->bytes, b->bytes, a->len) == 0);
}

// ---------------------------------------------------------------------------
// Padding never read; symbol_count bounds the decode
// ---------------------------------------------------------------------------

ITEST(padding_and_trailing_bytes_are_never_read) {
    XorShift32 rng{0xC001D00Du};
    for (u32 round = 0u; round < 500u; ++round) {
        std::vector<u32> f(1u + rng.below(64u));
        for (u32& x : f) x = 1u + rng.below(20u);
        const std::vector<StaticModel> tables = {table_of(f)};
        const CyclicModel model(tables);
        const std::vector<Symbol> symbols = weighted(rng, model, rng.below(50u));
        AssemblyInput in = input(rng.below(2u) == 0u ? Tier::Tier1 : Tier::Tier2, symbols, model);

        auto p = std::make_unique<NativePayload>();
        ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
        const u32 end_bits = coded_bit_length(in);
        const u32 pad = p->len * 8u - end_bits;

        std::vector<u8> dirty(p->bytes, p->bytes + p->len);
        dirty[p->len - 1u] = static_cast<u8>(dirty[p->len - 1u] | (rng.next() & ((1u << pad) - 1u)));
        for (u32 i = 0u; i < 32u; ++i) dirty.push_back(static_cast<u8>(rng.next()));

        auto parsed = std::make_unique<ParsedPayload>();
        const FixedSelector selector(&model);
        ITEST_TRUE(parse(dirty.data(), static_cast<u32>(dirty.size()), selector, *parsed) == ParseStatus::Ok);
        ITEST_TRUE(parsed->metadata == metadata_of(in));
        for (u32 i = 0u; i < in.symbol_count; ++i) ITEST_EQ(parsed->symbols[i], symbols[i]);
    }
}

ITEST(symbol_count_is_the_only_stopping_condition) {
    const std::vector<StaticModel> tables = {table_of({9u, 3u, 1u, 1u, 6u}), table_of({2u, 2u})};
    CyclicModel model(tables);
    XorShift32 rng{0xFEEDFACEu};
    const std::vector<Symbol> symbols = weighted(rng, model, 120u);
    AssemblyInput in = input(Tier::Tier2, symbols, model);
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);

    Metadata md;
    u32 offset = 0u;
    ITEST_TRUE(parse_metadata(p->bytes, p->len, md, offset) == ParseStatus::Ok);
    ITEST_EQ(offset, p->metadata_bits);

    // The decoder asks for exactly symbol_count tables and stops.
    std::vector<Symbol> out(120u, 0xFFFFFFFFu);
    model.reset();
    ITEST_TRUE(decode_symbols(p->bytes, p->len, offset, md.symbol_count, model, out.data(), 120u) ==
               ParseStatus::Ok);
    ITEST_EQ(model.calls(), 120u);
    ITEST_TRUE(out == symbols);

    // A smaller count decodes exactly that prefix and touches nothing more.
    for (u32 k = 0u; k <= 120u; k += 7u) {
        std::vector<Symbol> prefix(k + 1u, 0xFFFFFFFFu);
        model.reset();
        ITEST_TRUE(decode_symbols(p->bytes, p->len, offset, static_cast<u16>(k), model, prefix.data(),
                                  k + 1u) == ParseStatus::Ok);
        ITEST_EQ(model.calls(), k);
        for (u32 i = 0u; i < k; ++i) ITEST_EQ(prefix[i], symbols[i]);
        ITEST_EQ(prefix[k], 0xFFFFFFFFu);
    }
}

// ---------------------------------------------------------------------------
// Rejection
// ---------------------------------------------------------------------------

ITEST(assemble_rejects_fields_outside_their_range_or_tier) {
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {0u, 1u};
    auto p = std::make_unique<NativePayload>();

    AssemblyInput in = input(Tier::Tier2, symbols, model);
    in.negation = true;
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier1, symbols, model);
    in.language = 3u;
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier2, symbols, model);
    in.language = 16u;
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier1, symbols, model);
    in.context_hash = 0x001u;   // no hash_present
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(static_cast<Tier>(0u), symbols, model);
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);
    in = input(static_cast<Tier>(3u), symbols, model);
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier1, symbols, model);
    in.priority = static_cast<Priority>(2u);
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier1, symbols, model);
    in.model = nullptr;
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    in = input(Tier::Tier1, symbols, model);
    in.symbols = nullptr;
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);

    ITEST_EQ(p->len, 0u);
}

ITEST(parse_rejects_bad_tier_truncation_and_negation_mismatch_before_decoding) {
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    CyclicModel model(tables);
    const FixedSelector selector(&model);
    auto parsed = std::make_unique<ParsedPayload>();

    std::vector<Symbol> symbols(40u, 1u);
    AssemblyInput in = input(Tier::Tier1, symbols, model);
    in.negation = true;
    in.hash_present = true;
    in.context_hash = 0x5A5u;
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);
    ITEST_EQ(p->metadata_bits, 42u);

    // truncated inside the metadata
    for (u32 len = 0u; len * 8u < p->metadata_bits; ++len) {
        model.reset();
        ITEST_TRUE(parse(len == 0u ? nullptr : p->bytes, len, selector, *parsed) == ParseStatus::Truncated);
        ITEST_EQ(model.calls(), 0u);
    }

    // tier 00 and 11
    std::vector<u8> bad(p->bytes, p->bytes + p->len);
    bad[0] = static_cast<u8>(bad[0] & 0x3Fu);
    model.reset();
    ITEST_TRUE(parse(bad.data(), p->len, selector, *parsed) == ParseStatus::BadTier);
    bad[0] = static_cast<u8>(bad[0] | 0xC0u);
    ITEST_TRUE(parse(bad.data(), p->len, selector, *parsed) == ParseStatus::BadTier);
    ITEST_EQ(model.calls(), 0u);

    // one negation copy flipped: bits 28 and 29 after the 11-bit escape
    for (u32 bit = 28u; bit <= 29u; ++bit) {
        std::vector<u8> flipped(p->bytes, p->bytes + p->len);
        flipped[bit / 8u] = static_cast<u8>(flipped[bit / 8u] ^ (0x80u >> (bit % 8u)));
        model.reset();
        ITEST_TRUE(parse(flipped.data(), p->len, selector, *parsed) == ParseStatus::NegationMismatch);
        ITEST_EQ(model.calls(), 0u);
    }

    // both copies flipped is a consistent (different) negation — the metadata
    // block alone cannot tell; AEAD does (Phase 4, C-41).
    std::vector<u8> both(p->bytes, p->bytes + p->len);
    both[3] = static_cast<u8>(both[3] ^ 0x0Cu);
    ITEST_TRUE(parse(both.data(), p->len, selector, *parsed) == ParseStatus::Ok);
    ITEST_TRUE(!parsed->metadata.negation);
}

ITEST(parse_without_a_model_for_the_tier_is_no_model) {
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {1u};
    const AssemblyInput in = input(Tier::Tier2, symbols, model);
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::Ok);

    const FixedSelector none(nullptr);
    auto parsed = std::make_unique<ParsedPayload>();
    ITEST_TRUE(parse(p->bytes, p->len, none, *parsed) == ParseStatus::NoModel);
}

ITEST(decode_symbols_rejects_invalid_arguments) {
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    const u8 bytes[4] = {0x40, 0x00, 0x00, 0x00};
    Symbol out[4] = {};
    ITEST_TRUE(decode_symbols(bytes, 4u, 19u, 5u, model, out, 4u) == ParseStatus::InvalidArgument);
    ITEST_TRUE(decode_symbols(bytes, 4u, 19u, 1u, model, nullptr, 4u) == ParseStatus::InvalidArgument);
    ITEST_TRUE(decode_symbols(bytes, 4u, 19u, 0u, model, nullptr, 0u) == ParseStatus::Ok);
}

#ifdef NDEBUG

ITEST(release_hash_present_with_a_too_wide_hash_is_rejected) {
    const std::vector<StaticModel> tables = {table_of({1u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {1u};
    AssemblyInput in = input(Tier::Tier1, symbols, model);
    in.hash_present = true;
    in.context_hash = 0x1000u;
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::InvalidField);
    ITEST_EQ(p->len, 0u);
}

ITEST(release_zero_probability_symbol_is_a_coder_failure_with_no_payload) {
    const std::vector<StaticModel> tables = {table_of({1u, 0u, 1u})};
    const CyclicModel model(tables);
    const std::vector<Symbol> symbols = {0u, 1u};
    const AssemblyInput in = input(Tier::Tier2, symbols, model);
    auto p = std::make_unique<NativePayload>();
    ITEST_TRUE(assemble(in, *p) == AsmResult::CoderFailure);
    ITEST_EQ(p->len, 0u);
    ITEST_EQ(p->metadata_bits, 0u);
}

#endif  // NDEBUG

namespace {

int expect_assert(const char* which) {
#ifdef NDEBUG
    std::printf("ASSERTIONS-DISABLED: '%s' cannot abort in an NDEBUG build; "
                "the release_* cases cover this configuration\n", which);
    return 0;
#else
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    std::printf("expect-assert: %s\n", which);
    std::fflush(stdout);

    if (std::strcmp(which, "assemble-hash-too-wide") == 0) {
        const std::vector<StaticModel> tables = {table_of({1u, 1u})};
        const CyclicModel model(tables);
        const std::vector<Symbol> symbols = {1u};
        AssemblyInput in = input(Tier::Tier1, symbols, model);
        in.hash_present = true;
        in.context_hash = 0x1000u;
        auto p = std::make_unique<NativePayload>();
        assemble(in, *p);
    } else {
        std::printf("UNKNOWN-CASE: %s\n", which);
        return 2;
    }
    std::printf("NOT-ABORTED: %s returned instead of asserting\n", which);
    return 1;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--expect-assert") == 0) return expect_assert(argv[2]);
    return ::itest::run_all("unit.assemble");
}
