// Conformance — C-01 … C-03 host side for the Tier 1 / Tier 2 golden vectors
// (native/test/golden/tier_vectors.bin). Implementation plan Phase 8.
//
//   C-01 [D]  encode every golden vector on >= 3 SoC vendors, AEAD bypassed →
//             Phase 11 (contract §7.1: host results do not substitute). Host
//             precondition here: each vector's symbols re-derive from its input,
//             and the payload re-encodes byte for byte, using only the tables
//             embedded in the file.
//   C-02 [D]  decode every golden vector on >= 3 devices → Phase 11. Host
//             precondition: every payload decodes to its symbols and its input
//             (text, or frame).
//   C-03 [H]  encode the same input 1000 times in one process → byte-identical.
//
//   tier_vectors_test --vectors <path to tier_vectors.bin>
//
// The file is FROZEN. A failure here is an encoder regression; the fix is in the
// encoder, never in the file (contract §2.2, §7.2).

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "golden/tier_golden_format.h"
#include "packet/parse.h"
#include "tier1/decode.h"
#include "tier2/decode.h"
#include "itest.h"

using namespace itantra;

namespace {

struct Loaded {
    std::string                path;
    bool                       ok = false;
    std::string                error;
    tiergolden::TierGoldenFile file;
    tiergolden::LoadedTables   tables;
};

Loaded& loaded() {
    static Loaded l;
    return l;
}

bool require_loaded() {
    if (!loaded().ok) std::printf("  tier vectors not loaded from %s: %s\n", loaded().path.c_str(), loaded().error.c_str());
    return loaded().ok;
}

}  // namespace

ITEST(the_artifact_loads_and_matches_this_build) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    const tiergolden::TierGoldenFile& f = loaded().file;
    ITEST_EQ(f.file_version, tiergolden::kFileVersion);
    ITEST_EQ(f.packet_format_version, kPacketFormatVersion);
    ITEST_EQ(f.coder_version, kCoderVersion);
    ITEST_EQ(f.tokenizer_version, kTokenizerVersion);
    ITEST_EQ(f.ngram_version, kNgramTableVersion);
    ITEST_EQ(f.boost_version, kBoostTableVersion);
    ITEST_EQ(f.tier1_version, kTier1TableVersion);
    ITEST_EQ(f.tables.size(), 6u);

    u32 tier1 = 0u, tier2 = 0u, boosted = 0u, hashed = 0u, escaped = 0u, critical = 0u, negated = 0u, literal = 0u;
    u32 max_count = 0u;
    for (const tiergolden::TierVector& v : f.vectors) {
        (v.metadata.tier == Tier::Tier1 ? tier1 : tier2) += 1u;
        boosted += v.boosted ? 1u : 0u;
        hashed += v.metadata.hash_present ? 1u : 0u;
        escaped += v.metadata.symbol_count >= 31u ? 1u : 0u;
        critical += v.metadata.priority == Priority::Critical ? 1u : 0u;
        negated += v.metadata.negation ? 1u : 0u;
        for (const FrameSlot& fs : v.frame.slots) literal += fs.mode == SlotMode::Literal ? 1u : 0u;
        if (v.metadata.symbol_count > max_count) max_count = v.metadata.symbol_count;
    }
    ITEST_TRUE(tier1 >= 20u && tier2 >= 20u && boosted >= 3u && hashed >= 8u && escaped >= 4u && critical >= 8u &&
               negated >= 4u && literal >= 8u);
    ITEST_EQ(max_count, kMaxSymbolCount);
    std::printf("  %zu vectors: Tier 1 %u, Tier 2 %u; boosted %u, hash %u, escape form %u, CRITICAL %u, negated %u, "
                "literal slots %u, largest symbol_count %u\n",
                f.vectors.size(), tier1, tier2, boosted, hashed, escaped, critical, negated, literal, max_count);
}

