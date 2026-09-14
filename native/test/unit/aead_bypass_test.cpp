// Unit test — ITANTRA_DISABLE_AEAD bypasses cleanly in debug.
// Implementation plan Phase 4; packet §6.10.2.
//
// Built against a separate copy of the core compiled with the flag, in the
// Debug configuration only (native/CMakeLists.txt). "Cleanly" means: the
// sealed output is exactly the plaintext payload — so on a device it can be
// compared byte for byte with the frozen golden vectors (C-01, AEAD bypassed)
// — and the same entry points decode it. In any configuration without the
// flag this binary reports itself skipped.
//
//   aead_bypass_test --vectors <vectors.bin>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "golden/golden_format.h"
#include "itest.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace itantra;

namespace {

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

SessionKeys test_keys() {
    u8 psk[32], initiator[32], responder[32];
    for (u32 i = 0u; i < 32u; ++i) {
        psk[i]       = static_cast<u8>(0xA0u + i);
        initiator[i] = static_cast<u8>(0x10u + i);
        responder[i] = static_cast<u8>(0x55u ^ i);
    }
    SessionKeys k{};
    derive_session_keys(psk, initiator, responder, k);
    return k;
}

SeqCounter counter_for(std::size_t index, u8 seq) {
    return (static_cast<SeqCounter>(index) + 1u) * 256u + seq;
}

}  // namespace

ITEST(bypass_exists_only_in_a_debug_build) {
#ifdef NDEBUG
    ITEST_TRUE(false);   // unreachable: crypto/aead.h #errors first
#endif
    ITEST_TRUE(kAeadBypassed);
    ITEST_EQ(kAeadOverheadBytes, 0u);
    ITEST_EQ(kAeadTagBytes, 4u);   // the packet-size constant is unchanged
}

ITEST(bypass_seal_and_open_pass_the_plaintext_through_unchanged) {
    u8 key[32] = {};
    u8 nonce[12] = {};
    for (u32 length = 0u; length <= 64u; ++length) {
        std::vector<u8> plain(length);
        for (u32 i = 0u; i < length; ++i) plain[i] = static_cast<u8>(i * 37u + 1u);
        std::vector<u8> sealed(length + 8u, 0xEE);
        u32 n = 0u;
        ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, plain.data(), length, sealed.data(),
                             static_cast<u32>(sealed.size()), n) == AeadStatus::Ok);
        ITEST_EQ(n, length);
        ITEST_TRUE(std::memcmp(sealed.data(), plain.data(), length) == 0);

        std::vector<u8> back(length + 8u, 0xEE);
        u32 m = 0u;
        ITEST_TRUE(aead_open(key, nonce, nullptr, 0u, sealed.data(), n, back.data(),
                             static_cast<u32>(back.size()), m) == AeadStatus::Ok);
        ITEST_EQ(m, length);
        ITEST_TRUE(std::memcmp(back.data(), plain.data(), length) == 0);
    }
}

ITEST(bypass_sealed_assembly_is_byte_identical_to_every_frozen_golden_vector) {
    ITEST_TRUE(loaded().ok);
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    auto sealed = std::make_unique<SealedPayload>();
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const AsmResult r = assemble_sealed(golden::input_of(v, model), keys, Direction::InitiatorToResponder,
                                            counter_for(i, v.metadata.seq), *sealed);
        const bool same = r == AsmResult::Ok && sealed->len == v.payload.size() &&
                          std::memcmp(sealed->bytes, v.payload.data(), sealed->len) == 0;
        ITEST_TRUE(same);
        if (!same) std::printf("  bypass %s differs from the frozen vector\n", v.name.c_str());
    }
}

ITEST(bypass_open_and_parse_decodes_every_frozen_golden_vector) {
    ITEST_TRUE(loaded().ok);
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    auto parsed = std::make_unique<ParsedPayload>();
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const golden::FixedSelector selector(&model);
        const ParseStatus s = open_and_parse(v.payload.data(), static_cast<u32>(v.payload.size()), keys,
                                             Direction::InitiatorToResponder, counter_for(i, v.metadata.seq),
                                             selector, *parsed);
        bool same = s == ParseStatus::Ok && parsed->metadata == v.metadata;
        for (std::size_t k = 0u; same && k < v.symbols.size(); ++k) same = parsed->symbols[k] == v.symbols[k];
        ITEST_TRUE(same);
    }
}

int main(int argc, char** argv) {
    if (!kAeadBypassed) {
        std::printf("AEAD-BYPASS-NOT-BUILT: this configuration compiles AEAD in; the bypass exists in Debug only\n");
        return 0;
    }
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--vectors") == 0) loaded().path = argv[a + 1];
    }
    std::vector<u8> bytes;
    if (loaded().path != nullptr && golden::read_file(loaded().path, bytes)) {
        loaded().ok = golden::deserialize(bytes, loaded().file, loaded().error);
    }
    if (!loaded().ok) std::printf("golden vectors not loaded: %s\n", loaded().error.c_str());
    return ::itest::run_all("unit.aead-bypass");
}
