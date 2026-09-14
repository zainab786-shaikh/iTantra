// Fuzz tests — coder/. Implementation plan Phase 2.
//
//   fuzz   random symbol sequences, random models, no crash, exact round-trip
//
// Deterministic: every input derives from the seed, so any failure reproduces
// exactly. CTest runs the defaults. For a longer run:
//
//   coder_fuzz_test --iterations 200000 --seed 12345
//
// Models cover alphabets of 1 to ~66k symbols, totals up to kModelMaxTotal,
// zero-frequency holes, p = 2^-24 symbols and near-certain ones; streams mix
// several models, start at arbitrary bit offsets, and are followed by garbage.

#include "coder/coder.h"
#include "coder/static_model.h"
#include "itest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace itantra;

namespace {

struct Config {
    u32 iterations = 2000u;
    u32 seed       = 0x9E3779B9u;
};

Config& config() {
    static Config c;
    return c;
}

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

XorShift32 rng_for(u32 salt) {
    u32 s = config().seed ^ (salt * 0x85EBCA6Bu);
    return XorShift32{s == 0u ? 1u : s};   // 0 is a fixed point of xorshift
}

struct FuzzModel {
    std::vector<u32> freq;
    std::vector<u32> live;
    StaticModel      model;
};

void build_random_model(FuzzModel& fm, XorShift32& rng) {
    const u32 size_class = rng.below(100u);
    u32 n = 0u;
    if (size_class < 30u) {
        n = 1u + rng.below(16u);
    } else if (size_class < 92u) {
        n = 17u + rng.below(496u);
    } else {
        n = 513u + rng.below(65536u);
    }
    const u32 cap = kModelMaxTotal / n;   // n * cap <= kModelMaxTotal

    fm.freq.assign(n, 0u);
    switch (rng.below(6u)) {
        case 0:   // uniform draws, zeros included
            for (u32& f : fm.freq) f = rng.below(cap + 1u);
            break;
        case 1:   // near-flat, small total
            for (u32& f : fm.freq) f = 1u + rng.below(8u);
            break;
        case 2:   // heavily skewed
            for (u32& f : fm.freq) f = cap >> rng.below(24u);
            break;
        case 3:   // sparse
            for (u32& f : fm.freq) f = (rng.below(4u) == 0u) ? 1u + rng.below(cap) : 0u;
            break;
        case 4:   // everything flat at the maximum total
            for (u32& f : fm.freq) f = cap;
            break;
        default:  // one near-certain symbol, the rest at p = 2^-24
            for (u32& f : fm.freq) f = 1u;
            fm.freq[rng.below(n)] = kModelMaxTotal - (n - 1u);
            break;
    }

    bool any = false;
    for (u32 f : fm.freq) any = any || f != 0u;
    if (!any) fm.freq[rng.below(n)] = 1u;

    fm.live.clear();
    for (u32 i = 0u; i < n; ++i) {
        if (fm.freq[i] != 0u) fm.live.push_back(i);
    }
    const bool ok = fm.model.assign(fm.freq.data(), n);
    ITEST_TRUE(ok);
}

// Half uniform over the live symbols, half weighted by probability — the
// latter produces the compressible streams real payloads are.
u32 pick_symbol(const FuzzModel& fm, XorShift32& rng) {
    if (rng.below(2u) == 0u) return fm.live[rng.below(static_cast<u32>(fm.live.size()))];
    SymbolRange r{0u, 0u, 0u};
    return fm.model.find(rng.below(fm.model.total()), r);
}

u32 capacity_bytes(u32 prefix_bits, u32 symbols) {
    return (prefix_bits + 32u * symbols + kCoderFlushBits + 7u) / 8u;
}

struct Encoding {
    u32  committed = 0u;
    u32  end_bits  = 0u;
    bool ok        = false;
};

Encoding encode_into(u8* buf, u32 capacity, u32 prefix_bits, u32 prefix_value,
                     const FuzzModel* const* models, const std::vector<u32>& which,
                     const std::vector<u32>& symbols) {
    Encoding e;
    BitWriter bw(buf, capacity);
    for (u32 i = 0u; i < prefix_bits; ++i) bw.write((prefix_value >> (i % 32u)) & 1u, 1);
    ArithmeticEncoder enc(bw);
    bool ok = true;
    for (std::size_t i = 0u; i < symbols.size(); ++i) {
        ok = enc.encode(models[which[i]]->model, symbols[i]) && ok;
    }
    e.committed = enc.committed_bits();
    ok = enc.finish() && ok;
    e.end_bits = bw.bit_length();
    e.ok = ok && enc.ok() && bw.ok();
    return e;
}

}  // namespace

