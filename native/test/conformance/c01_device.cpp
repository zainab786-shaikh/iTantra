// Conformance — C-01 / C-02 on a device. Implementation plan Phase 11.
//
//   C-01 [D]  encode every golden vector on >= 3 devices with different SoC
//             vendors, AEAD bypassed → byte-identical output on all devices
//   C-02 [D]  decode every golden vector on >= 3 devices, AEAD bypassed →
//             identical decoded symbols on all devices
//
// One run is one device. native/test/device/CMakeLists.txt builds this file
// with the NDK from the SAME source list as libitantra-native.so
// (android-native/app/src/main/cpp/sources.cmake); it is pushed to each device
// with both frozen artifacts and run there. The host build runs it as well
// (CTest conformance.c01_device) as the reference run.
//
// A device passes when every vector of BOTH frozen artifacts re-encodes to the
// frozen bytes and decodes to the frozen input. The reference is the frozen
// file itself, so every device passing is byte-identity across the devices. The
// run also prints a CRC-32 digest of everything it produced, so two devices'
// runs can be compared line for line.
//
//   vectors.bin        packet format 1: assemble_sealed() with the AEAD bypassed,
//                      then open_and_parse() of the frozen bytes
//   tier_vectors.bin   the Tier 1 / Tier 2 determinism surface: tokenizer, n-gram
//                      model and boost, frame layout, literals; tier1_decode() /
//                      tier2_decode()
//
// AEAD bypassed (contract §5.1, §2.2; packet §6.10.2): build with
// ITANTRA_DISABLE_AEAD in a debug configuration. The sealed output itself is
// then compared with the frozen plaintext. A build with the AEAD reports
// AEAD-NOT-BYPASSED and checks nothing.
//
//   c01_device --vectors vectors.bin --tier-vectors tier_vectors.bin
//
// FROZEN artifacts: a failure is an encoder or decoder regression on that
// device, never a reason to regenerate (contract §2.2, §7.2).

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "golden/golden_format.h"
#include "golden/tier_golden_format.h"
#include "itest.h"
#include "packet/parse.h"
#include "tier1/decode.h"
#include "tier2/decode.h"

using namespace itantra;

namespace {

// CRC-32/ISO-HDLC, fed incrementally (golden::crc32 is the one-shot form).
struct Digest {
    u32 state = 0xFFFFFFFFu;
    u64 bytes = 0u;

    void add(const u8* data, std::size_t length) {
        for (std::size_t i = 0u; i < length; ++i) {
            state ^= data[i];
            for (u32 k = 0u; k < 8u; ++k) state = (state & 1u) != 0u ? (state >> 1) ^ 0xEDB88320u : state >> 1;
        }
        bytes += length;
    }
    void add8(u32 v) {
        const u8 b = static_cast<u8>(v & 0xFFu);
        add(&b, 1u);
    }
    void add16(u32 v) {
        add8(v >> 8);
        add8(v);
    }
    void add32(u32 v) {
        add16(v >> 16);
        add16(v & 0xFFFFu);
    }
    void add_metadata(const Metadata& m) {
        add8(static_cast<u32>(m.tier));
        add16(m.symbol_count);
        add8(m.seq);
        add8(m.hash_present ? 1u : 0u);
        add8(static_cast<u32>(m.priority));
        add8(m.negation ? 1u : 0u);
        add8(m.language);
        add16(m.context_hash);
    }
    u32 value() const { return state ^ 0xFFFFFFFFu; }
};

struct Loaded {
    std::string                vectors_path;
    std::string                tier_vectors_path;
    bool                       ok = false;
    std::string                error;
    golden::GoldenFile         file;
    tiergolden::TierGoldenFile tier_file;
    tiergolden::LoadedTables   tier_tables;
    Digest                     encoded;
    Digest                     decoded;
};

Loaded& loaded() {
    static Loaded l;
    return l;
}

bool require_loaded() {
    if (!loaded().ok) std::printf("  golden artifacts not loaded: %s\n", loaded().error.c_str());
    return loaded().ok;
}

SessionKeys device_keys() {
    u8 psk[kPskBytes], initiator[kHelloNonceBytes], responder[kHelloNonceBytes];
    for (u32 i = 0u; i < kPskBytes; ++i) psk[i] = static_cast<u8>(0x3Cu + i);
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        initiator[i] = static_cast<u8>(0x81u ^ i);
        responder[i] = static_cast<u8>(0x17u + 3u * i);
    }
    SessionKeys k{};
    derive_session_keys(psk, initiator, responder, k);
    return k;
}