ITEST(c01_host_every_vector_re_encodes_byte_identically) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    u32 identical = 0u;
    for (const tiergolden::TierVector& v : loaded().file.vectors) {
        std::vector<Symbol> symbols;
        std::vector<u8> payload;
        u16 bits = 0u;
        bool ok = tiergolden::derive_symbols(loaded().tables, v, symbols) && symbols == v.symbols &&
                  tiergolden::encode_symbols(loaded().tables, v, symbols, payload, bits) && payload == v.payload &&
                  bits == v.metadata_bits;
        if (v.metadata.tier == Tier::Tier2 && v.boosted) {
            ok = ok && v.metadata.hash_present &&
                 v.metadata.context_hash == wire_context_hash(context_hash(tiergolden::context_of(v.context)));
        }
        if (v.metadata.tier == Tier::Tier1) ok = ok && v.metadata.hash_present == frame_uses_context(v.frame);
        ITEST_TRUE(ok);
        if (!ok) std::printf("  ENCODER REGRESSION: %s\n", v.name.c_str());
        if (ok) ++identical;
    }
    std::printf("  C-01 host precondition: %u of %zu vectors re-encode byte-identically\n", identical,
                loaded().file.vectors.size());
}

ITEST(c02_host_every_vector_decodes_to_its_input) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    u32 decoded = 0u;
    for (const tiergolden::TierVector& v : loaded().file.vectors) {
        bool ok = false;
        if (v.metadata.tier == Tier::Tier2) {
            const Context ctx = tiergolden::context_of(v.context);
            Tier2Decoded d;
            ok = tier2_decode(loaded().tables.tier2, v.payload.data(), static_cast<u32>(v.payload.size()),
                              v.boosted ? &ctx : nullptr, d) == Tier2Status::Ok &&
                 d.text == v.text && d.metadata == v.metadata;
        } else {
            Tier1Decoded d;
            ok = tier1_decode(loaded().tables.common, loaded().tables.tier2, v.payload.data(),
                              static_cast<u32>(v.payload.size()), d) == Tier1DecodeStatus::Ok &&
                 d.frame == v.frame && d.metadata == v.metadata;
        }
        ITEST_TRUE(ok);
        if (!ok) std::printf("  DECODER REGRESSION: %s\n", v.name.c_str());
        if (ok) ++decoded;
    }
    std::printf("  C-02 host precondition: %u of %zu vectors decode to their input\n", decoded, loaded().file.vectors.size());
}

ITEST(c03_the_same_input_encodes_identically_1000_times) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
#ifdef NDEBUG
    const u32 large_iterations = 1000u;
#else
    const u32 large_iterations = 20u;   // Debug: vectors above 400 symbols only; Release runs all 1000
#endif
    u64 encodes = 0u;
    u32 failures = 0u;
    for (const tiergolden::TierVector& v : loaded().file.vectors) {
        const u32 iterations = v.symbols.size() > 400u ? large_iterations : 1000u;
        for (u32 i = 0u; i < iterations; ++i) {
            std::vector<Symbol> symbols;
            std::vector<u8> payload;
            u16 bits = 0u;
            if (!tiergolden::derive_symbols(loaded().tables, v, symbols) ||
                !tiergolden::encode_symbols(loaded().tables, v, symbols, payload, bits) || payload != v.payload) {
                ++failures;
            }
            ++encodes;
        }
    }
    ITEST_EQ(failures, 0u);
    std::printf("  C-03: %llu encodes, 0 differences (large-vector iterations %u)\n", static_cast<unsigned long long>(encodes),
                large_iterations);
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; ++a) {
        if (std::strcmp(argv[a], "--vectors") == 0) loaded().path = argv[a + 1];
    }
    std::vector<u8> bytes;
    if (!loaded().path.empty() && golden::read_file(loaded().path.c_str(), bytes)) {
        loaded().ok = tiergolden::deserialize(bytes, loaded().file, loaded().error) &&
                      loaded().tables.load(loaded().file, loaded().error);
    } else {
        loaded().error = "cannot read the file";
    }
    return ::itest::run_all("conformance.tier_vectors");
}