ITEST(fuzz_random_models_random_sequences_exact_round_trip) {
    XorShift32 rng = rng_for(1u);
    for (u32 it = 0u; it < config().iterations; ++it) {
        FuzzModel pool[3];
        const u32 model_count = 1u + rng.below(3u);
        for (u32 k = 0u; k < model_count; ++k) build_random_model(pool[k], rng);
        const FuzzModel* const models[3] = {&pool[0], &pool[1], &pool[2]};

        // Mostly clause-sized; sometimes long.
        const u32 length = rng.below(4u) != 0u ? rng.below(24u) : rng.below(400u);
        std::vector<u32> which(length);
        std::vector<u32> symbols(length);
        for (u32 i = 0u; i < length; ++i) {
            which[i]   = rng.below(model_count);
            symbols[i] = pick_symbol(*models[which[i]], rng);
        }

        const u32 prefix_bits  = rng.below(41u);
        const u32 prefix_value = rng.next();
        const u32 capacity     = capacity_bytes(prefix_bits, length);

        // Encode into a dirty buffer.
        std::vector<u8> buf(capacity + 64u);
        for (u8& b : buf) b = static_cast<u8>(rng.next());
        const Encoding e = encode_into(buf.data(), capacity, prefix_bits, prefix_value, models,
                                       which, symbols);
        ITEST_TRUE(e.ok);
        ITEST_EQ(e.end_bits, prefix_bits + e.committed + kCoderFlushBits);
        const u32 used = (e.end_bits + 7u) / 8u;

        // Determinism: freshly built copies of the models, a clean buffer,
        // identical bytes.
        FuzzModel copies[3];
        for (u32 k = 0u; k < model_count; ++k) {
            copies[k].freq = pool[k].freq;
            copies[k].live = pool[k].live;
            ITEST_TRUE(copies[k].model.assign(copies[k].freq.data(),
                                              static_cast<u32>(copies[k].freq.size())));
        }
        const FuzzModel* const copy_ptrs[3] = {&copies[0], &copies[1], &copies[2]};
        std::vector<u8> clean(capacity, 0u);
        const Encoding again = encode_into(clean.data(), capacity, prefix_bits, prefix_value,
                                           copy_ptrs, which, symbols);
        ITEST_TRUE(again.ok);
        ITEST_EQ(again.end_bits, e.end_bits);
        ITEST_TRUE(std::memcmp(clean.data(), buf.data(), used) == 0);

        // Garbage after the flush: in the pad bits of the last byte and in
        // every byte beyond. The decode must not notice.
        const u32 tail_bits = used * 8u - e.end_bits;
        buf[used - 1u] = static_cast<u8>(buf[used - 1u] | (rng.next() & ((1u << tail_bits) - 1u)));

        BitReader br(buf.data(), used + 64u);
        for (u32 i = 0u; i < prefix_bits; ++i) {
            ITEST_EQ(br.read(1), (prefix_value >> (i % 32u)) & 1u);
        }
        ArithmeticDecoder dec(br);
        for (u32 i = 0u; i < length; ++i) {
            u32 got = 0u;
            ITEST_TRUE(dec.decode(models[which[i]]->model, got));
            ITEST_EQ(got, symbols[i]);
        }
        ITEST_TRUE(dec.ok());
    }
}

ITEST(fuzz_arbitrary_bits_decode_to_live_symbols_without_crashing) {
    // A decoder handed bytes no encoder produced must still terminate, stay
    // inside the alphabet, and never yield a p == 0 symbol.
    XorShift32 rng = rng_for(2u);
    for (u32 it = 0u; it < config().iterations; ++it) {
        FuzzModel fm;
        build_random_model(fm, rng);

        const u32 length = rng.below(65u);
        std::vector<u8> bytes(length);
        for (u8& b : bytes) b = static_cast<u8>(rng.next());

        BitReader br(length == 0u ? nullptr : bytes.data(), length);
        ArithmeticDecoder dec(br);
        const u32 count = rng.below(500u);
        for (u32 i = 0u; i < count; ++i) {
            u32 s = 0u;
            ITEST_TRUE(dec.decode(fm.model, s));
            ITEST_TRUE(s < fm.model.alphabet_size());
            ITEST_TRUE(fm.model.frequency(s) != 0u);
        }
        ITEST_TRUE(dec.ok());
    }
}