// A counter whose low 8 bits are the vector's seq (packet §3.6 binding).
SeqCounter counter_for(std::size_t index, u8 seq) {
    return (static_cast<SeqCounter>(index) + 1u) * 256u + seq;
}

const char* build_label() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

}  // namespace

ITEST(C01_vectors_bin_sealed_with_the_aead_bypassed_is_the_frozen_payload) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = device_keys();
    auto plain  = std::make_unique<NativePayload>();
    auto sealed = std::make_unique<SealedPayload>();
    u32 identical = 0u;
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector&  v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const AssemblyInput          in = golden::input_of(v, model);
        const bool plain_same = assemble(in, *plain) == AsmResult::Ok && plain->len == v.payload.size() &&
                                plain->metadata_bits == v.metadata_bits &&
                                std::memcmp(plain->bytes, v.payload.data(), plain->len) == 0;
        const bool sealed_same =
            assemble_sealed(in, keys, Direction::InitiatorToResponder, counter_for(i, v.metadata.seq), *sealed) ==
                AsmResult::Ok &&
            sealed->len == v.payload.size() && std::memcmp(sealed->bytes, v.payload.data(), sealed->len) == 0;
        ITEST_TRUE(plain_same && sealed_same);
        if (!(plain_same && sealed_same)) std::printf("  C-01 ENCODER REGRESSION: vectors.bin %s\n", v.name.c_str());
        if (plain_same && sealed_same) ++identical;
        loaded().encoded.add(sealed->bytes, sealed->len);
    }
    std::printf("  C-01 vectors.bin: %u of %zu vectors byte-identical (sealed, AEAD bypassed)\n", identical,
                f.vectors.size());
}

ITEST(C02_vectors_bin_decodes_to_the_frozen_symbols) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = device_keys();
    auto parsed = std::make_unique<ParsedPayload>();
    u32 identical = 0u;
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector&  v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const golden::FixedSelector  selector(&model);
        const ParseStatus status =
            open_and_parse(v.payload.data(), static_cast<u32>(v.payload.size()), keys,
                           Direction::InitiatorToResponder, counter_for(i, v.metadata.seq), selector, *parsed);
        bool same = status == ParseStatus::Ok && parsed->metadata == v.metadata &&
                    parsed->metadata_bits == v.metadata_bits;
        for (std::size_t k = 0u; same && k < v.symbols.size(); ++k) same = parsed->symbols[k] == v.symbols[k];
        ITEST_TRUE(same);
        if (!same) std::printf("  C-02 DECODER REGRESSION: vectors.bin %s (status %u)\n", v.name.c_str(),
                               static_cast<u32>(status));
        if (same) ++identical;
        if (status == ParseStatus::Ok) {
            loaded().decoded.add_metadata(parsed->metadata);
            for (u32 k = 0u; k < parsed->metadata.symbol_count; ++k) loaded().decoded.add32(parsed->symbols[k]);
        }
    }
    std::printf("  C-02 vectors.bin: %u of %zu vectors decode to their frozen symbols\n", identical, f.vectors.size());
}

ITEST(C01_tier_vectors_bin_re_encodes_byte_identically) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    u32 identical = 0u;
    for (const tiergolden::TierVector& v : loaded().tier_file.vectors) {
        std::vector<Symbol> symbols;
        std::vector<u8>     payload;
        u16                 bits = 0u;
        const bool same = tiergolden::derive_symbols(loaded().tier_tables, v, symbols) && symbols == v.symbols &&
                          tiergolden::encode_symbols(loaded().tier_tables, v, symbols, payload, bits) &&
                          payload == v.payload && bits == v.metadata_bits;
        ITEST_TRUE(same);
        if (!same) std::printf("  C-01 ENCODER REGRESSION: tier_vectors.bin %s\n", v.name.c_str());
        if (same) ++identical;
        loaded().encoded.add(payload.data(), payload.size());
    }
    std::printf("  C-01 tier_vectors.bin: %u of %zu vectors byte-identical\n", identical,
                loaded().tier_file.vectors.size());
}

