// Conformance — C-01 … C-04, host side. Implementation plan Phase 3.
//
//   C-01 [D]  encode every golden vector on >= 3 SoC vendors, AEAD bypassed
//             → DEFERRED to Phase 11. Contract §7.1: host results do not
//             substitute for device results. The host encode check below is
//             its precondition, and this binary is the one to run on devices.
//   C-02 [D]  decode every golden vector on >= 3 devices → DEFERRED to Phase 11;
//             host decode check below.
//   C-03 [H]  encode the same input 1000 times in one process → byte-identical.
//   C-04 [H]  no float / double → the build step (tools/c04_no_float.cpp),
//             CTest "C-04". Not repeated here.
//
//   c01_c04_test --vectors <path to vectors.bin>
//
// The vectors are FROZEN. A failure here is an encoder regression; the fix is
// in the encoder, never in the file (contract §2.2, §7.2).

#include "golden/golden_format.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace itantra;

namespace {

constexpr u32 kC03Iterations = 1000u;

struct Loaded {
    const char*        path = nullptr;
    bool               ok   = false;
    std::string        error;
    golden::GoldenFile file;
};

Loaded& loaded() {
    static Loaded l;
    return l;
}

bool require_loaded() {
    if (!loaded().ok) {
        std::printf("  golden vectors not loaded from %s: %s\n",
                    loaded().path != nullptr ? loaded().path : "(no --vectors)", loaded().error.c_str());
    }
    return loaded().ok;
}

void report_first_difference(const char* what, const std::string& name, const u8* got, u32 got_len,
                             const std::vector<u8>& want) {
    u32 i = 0u;
    while (i < got_len && i < want.size() && got[i] == want[i]) ++i;
    std::printf("  %s %s: length %u vs frozen %zu, first difference at byte %u\n", what, name.c_str(),
                got_len, want.size(), i);
}

}  // namespace

ITEST(golden_file_loads_with_the_current_format_and_coder_versions) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    ITEST_EQ(f.file_version, golden::kFileVersion);
    ITEST_EQ(f.packet_format_version, kPacketFormatVersion);
    ITEST_EQ(f.coder_version, kCoderVersion);
    ITEST_TRUE(!f.vectors.empty());
}

ITEST(golden_vector_set_covers_contract_section_2_2) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;

    bool min_t1 = false, min_t2 = false, max_t1 = false, max_t2 = false;
    bool count30 = false, count31 = false, count32 = false, empty = false;
    bool t1_variant[2][2][2] = {};
    bool t2_variant[2][2] = {};
    bool seq0 = false, seq255 = false, hash000 = false, hashfff = false, lang0 = false, lang15 = false;
    u32 literals = 0u, adversarial = 0u;

    for (const golden::GoldenVector& v : loaded().file.vectors) {
        const Metadata& m = v.metadata;
        const u32 n = m.symbol_count;
        const u32 hp = m.hash_present ? 1u : 0u;
        const u32 pri = static_cast<u32>(m.priority);
        const bool t1 = m.tier == Tier::Tier1;

        if (n == 1u && hp == 0u) (t1 ? min_t1 : min_t2) = true;
        if (n == kMaxSymbolCount && hp == 1u) (t1 ? max_t1 : max_t2) = true;
        if (n == 30u) count30 = true;
        if (n == 31u) count31 = true;
        if (n == 32u) count32 = true;
        if (n == 0u) empty = true;
        if (t1) t1_variant[hp][pri][m.negation ? 1u : 0u] = true; else t2_variant[hp][pri] = true;
        if (m.seq == 0u) seq0 = true;
        if (m.seq == 255u) seq255 = true;
        if (hp == 1u && m.context_hash == 0x000u) hash000 = true;
        if (hp == 1u && m.context_hash == 0xFFFu) hashfff = true;
        if (!t1 && m.language == 0u) lang0 = true;
        if (!t1 && m.language == 15u) lang15 = true;
        if (v.name.rfind("literal-", 0u) == 0u) ++literals;
        if (v.name.rfind("adversarial-", 0u) == 0u) ++adversarial;
    }

    ITEST_TRUE(min_t1 && min_t2);                  // minimum: 1 symbol, no hash, both tiers
    ITEST_TRUE(max_t1 && max_t2);                  // maximum symbol_count, hash present
    ITEST_TRUE(count30 && count31 && count32);     // either side of the escape boundary
    for (u32 hp = 0u; hp < 2u; ++hp) {             // tier × hash_present × priority × negation
        for (u32 pri = 0u; pri < 2u; ++pri) {
            ITEST_TRUE(t1_variant[hp][pri][0] && t1_variant[hp][pri][1]);
            ITEST_TRUE(t2_variant[hp][pri]);
        }
    }
    ITEST_TRUE(empty);                             // adversarial: empty input
    ITEST_TRUE(seq0 && seq255 && hash000 && hashfff && lang0 && lang15);
    ITEST_TRUE(literals >= 10u);                   // every script, byte fallback
    ITEST_TRUE(adversarial >= 5u);                 // random bytes, mixed, extremes
}