ITEST(fuzz_small_buffers_fail_cleanly_or_round_trip) {
    XorShift32 rng = rng_for(3u);
    const u8 kGuard = 0xA5u;
    for (u32 it = 0u; it < config().iterations; ++it) {
        FuzzModel pool[1];
        build_random_model(pool[0], rng);
        const FuzzModel* const models[1] = {&pool[0]};

        const u32 length = rng.below(40u);
        std::vector<u32> which(length, 0u);
        std::vector<u32> symbols(length);
        for (u32& s : symbols) s = pick_symbol(pool[0], rng);

        const u32 capacity = rng.below(17u);
        std::vector<u8> buf(capacity + 8u, kGuard);
        const Encoding e = encode_into(buf.data(), capacity, 0u, 0u, models, which, symbols);

        for (u32 i = capacity; i < capacity + 8u; ++i) ITEST_EQ(buf[i], kGuard);
        ITEST_TRUE(e.end_bits <= capacity * 8u);

        if (e.ok) {
            BitReader br(buf.data(), capacity);
            ArithmeticDecoder dec(br);
            for (u32 i = 0u; i < length; ++i) {
                u32 got = 0u;
                ITEST_TRUE(dec.decode(pool[0].model, got));
                ITEST_EQ(got, symbols[i]);
            }
        }
    }
}

ITEST(fuzz_extreme_probabilities_and_follow_runs) {
    // Streams built from the symbols that stress the coder hardest: p = 2^-24
    // (maximum bits per symbol), near-certain (almost no bits), and the
    // middle of {1,2,1} (pure underflow, long follow runs).
    XorShift32 rng = rng_for(4u);
    FuzzModel pool[3];
    pool[0].freq = {1u, kModelMaxTotal - 2u, 1u};
    pool[1].freq = {1u, 2u, 1u};
    pool[2].freq = {kModelMaxTotal / 2u, 1u, kModelMaxTotal / 2u - 1u};
    for (FuzzModel& fm : pool) {
        fm.live.clear();
        for (u32 i = 0u; i < static_cast<u32>(fm.freq.size()); ++i) {
            if (fm.freq[i] != 0u) fm.live.push_back(i);
        }
        ITEST_TRUE(fm.model.assign(fm.freq.data(), static_cast<u32>(fm.freq.size())));
    }
    const FuzzModel* const models[3] = {&pool[0], &pool[1], &pool[2]};

    const u32 rounds = config().iterations / 4u + 1u;
    for (u32 it = 0u; it < rounds; ++it) {
        const u32 length = rng.below(2000u);
        std::vector<u32> which(length);
        std::vector<u32> symbols(length);
        u32 run_model = 0u;
        u32 run_symbol = 0u;
        for (u32 i = 0u; i < length; ++i) {
            if (rng.below(16u) == 0u || i == 0u) {   // runs, so follow counts grow
                run_model  = rng.below(3u);
                run_symbol = pool[run_model].live[rng.below(
                    static_cast<u32>(pool[run_model].live.size()))];
            }
            which[i]   = run_model;
            symbols[i] = run_symbol;
        }

        const u32 prefix_bits = rng.below(32u);
        const u32 capacity    = capacity_bytes(prefix_bits, length);
        std::vector<u8> buf(capacity + 8u, 0xFFu);
        const Encoding e = encode_into(buf.data(), capacity, prefix_bits, 0u, models, which,
                                       symbols);
        ITEST_TRUE(e.ok);
        ITEST_EQ(e.end_bits, prefix_bits + e.committed + kCoderFlushBits);

        BitReader br(buf.data(), capacity + 8u);   // 0xFF garbage after the flush
        for (u32 i = 0u; i < prefix_bits; ++i) ITEST_EQ(br.read(1), 0u);
        ArithmeticDecoder dec(br);
        for (u32 i = 0u; i < length; ++i) {
            u32 got = 0u;
            ITEST_TRUE(dec.decode(models[which[i]]->model, got));
            ITEST_EQ(got, symbols[i]);
        }
    }
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; a += 2) {
        const unsigned long v = std::strtoul(argv[a + 1], nullptr, 0);
        if (std::strcmp(argv[a], "--iterations") == 0) {
            config().iterations = static_cast<u32>(v);
        } else if (std::strcmp(argv[a], "--seed") == 0) {
            config().seed = static_cast<u32>(v);
        } else {
            std::printf("usage: coder_fuzz_test [--iterations N] [--seed S]\n");
            return 2;
        }
    }
    std::printf("unit.coder.fuzz: iterations=%u seed=0x%08X\n", config().iterations, config().seed);
    return ::itest::run_all("unit.coder.fuzz");
}