ITEST(C02_tier_vectors_bin_decodes_to_its_input) {
    if (!require_loaded()) {
        ITEST_TRUE(false);
        return;
    }
    const tiergolden::LoadedTables& t = loaded().tier_tables;
    u32 identical = 0u;
    for (const tiergolden::TierVector& v : loaded().tier_file.vectors) {
        bool same = false;
        if (v.metadata.tier == Tier::Tier2) {
            const Context ctx = tiergolden::context_of(v.context);
            Tier2Decoded  d;
            const bool ok = tier2_decode(t.tier2, v.payload.data(), static_cast<u32>(v.payload.size()),
                                         v.boosted ? &ctx : nullptr, d) == Tier2Status::Ok;
            same = ok && d.text == v.text && d.metadata == v.metadata;
            if (ok) {
                loaded().decoded.add_metadata(d.metadata);
                loaded().decoded.add(reinterpret_cast<const u8*>(d.text.data()), d.text.size());
            }
        } else {
            Tier1Decoded d;
            const bool ok = tier1_decode(t.common, t.tier2, v.payload.data(), static_cast<u32>(v.payload.size()), d) ==
                            Tier1DecodeStatus::Ok;
            same = ok && d.frame == v.frame && d.metadata == v.metadata;
            if (ok) {
                loaded().decoded.add_metadata(d.metadata);
                loaded().decoded.add16(d.frame.intent);
                for (const FrameSlot& s : d.frame.slots) {
                    loaded().decoded.add8(static_cast<u32>(s.mode));
                    loaded().decoded.add16(s.value);
                    loaded().decoded.add(reinterpret_cast<const u8*>(s.literal.data()), s.literal.size());
                }
            }
        }
        ITEST_TRUE(same);
        if (!same) std::printf("  C-02 DECODER REGRESSION: tier_vectors.bin %s\n", v.name.c_str());
        if (same) ++identical;
    }
    std::printf("  C-02 tier_vectors.bin: %u of %zu vectors decode to their input\n", identical,
                loaded().tier_file.vectors.size());
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; ++a) {
        if (std::strcmp(argv[a], "--vectors") == 0) loaded().vectors_path = argv[a + 1];
        if (std::strcmp(argv[a], "--tier-vectors") == 0) loaded().tier_vectors_path = argv[a + 1];
    }
    std::printf("C-01/C-02 device run: build %s\n", build_label());
    if (!kAeadBypassed) {
        std::printf("AEAD-NOT-BYPASSED: C-01/C-02 run with ITANTRA_DISABLE_AEAD in a debug build (contract 5.1)\n");
        return 0;
    }
    if (loaded().vectors_path.empty() || loaded().tier_vectors_path.empty()) {
        std::printf("usage: c01_device --vectors <vectors.bin> --tier-vectors <tier_vectors.bin>\n");
        return 2;
    }

    std::vector<u8> bytes;
    std::vector<u8> tier_bytes;
    if (!golden::read_file(loaded().vectors_path.c_str(), bytes)) {
        loaded().error = "cannot read " + loaded().vectors_path;
    } else if (!golden::read_file(loaded().tier_vectors_path.c_str(), tier_bytes)) {
        loaded().error = "cannot read " + loaded().tier_vectors_path;
    } else {
        loaded().ok = golden::deserialize(bytes, loaded().file, loaded().error) &&
                      tiergolden::deserialize(tier_bytes, loaded().tier_file, loaded().error) &&
                      loaded().tier_tables.load(loaded().tier_file, loaded().error);
    }
    if (loaded().ok) {
        std::printf("frozen artifacts: vectors.bin crc32 0x%08X (%zu vectors), tier_vectors.bin crc32 0x%08X "
                    "(%zu vectors)\n",
                    golden::crc32(bytes.data(), bytes.size() - 4u), loaded().file.vectors.size(),
                    golden::crc32(tier_bytes.data(), tier_bytes.size() - 4u), loaded().tier_file.vectors.size());
    }

    const int result = ::itest::run_all("conformance.c01_device");
    std::printf("C-01 encode digest 0x%08X over %llu bytes\n", loaded().encoded.value(),
                static_cast<unsigned long long>(loaded().encoded.bytes));
    std::printf("C-02 decode digest 0x%08X over %llu bytes\n", loaded().decoded.value(),
                static_cast<unsigned long long>(loaded().decoded.bytes));
    std::printf("C-01/C-02 on this device: %s\n", result == 0 ? "PASS" : "FAIL");
    return result;
}