ITEST(host_encode_reproduces_every_frozen_payload_byte_for_byte) {
    // C-01 precondition on the host. C-01 itself is [D], Phase 11.
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    auto payload = std::make_unique<NativePayload>();
    for (const golden::GoldenVector& v : f.vectors) {
        const golden::ScheduledModel model(f.models, v);
        const bool assembled = assemble(golden::input_of(v, model), *payload) == AsmResult::Ok;
        ITEST_TRUE(assembled);
        const bool same = assembled && payload->len == v.payload.size() &&
                          payload->metadata_bits == v.metadata_bits &&
                          std::memcmp(payload->bytes, v.payload.data(), payload->len) == 0;
        ITEST_TRUE(same);
        if (!same) report_first_difference("encode", v.name, payload->bytes, payload->len, v.payload);
    }
}

ITEST(host_decode_reproduces_every_frozen_input) {
    // C-02 precondition on the host. C-02 itself is [D], Phase 11.
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    auto parsed = std::make_unique<ParsedPayload>();
    for (const golden::GoldenVector& v : f.vectors) {
        const golden::ScheduledModel model(f.models, v);
        const golden::FixedSelector selector(&model);
        const ParseStatus status =
            parse(v.payload.data(), static_cast<u32>(v.payload.size()), selector, *parsed);
        bool same = status == ParseStatus::Ok && parsed->metadata == v.metadata &&
                    parsed->metadata_bits == v.metadata_bits;
        for (std::size_t i = 0u; same && i < v.symbols.size(); ++i) same = parsed->symbols[i] == v.symbols[i];
        ITEST_TRUE(same);
        if (!same) std::printf("  decode %s: status %u\n", v.name.c_str(), static_cast<u32>(status));
    }
}

ITEST(C03_same_input_encoded_1000_times_is_byte_identical) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    auto payload = std::make_unique<NativePayload>();
    u32 encodes = 0u;
    for (const golden::GoldenVector& v : f.vectors) {
        const golden::ScheduledModel model(f.models, v);
        const AssemblyInput in = golden::input_of(v, model);
        u32 mismatches = 0u;
        for (u32 i = 0u; i < kC03Iterations; ++i) {
            const bool same = assemble(in, *payload) == AsmResult::Ok && payload->len == v.payload.size() &&
                              std::memcmp(payload->bytes, v.payload.data(), payload->len) == 0;
            if (!same) ++mismatches;
            ++encodes;
        }
        ITEST_EQ(mismatches, 0u);
        if (mismatches != 0u) std::printf("  C-03 %s: %u of %u encodes differed\n", v.name.c_str(), mismatches, kC03Iterations);
    }
    std::printf("  C-03: %u vectors x %u = %u encodes\n", static_cast<u32>(f.vectors.size()), kC03Iterations, encodes);
}

ITEST(frozen_payloads_decode_identically_whatever_follows_the_flush) {
    // packet §4.1 padding is never read; §3.5 symbol_count is the only stop.
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    auto parsed = std::make_unique<ParsedPayload>();
    u32 state = 0x31415927u;
    for (const golden::GoldenVector& v : f.vectors) {
        const golden::ScheduledModel model(f.models, v);
        const golden::FixedSelector selector(&model);
        const u32 end_bits = golden::coded_bit_length(golden::input_of(v, model));
        const u32 pad = static_cast<u32>(v.payload.size()) * 8u - end_bits;
        ITEST_TRUE(pad < 8u);
        ITEST_EQ(v.payload.back() & ((1u << pad) - 1u), 0u);   // frozen padding is zeros

        std::vector<u8> dirty = v.payload;
        dirty.back() = static_cast<u8>(dirty.back() | ((1u << pad) - 1u));
        for (u32 i = 0u; i < 16u; ++i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            dirty.push_back(static_cast<u8>(state));
        }
        bool same = parse(dirty.data(), static_cast<u32>(dirty.size()), selector, *parsed) == ParseStatus::Ok &&
                    parsed->metadata == v.metadata;
        for (std::size_t i = 0u; same && i < v.symbols.size(); ++i) same = parsed->symbols[i] == v.symbols[i];
        ITEST_TRUE(same);
    }
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--vectors") == 0) loaded().path = argv[a + 1];
    }
    if (loaded().path == nullptr) {
        std::printf("usage: c01_c04_test --vectors <vectors.bin>\n");
        return 2;
    }
    std::vector<u8> bytes;
    if (!golden::read_file(loaded().path, bytes)) {
        loaded().error = "cannot read file";
    } else {
        loaded().ok = golden::deserialize(bytes, loaded().file, loaded().error);
    }
    if (loaded().ok) {
        std::printf("golden vectors: %s — %zu vectors, %zu tables, file crc32 0x%08X\n", loaded().path,
                    loaded().file.vectors.size(), loaded().file.models.size(),
                    golden::crc32(bytes.data(), bytes.size() - 4u));
    }
    std::printf("C-01 [D] SKIPPED on host: requires >= 3 devices with different SoC vendors (Phase 11)\n");
    std::printf("C-02 [D] SKIPPED on host: requires >= 3 devices (Phase 11)\n");
    std::printf("C-04 [H] runs as the build step and CTest 'C-04'\n");
    return ::itest::run_all("conformance.c01_c04");
}
