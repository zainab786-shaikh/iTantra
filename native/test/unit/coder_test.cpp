// Unit tests — coder/. Implementation plan Phase 2.
//
//   encode → decode → identical symbols, 10k random sequences
//   flush emits a FIXED bit count, independent of model or content
//   every symbol with p > 0 encodes and decodes
//   a symbol with p == 0 is a programming error → assert, not garbage
//   two coder instances with identical models produce identical bytes
//
// The assert cases end the process, so they cannot run inside this harness.
// `coder_test --expect-assert <case>` triggers one contract violation; CTest
// runs each case in its own process and passes it only if that assert's
// message appears (native/CMakeLists.txt). In an NDEBUG build asserts are
// compiled out, those cases report themselves skipped, and the NDEBUG-only
// cases below check the release behaviour instead: rejected, nothing written.

#include "coder/coder.h"
#include "coder/static_model.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

using namespace itantra;

namespace {

// Deterministic pseudo-random source, as in bitio_test.cpp. Not <random>: its
// distributions are implementation-defined.
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

StaticModel model_of(const std::vector<u32>& frequencies) {
    StaticModel m;
    const bool ok = m.assign(frequencies.data(), static_cast<u32>(frequencies.size()));
    ITEST_TRUE(ok);
    return m;
}

std::vector<u32> live_symbols(const std::vector<u32>& frequencies) {
    std::vector<u32> live;
    for (u32 i = 0u; i < static_cast<u32>(frequencies.size()); ++i) {
        if (frequencies[i] != 0u) live.push_back(i);
    }
    return live;
}

// No symbol costs more than 26 committed bits (the narrowed interval is at
// least 64 wide, and doubles once per bit until it passes 2^31); 32 is a safe
// bound. Derivation: packet/assemble.h kCoderMaxBitsPerSymbol.
u32 capacity_bytes(u32 prefix_bits, u32 symbols) {
    return (prefix_bits + 32u * symbols + kCoderFlushBits + 7u) / 8u;
}

u32 prefix_bit(u32 i) {
    return (i & 1u) ^ 1u;   // 1010… — a stand-in for packet metadata
}

struct Stream {
    std::vector<u8> bytes;
    u32  prefix_bits             = 0u;
    u32  committed_before_finish = 0u;
    u32  end_bits                = 0u;
    bool ok                      = false;
};

Stream encode_symbols(const Model& m, const std::vector<u32>& symbols, u32 prefix_bits = 0u,
                      u8 fill = 0x00) {
    Stream s;
    const u32 cap = capacity_bytes(prefix_bits, static_cast<u32>(symbols.size()));
    s.bytes.assign(cap, fill);

    BitWriter bw(s.bytes.data(), cap);
    for (u32 i = 0u; i < prefix_bits; ++i) bw.write(prefix_bit(i), 1);

    ArithmeticEncoder enc(bw);
    bool ok = true;
    for (u32 symbol : symbols) ok = enc.encode(m, symbol) && ok;
    s.committed_before_finish = enc.committed_bits();
    ok = enc.finish() && ok;

    s.prefix_bits = prefix_bits;
    s.end_bits    = bw.bit_length();
    s.ok          = ok && enc.ok() && bw.ok();
    s.bytes.resize(bw.byte_length());
    return s;
}

bool decodes_to(const Model& m, const std::vector<u32>& symbols, const u8* bytes, u32 length,
                u32 prefix_bits) {
    BitReader br(bytes, length);
    for (u32 i = 0u; i < prefix_bits; ++i) {
        if (br.read(1) != prefix_bit(i)) return false;
    }
    ArithmeticDecoder dec(br);
    for (u32 expected : symbols) {
        u32 got = 0u;
        if (!dec.decode(m, got) || got != expected) return false;
    }
    return dec.ok();
}

bool decodes_to(const Model& m, const std::vector<u32>& symbols, const Stream& s) {
    return decodes_to(m, symbols, s.bytes.data(), static_cast<u32>(s.bytes.size()), s.prefix_bits);
}

// Random table: alphabet 1…300, about a quarter of the symbols at p == 0.
std::vector<u32> random_frequencies(XorShift32& rng) {
    const u32 n = 1u + rng.below(300u);
    std::vector<u32> f(n);
    for (u32& x : f) x = (rng.below(4u) == 0u) ? 0u : rng.below(1001u);
    f[rng.below(n)] |= 1u;
    return f;
}

std::vector<u32> random_sequence(XorShift32& rng, const std::vector<u32>& live, u32 length) {
    std::vector<u32> symbols(length);
    for (u32& x : symbols) x = live[rng.below(static_cast<u32>(live.size()))];
    return symbols;
}

// Violates M4: find() answers with the symbol after the right one.
class BrokenFindModel final : public Model {
public:
    u32 total() const noexcept override { return 4u; }
    SymbolRange range_of(u32 s) const noexcept override {
        return s < 4u ? SymbolRange{s, s + 1u, 4u} : SymbolRange{0u, 0u, 4u};
    }
    u32 find(u32 target, SymbolRange& r) const noexcept override {
        const u32 wrong = (target + 1u) % 4u;
        r = range_of(wrong);
        return wrong;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Known answers — bit patterns worked out by hand from the coder.h algorithm
// ---------------------------------------------------------------------------

ITEST(known_answer_uniform_binary_symbols_are_their_own_bits) {
    // {1,1}: each symbol halves the interval and emits its own value.
    //   1 → emit 1;  0 → emit 0;  1 → emit 1;  finish, low = 0 < QUARTER → "01"
    //   1 0 1 | 0 1  = 1010 1000 = 0xA8
    const StaticModel m = model_of({1u, 1u});
    const Stream s = encode_symbols(m, {1u, 0u, 1u});
    ITEST_TRUE(s.ok);
    ITEST_EQ(s.committed_before_finish, 3u);
    ITEST_EQ(s.end_bits, 5u);
    ITEST_EQ(s.bytes.size(), 1u);
    ITEST_EQ(s.bytes[0], 0xA8u);
    ITEST_TRUE(decodes_to(m, {1u, 0u, 1u}, s));
}

ITEST(known_answer_middle_symbol_commits_a_follow_bit) {
    // {1,2,1}, symbol 1 → [QUARTER, 3*QUARTER): no bit emitted, follow = 1,
    // interval back to [0, TOP]. finish: follow = 2, emit 0 then 1, 1.
    //   0 1 1  = 0110 0000 = 0x60
    const StaticModel m = model_of({1u, 2u, 1u});
    const Stream s = encode_symbols(m, {1u});
    ITEST_TRUE(s.ok);
    ITEST_EQ(s.committed_before_finish, 1u);
    ITEST_EQ(s.end_bits, 3u);
    ITEST_EQ(s.bytes[0], 0x60u);
    ITEST_TRUE(decodes_to(m, {1u}, s));
}

ITEST(known_answer_no_symbols_is_the_flush_alone) {
    //   finish on [0, TOP]: "01" = 0100 0000 = 0x40
    const StaticModel m = model_of({1u, 1u});
    const Stream s = encode_symbols(m, {});
    ITEST_TRUE(s.ok);
    ITEST_EQ(s.committed_before_finish, 0u);
    ITEST_EQ(s.end_bits, kCoderFlushBits);
    ITEST_EQ(s.bytes[0], 0x40u);
}

ITEST(known_answer_certain_symbol_costs_nothing) {
    // p == 1: the interval never narrows, whatever the count.
    const StaticModel m = model_of({7u});
    const std::vector<u32> symbols(1000u, 0u);
    const Stream s = encode_symbols(m, symbols);
    ITEST_TRUE(s.ok);
    ITEST_EQ(s.committed_before_finish, 0u);
    ITEST_EQ(s.end_bits, kCoderFlushBits);
    ITEST_EQ(s.bytes[0], 0x40u);
    ITEST_TRUE(decodes_to(m, symbols, s));
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

ITEST(encode_decode_10k_random_sequences_identical_symbols) {
    XorShift32 rng{0x2545F491u};
    for (u32 round = 0u; round < 10000u; ++round) {
        const std::vector<u32> f = random_frequencies(rng);
        const StaticModel m = model_of(f);
        const std::vector<u32> symbols = random_sequence(rng, live_symbols(f), rng.below(65u));
        const Stream s = encode_symbols(m, symbols, rng.below(32u));
        ITEST_TRUE(s.ok);
        ITEST_TRUE(decodes_to(m, symbols, s));
    }
}

ITEST(symbols_may_switch_models_within_one_stream) {
    // The Tier 1 shape: intent, then slot modes, then slot values — each from
    // its own table, in one contiguous stream (tier §5.9).
    const StaticModel intents = model_of({40u, 25u, 0u, 20u, 10u, 5u});
    const StaticModel modes   = model_of({6u, 3u, 2u, 1u});
    const StaticModel values  = model_of(std::vector<u32>(1024u, 3u));
    const Model* const order[] = {&intents, &modes, &modes, &values, &modes, &values, &values};
    const u32 symbols[] = {3u, 0u, 2u, 1000u, 3u, 0u, 1023u};

    u8 buf[16] = {};
    BitWriter bw(buf, 16u);
    bw.write(0x5u, 3);
    ArithmeticEncoder enc(bw);
    for (u32 i = 0u; i < 7u; ++i) ITEST_TRUE(enc.encode(*order[i], symbols[i]));
    ITEST_TRUE(enc.finish());

    BitReader br(buf, bw.byte_length());
    ITEST_EQ(br.read(3), 0x5u);
    ArithmeticDecoder dec(br);
    for (u32 i = 0u; i < 7u; ++i) {
        u32 got = 0u;
        ITEST_TRUE(dec.decode(*order[i], got));
        ITEST_EQ(got, symbols[i]);
    }
}

ITEST(payload_starts_on_any_bit_after_the_metadata) {
    // packet §3.6: no byte alignment — the payload starts at the next bit.
    XorShift32 rng{0x3C6EF372u};
    const std::vector<u32> f = {9u, 1u, 4u, 0u, 2u, 7u};
    const StaticModel m = model_of(f);
    const std::vector<u32> symbols = random_sequence(rng, live_symbols(f), 40u);
    for (u32 prefix = 0u; prefix <= 40u; ++prefix) {
        const Stream s = encode_symbols(m, symbols, prefix, 0xFF);
        ITEST_TRUE(s.ok);
        ITEST_EQ(s.end_bits, prefix + s.committed_before_finish + kCoderFlushBits);
        ITEST_TRUE(decodes_to(m, symbols, s));
    }
}

ITEST(decoding_ignores_every_bit_after_the_flush) {
    // packet §3.5: the parser counts symbols and ignores the rest. Zero
    // padding, an early end of buffer, all-ones and random garbage must all
    // decode identically.
    XorShift32 rng{0xA54FF53Au};
    for (u32 round = 0u; round < 500u; ++round) {
        const std::vector<u32> f = random_frequencies(rng);
        const StaticModel m = model_of(f);
        const std::vector<u32> symbols = random_sequence(rng, live_symbols(f), rng.below(40u));
        const Stream s = encode_symbols(m, symbols, rng.below(24u));
        ITEST_TRUE(s.ok);

        const u32 used      = static_cast<u32>(s.bytes.size());
        const u32 tail_bits = used * 8u - s.end_bits;   // pad bits in the last byte
        const u8  tail_mask = static_cast<u8>((1u << tail_bits) - 1u);

        // exactly the written bytes; the reader supplies zeros beyond
        ITEST_TRUE(decodes_to(m, symbols, s));

        for (u32 variant = 0u; variant < 2u; ++variant) {
            std::vector<u8> dirty = s.bytes;
            dirty.resize(used + 8u);
            if (used > 0u) {
                const u8 junk = variant == 0u ? 0xFFu : static_cast<u8>(rng.next());
                dirty[used - 1u] = static_cast<u8>(dirty[used - 1u] | (junk & tail_mask));
            }
            for (u32 i = used; i < used + 8u; ++i) {
                dirty[i] = variant == 0u ? 0xFFu : static_cast<u8>(rng.next());
            }
            ITEST_TRUE(decodes_to(m, symbols, dirty.data(), used + 8u, s.prefix_bits));
        }
    }
}

// ---------------------------------------------------------------------------
// Fixed flush
// ---------------------------------------------------------------------------

ITEST(flush_bit_count_is_the_documented_wire_constant) {
    ITEST_EQ(kCoderFlushBits, 2u);
    ITEST_EQ(kCoderPrecisionBits, 32u);
}

ITEST(flush_emits_a_fixed_bit_count_independent_of_model_and_content) {
    XorShift32 rng{0x510E527Fu};
    for (u32 round = 0u; round < 3000u; ++round) {
        const std::vector<u32> f = random_frequencies(rng);
        const StaticModel m = model_of(f);
        const std::vector<u32> symbols = random_sequence(rng, live_symbols(f), rng.below(65u));
        const u32 prefix = rng.below(32u);
        const Stream s = encode_symbols(m, symbols, prefix);
        ITEST_TRUE(s.ok);
        ITEST_EQ(s.end_bits - prefix - s.committed_before_finish, kCoderFlushBits);
    }
}

ITEST(flush_is_fixed_for_one_content_under_different_models) {
    // Same symbols, very different code lengths; the terminator never moves.
    const std::vector<u32> symbols = {0u, 1u, 2u, 1u, 0u, 2u, 2u, 2u};
    const StaticModel models[] = {
        model_of({1u, 1u, 1u}),
        model_of({1000u, 10u, 1u}),
        model_of({1u, 10u, kModelMaxTotal - 11u}),
        model_of({1u, 2u, 1u}),
    };
    u32 distinct_lengths = 0u;
    u32 previous = 0u;
    for (const StaticModel& m : models) {
        const Stream s = encode_symbols(m, symbols);
        ITEST_TRUE(s.ok);
        ITEST_EQ(s.end_bits - s.committed_before_finish, kCoderFlushBits);
        ITEST_TRUE(decodes_to(m, symbols, s));
        if (s.end_bits != previous) ++distinct_lengths;
        previous = s.end_bits;
    }
    ITEST_TRUE(distinct_lengths >= 3u);
}

ITEST(follow_bits_are_payload_and_the_flush_stays_fixed) {
    // {1,2,1} symbol 1 renormalises by underflow alone: every symbol commits
    // one follow bit and emits nothing until finish resolves all of them.
    const StaticModel m = model_of({1u, 2u, 1u});
    const std::vector<u32> symbols(100000u, 1u);
    const Stream s = encode_symbols(m, symbols);
    ITEST_TRUE(s.ok);
    ITEST_EQ(s.committed_before_finish, 100000u);
    ITEST_EQ(s.end_bits, 100000u + kCoderFlushBits);
    ITEST_TRUE(decodes_to(m, symbols, s));
}

// ---------------------------------------------------------------------------
// Every p > 0 symbol codes
// ---------------------------------------------------------------------------

ITEST(every_symbol_with_nonzero_probability_encodes_and_decodes) {
    XorShift32 rng{0x9B05688Cu};

    std::vector<u32> sparse(4096u);
    for (u32 i = 0u; i < 4096u; ++i) sparse[i] = (i % 3u == 0u) ? 0u : 1u + rng.below(500u);

    const std::vector<std::vector<u32>> tables = {
        {1u},
        {1u, 1u},
        {kModelMaxTotal - 1u, 1u},              // p = 2^-24, the smallest there is
        {1u, kModelMaxTotal - 2u, 1u},
        {0u, 5u, 0u, 0u, 3u, 0u},
        std::vector<u32>(256u, 1u),             // the Tier 2 byte-fallback alphabet
        sparse,
        std::vector<u32>(65536u, 256u),         // 2^16 symbols, total exactly 2^24
    };

    for (const std::vector<u32>& f : tables) {
        const StaticModel m = model_of(f);
        const std::vector<u32> live = live_symbols(f);
        for (u32 symbol : live) {
            const std::vector<u32> alone = {symbol};
            const Stream a = encode_symbols(m, alone);
            ITEST_TRUE(a.ok);
            ITEST_TRUE(decodes_to(m, alone, a));

            const std::vector<u32> between = {
                live[rng.below(static_cast<u32>(live.size()))], symbol, symbol,
                live[rng.below(static_cast<u32>(live.size()))]};
            const Stream b = encode_symbols(m, between, 19u);
            ITEST_TRUE(b.ok);
            ITEST_TRUE(decodes_to(m, between, b));
        }
    }
}

ITEST(minimum_probability_symbol_repeated_round_trips) {
    const StaticModel m = model_of({kModelMaxTotal - 1u, 1u});
    const std::vector<u32> symbols(500u, 1u);
    const Stream s = encode_symbols(m, symbols);
    ITEST_TRUE(s.ok);
    ITEST_TRUE(s.committed_before_finish >= 500u * 24u);
    ITEST_TRUE(s.committed_before_finish <= 500u * 25u);
    ITEST_TRUE(decodes_to(m, symbols, s));
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

ITEST(two_coder_instances_with_identical_models_produce_identical_bytes) {
    XorShift32 rng{0x1F83D9ABu};
    for (u32 round = 0u; round < 1000u; ++round) {
        const std::vector<u32> f = random_frequencies(rng);
        const StaticModel m1 = model_of(f);
        const StaticModel m2 = model_of(f);   // separately built, separate memory
        const std::vector<u32> symbols = random_sequence(rng, live_symbols(f), rng.below(65u));
        const u32 prefix = rng.below(32u);

        const Stream a = encode_symbols(m1, symbols, prefix, 0x00);
        const Stream b = encode_symbols(m2, symbols, prefix, 0xFF);   // dirty buffer
        ITEST_TRUE(a.ok && b.ok);
        ITEST_EQ(a.end_bits, b.end_bits);
        ITEST_TRUE(a.bytes == b.bytes);
    }
}

ITEST(repeated_encoding_of_one_input_is_byte_identical) {
    const std::vector<u32> f = {12u, 0u, 3u, 3u, 50u, 1u, 0u, 9u};
    const std::vector<u32> symbols = {4u, 0u, 7u, 2u, 5u, 4u, 4u, 3u, 0u};
    const Stream first = encode_symbols(model_of(f), symbols, 21u);
    ITEST_TRUE(first.ok);
    for (u32 i = 0u; i < 1000u; ++i) {
        const Stream again = encode_symbols(model_of(f), symbols, 21u);
        ITEST_TRUE(again.bytes == first.bytes);
    }
}

// ---------------------------------------------------------------------------
// Capacity
// ---------------------------------------------------------------------------

ITEST(exhausted_writer_latches_failure_and_never_writes_past_capacity) {
    const StaticModel m = model_of({1u, 1u});
    u8 buf[3] = {0x00, 0x00, 0xEE};   // buf[2] is a guard outside capacity
    BitWriter bw(buf, 2u);
    ArithmeticEncoder enc(bw);
    bool all_ok = true;
    for (u32 i = 0u; i < 40u; ++i) all_ok = enc.encode(m, i & 1u) && all_ok;
    ITEST_TRUE(!all_ok);
    ITEST_TRUE(!enc.ok());
    ITEST_TRUE(!enc.encode(m, 0u));
    ITEST_TRUE(!enc.finish());
    ITEST_EQ(bw.bit_length(), 16u);
    ITEST_EQ(buf[2], 0xEEu);
}

ITEST(encoder_on_an_already_failed_writer_is_not_ok) {
    u8 buf[1] = {};
    BitWriter bw(buf, 1u);
    bw.write(0xFFu, 8);
    bw.write(1u, 1);   // overflow: the writer latches failure
    ArithmeticEncoder enc(bw);
    ITEST_TRUE(!enc.ok());
    ITEST_TRUE(!enc.finish());
}

// ---------------------------------------------------------------------------
// StaticModel
// ---------------------------------------------------------------------------

ITEST(static_model_rejects_invalid_tables) {
    StaticModel m;
    ITEST_TRUE(!m.valid());
    ITEST_EQ(m.total(), 0u);

    const u32 one[] = {1u};
    const u32 zeros[] = {0u, 0u, 0u};
    const u32 over[] = {kModelMaxTotal, 1u};
    const u32 wrap[] = {0xFFFFFFFFu, 0xFFFFFFFFu, 2u};   // would wrap a u32 sum to 0
    const u32 at_max[] = {kModelMaxTotal - 5u, 5u};

    ITEST_TRUE(!m.assign(nullptr, 1u));
    ITEST_TRUE(!m.assign(one, 0u));
    ITEST_TRUE(!m.assign(zeros, 3u));
    ITEST_TRUE(!m.assign(over, 2u));
    ITEST_TRUE(!m.assign(wrap, 3u));
    ITEST_TRUE(!m.valid());

    ITEST_TRUE(m.assign(at_max, 2u));
    ITEST_EQ(m.total(), kModelMaxTotal);

    ITEST_TRUE(!m.assign(over, 2u));   // a failed assign leaves the model empty
    ITEST_TRUE(!m.valid());
    ITEST_EQ(m.total(), 0u);
}

ITEST(static_model_intervals_tile_the_total_and_find_inverts_range_of) {
    const std::vector<u32> f = {0u, 3u, 0u, 0u, 1u, 5u, 0u};
    const StaticModel m = model_of(f);
    ITEST_EQ(m.alphabet_size(), 7u);
    ITEST_EQ(m.total(), 9u);

    u32 cursor = 0u;
    for (u32 s = 0u; s < 7u; ++s) {
        const SymbolRange r = m.range_of(s);
        ITEST_EQ(r.low, cursor);
        ITEST_EQ(r.high - r.low, f[s]);
        ITEST_EQ(r.total, 9u);
        ITEST_EQ(m.frequency(s), f[s]);
        cursor = r.high;
    }
    ITEST_EQ(cursor, m.total());

    const u32 expected[9] = {1u, 1u, 1u, 4u, 5u, 5u, 5u, 5u, 5u};
    for (u32 t = 0u; t < 9u; ++t) {
        SymbolRange r{0u, 0u, 0u};
        const u32 s = m.find(t, r);
        ITEST_EQ(s, expected[t]);
        ITEST_TRUE(r.low <= t && t < r.high);
        ITEST_EQ(r.low, m.range_of(s).low);
        ITEST_EQ(r.high, m.range_of(s).high);
    }

    const SymbolRange outside = m.range_of(7u);
    ITEST_EQ(outside.high - outside.low, 0u);   // outside the alphabet: p == 0
    ITEST_EQ(m.frequency(7u), 0u);
}

// ---------------------------------------------------------------------------
// p == 0 in an NDEBUG build: rejected, nothing written. (Debug builds assert
// instead — the --expect-assert cases.)
// ---------------------------------------------------------------------------

#ifdef NDEBUG

ITEST(release_zero_probability_symbol_is_rejected_and_writes_nothing) {
    const StaticModel m = model_of({1u, 0u, 1u});
    u8 buf[8] = {};
    BitWriter bw(buf, 8u);
    ArithmeticEncoder enc(bw);
    ITEST_TRUE(enc.encode(m, 0u));
    const u32 before = bw.bit_length();
    const u32 committed = enc.committed_bits();

    ITEST_TRUE(!enc.encode(m, 1u));        // zero frequency
    ITEST_TRUE(!enc.ok());
    ITEST_EQ(bw.bit_length(), before);
    ITEST_EQ(enc.committed_bits(), committed);
    ITEST_TRUE(!enc.encode(m, 2u));        // latched: even a live symbol is refused
    ITEST_TRUE(!enc.finish());             // no terminator: no payload at all
    ITEST_EQ(bw.bit_length(), before);
}

ITEST(release_out_of_alphabet_and_empty_model_are_rejected) {
    const StaticModel m = model_of({1u, 1u});
    const StaticModel empty;
    u8 buf[8] = {};

    BitWriter bw1(buf, 8u);
    ArithmeticEncoder e1(bw1);
    ITEST_TRUE(!e1.encode(m, 2u));
    ITEST_EQ(bw1.bit_length(), 0u);

    BitWriter bw2(buf, 8u);
    ArithmeticEncoder e2(bw2);
    ITEST_TRUE(!e2.encode(empty, 0u));
    ITEST_EQ(bw2.bit_length(), 0u);

    BitReader br(buf, 8u);
    ArithmeticDecoder d(br);
    u32 symbol = 99u;
    ITEST_TRUE(!d.decode(empty, symbol));
    ITEST_EQ(symbol, 0u);
    ITEST_TRUE(!d.ok());
}

ITEST(release_encode_after_finish_is_rejected) {
    const StaticModel m = model_of({1u, 1u});
    u8 buf[8] = {};
    BitWriter bw(buf, 8u);
    ArithmeticEncoder enc(bw);
    ITEST_TRUE(enc.finish());
    const u32 end = bw.bit_length();
    ITEST_TRUE(!enc.encode(m, 0u));
    ITEST_TRUE(!enc.finish());
    ITEST_EQ(bw.bit_length(), end);
}

ITEST(release_model_breaking_find_contract_is_rejected) {
    const BrokenFindModel broken;
    const u8 buf[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    BitReader br(buf, 8u);
    ArithmeticDecoder d(br);
    u32 symbol = 99u;
    ITEST_TRUE(!d.decode(broken, symbol));
    ITEST_TRUE(!d.ok());
}

#endif  // NDEBUG

// ---------------------------------------------------------------------------
// --expect-assert <case>
// ---------------------------------------------------------------------------

namespace {

int expect_assert(const char* which) {
#ifdef NDEBUG
    std::printf("ASSERTIONS-DISABLED: '%s' cannot abort in an NDEBUG build; "
                "the release_* cases cover this configuration\n", which);
    return 0;
#else
#ifdef _MSC_VER
    // Report to stderr and exit; never a modal dialog that would hang CTest.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    std::printf("expect-assert: %s\n", which);
    std::fflush(stdout);

    u8 buf[8] = {};
    BitWriter bw(buf, 8u);
    ArithmeticEncoder enc(bw);
    const StaticModel zero_middle = model_of({1u, 0u, 1u});
    const StaticModel binary      = model_of({1u, 1u});
    const StaticModel empty;

    if (std::strcmp(which, "encode-zero-frequency") == 0) {
        enc.encode(zero_middle, 1u);
    } else if (std::strcmp(which, "encode-out-of-alphabet") == 0) {
        enc.encode(binary, 2u);
    } else if (std::strcmp(which, "encode-empty-model") == 0) {
        enc.encode(empty, 0u);
    } else if (std::strcmp(which, "encode-after-finish") == 0) {
        enc.finish();
        enc.encode(binary, 0u);
    } else if (std::strcmp(which, "decode-empty-model") == 0) {
        BitReader br(buf, 8u);
        ArithmeticDecoder dec(br);
        u32 symbol = 0u;
        dec.decode(empty, symbol);
    } else if (std::strcmp(which, "decode-broken-model") == 0) {
        BitReader br(buf, 8u);
        ArithmeticDecoder dec(br);
        const BrokenFindModel broken;
        u32 symbol = 0u;
        dec.decode(broken, symbol);
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
    return ::itest::run_all("unit.coder");
}
