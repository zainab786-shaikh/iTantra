// Conformance — C-40 … C-43. Implementation plan Phase 4.
//
//   C-40 [H]  encrypt → decrypt every golden vector payload → exact plaintext recovery
//   C-41 [H]  flip one bit of the tag; flip one bit of the ciphertext
//             → rejected in both cases, never partially decoded
//   C-42 [P]  replay a previously accepted packet → rejected by the replay window
//             DEFERRED to Phase 12 (pair rig). crypto/replay is unit-tested now.
//   C-43 [D]  release build with ITANTRA_DISABLE_AEAD → COMPILE ERROR
//             DEFERRED to Phase 11 (device release build). Host precondition:
//             CTest "C-43.precondition.release-guard".
//
//   c40_c43_test --vectors <vectors.bin>
//
// Vectors are the frozen pre-encryption payloads (packet §6.10.1); nothing
// here changes them.

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
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

SessionKeys test_keys(u8 variant = 0u) {
    u8 psk[32], initiator[32], responder[32];
    for (u32 i = 0u; i < 32u; ++i) {
        psk[i]       = static_cast<u8>(0xA0u + i + variant);
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

const Direction kDirections[2] = {Direction::InitiatorToResponder, Direction::ResponderToInitiator};

std::vector<u8> seal(const SessionKeys& k, Direction d, SeqCounter c, const std::vector<u8>& plain) {
    u8 nonce[kAeadNonceBytes];
    derive_nonce(k.session_id, d, c, nonce);
    std::vector<u8> out(plain.size() + kAeadTagBytes);
    u32 n = 0u;
    aead_seal(k.key, nonce, nullptr, 0u, plain.data(), static_cast<u32>(plain.size()), out.data(),
              static_cast<u32>(out.size()), n);
    out.resize(n);
    return out;
}

struct Opened {
    AeadStatus      status;
    u32             length;
    bool            wiped;      // on failure: every plaintext byte is zero
    std::vector<u8> plain;
};

Opened open(const SessionKeys& k, Direction d, SeqCounter c, const std::vector<u8>& sealed) {
    u8 nonce[kAeadNonceBytes];
    derive_nonce(k.session_id, d, c, nonce);
    Opened o;
    o.plain.assign(sealed.size() + 8u, 0xEE);
    o.length = 0u;
    o.status = aead_open(k.key, nonce, nullptr, 0u, sealed.data(), static_cast<u32>(sealed.size()),
                         o.plain.data(), static_cast<u32>(o.plain.size()), o.length);
    o.wiped = true;
    if (o.status != AeadStatus::Ok && sealed.size() >= kAeadTagBytes) {
        for (std::size_t i = 0u; i < sealed.size() - kAeadTagBytes; ++i) o.wiped = o.wiped && o.plain[i] == 0u;
    }
    return o;
}

// Every byte of short payloads; for long ones the first and last 32 bytes and
// every 97th byte between.
std::vector<u32> flip_bytes(u32 ciphertext_length) {
    std::vector<u32> out;
    for (u32 i = 0u; i < ciphertext_length; ++i) {
        if (ciphertext_length <= 256u || i < 32u || i + 32u >= ciphertext_length || i % 97u == 0u) {
            out.push_back(i);
        }
    }
    return out;
}

class CountingSelector final : public ModelSelector {
public:
    explicit CountingSelector(const PayloadModel* m) : m_(m) {}
    const PayloadModel* select(const Metadata&) const noexcept override {
        ++calls;
        return m_;
    }
    mutable u32 calls = 0u;

private:
    const PayloadModel* m_;
};

bool require_loaded() {
    if (!loaded().ok) std::printf("  golden vectors not loaded: %s\n", loaded().error.c_str());
    return loaded().ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// C-40
// ---------------------------------------------------------------------------

ITEST(C40_encrypt_decrypt_every_golden_vector_payload_exact_recovery) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    u32 recovered = 0u;
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        for (Direction d : kDirections) {
            const SeqCounter c = counter_for(i, v.metadata.seq);
            const std::vector<u8> sealed = seal(keys, d, c, v.payload);
            ITEST_EQ(sealed.size(), v.payload.size() + kAeadTagBytes);
            ITEST_TRUE(std::memcmp(sealed.data(), v.payload.data(), v.payload.size()) != 0);   // it IS encrypted

            const Opened o = open(keys, d, c, sealed);
            const bool exact = o.status == AeadStatus::Ok && o.length == v.payload.size() &&
                               std::memcmp(o.plain.data(), v.payload.data(), o.length) == 0;
            ITEST_TRUE(exact);
            if (exact) ++recovered;
        }
    }
    ITEST_EQ(recovered, 2u * f.vectors.size());
    std::printf("  C-40: %u of %u sealed payloads recovered exactly (%zu vectors x 2 directions)\n", recovered,
                static_cast<u32>(2u * f.vectors.size()), f.vectors.size());
}

ITEST(C40_sealed_assembly_opens_to_the_frozen_plaintext_and_decodes) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    auto sealed = std::make_unique<SealedPayload>();
    auto plain = std::make_unique<NativePayload>();
    auto parsed = std::make_unique<ParsedPayload>();
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const SeqCounter c = counter_for(i, v.metadata.seq);
        ITEST_TRUE(assemble_sealed(golden::input_of(v, model), keys, Direction::ResponderToInitiator, c, *sealed) ==
                   AsmResult::Ok);
        ITEST_EQ(sealed->len, v.payload.size() + kAeadTagBytes);

        // The golden vector boundary survives encryption exactly.
        ITEST_TRUE(open_payload(sealed->bytes, sealed->len, keys, Direction::ResponderToInitiator, c, *plain) ==
                   ParseStatus::Ok);
        ITEST_TRUE(plain->len == v.payload.size() && std::memcmp(plain->bytes, v.payload.data(), plain->len) == 0);

        const golden::FixedSelector selector(&model);
        const ParseStatus s = open_and_parse(sealed->bytes, sealed->len, keys, Direction::ResponderToInitiator, c,
                                             selector, *parsed);
        bool same = s == ParseStatus::Ok && parsed->metadata == v.metadata;
        for (std::size_t k = 0u; same && k < v.symbols.size(); ++k) same = parsed->symbols[k] == v.symbols[k];
        ITEST_TRUE(same);
    }
}

ITEST(C40_nonce_inputs_change_the_ciphertext) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenVector& v = loaded().file.vectors[2];
    const SessionKeys keys = test_keys();
    const SeqCounter c = counter_for(0u, v.metadata.seq);
    const std::vector<u8> base = seal(keys, Direction::InitiatorToResponder, c, v.payload);
    ITEST_TRUE(seal(keys, Direction::ResponderToInitiator, c, v.payload) != base);
    ITEST_TRUE(seal(keys, Direction::InitiatorToResponder, c + 256u, v.payload) != base);
    ITEST_TRUE(seal(test_keys(1u), Direction::InitiatorToResponder, c, v.payload) != base);
    ITEST_TRUE(seal(keys, Direction::InitiatorToResponder, c, v.payload) == base);   // deterministic
}

// ---------------------------------------------------------------------------
// C-41
// ---------------------------------------------------------------------------

ITEST(C41_every_tag_bit_flip_is_rejected_for_every_golden_vector) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    u32 flips = 0u, rejected = 0u;
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const SeqCounter c = counter_for(i, v.metadata.seq);
        const std::vector<u8> sealed = seal(keys, Direction::InitiatorToResponder, c, v.payload);
        const std::size_t tag_at = v.payload.size();
        for (u32 bit = 0u; bit < kAeadTagBytes * 8u; ++bit) {
            std::vector<u8> t = sealed;
            t[tag_at + bit / 8u] = static_cast<u8>(t[tag_at + bit / 8u] ^ (0x80u >> (bit % 8u)));
            const Opened o = open(keys, Direction::InitiatorToResponder, c, t);
            ++flips;
            const bool ok = o.status == AeadStatus::AuthenticationFailed && o.length == 0u && o.wiped;
            ITEST_TRUE(ok);
            if (ok) ++rejected;
        }
    }
    ITEST_EQ(rejected, flips);
    std::printf("  C-41 tag: %u of %u single-bit tag flips rejected, plaintext wiped\n", rejected, flips);
}

ITEST(C41_ciphertext_bit_flips_are_rejected_for_every_golden_vector) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    u32 flips = 0u, rejected = 0u;
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const SeqCounter c = counter_for(i, v.metadata.seq);
        const std::vector<u8> sealed = seal(keys, Direction::ResponderToInitiator, c, v.payload);
        for (u32 byte : flip_bytes(static_cast<u32>(v.payload.size()))) {
            for (u32 bit = 0u; bit < 8u; ++bit) {
                std::vector<u8> t = sealed;
                t[byte] = static_cast<u8>(t[byte] ^ (1u << bit));
                const Opened o = open(keys, Direction::ResponderToInitiator, c, t);
                ++flips;
                const bool ok = o.status == AeadStatus::AuthenticationFailed && o.length == 0u && o.wiped;
                ITEST_TRUE(ok);
                if (ok) ++rejected;
            }
        }
    }
    ITEST_EQ(rejected, flips);
    std::printf("  C-41 ciphertext: %u of %u single-bit ciphertext flips rejected, plaintext wiped\n", rejected, flips);
}

ITEST(C41_rejected_packets_are_never_parsed_or_decoded) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    auto sealed = std::make_unique<SealedPayload>();
    auto parsed = std::make_unique<ParsedPayload>();
    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const golden::ScheduledModel model(f.models, v);
        const SeqCounter c = counter_for(i, v.metadata.seq);
        ITEST_TRUE(assemble_sealed(golden::input_of(v, model), keys, Direction::InitiatorToResponder, c, *sealed) ==
                   AsmResult::Ok);
        const u32 flip_positions[2] = {0u, sealed->len - 1u};   // first ciphertext bit, last tag bit
        for (u32 pos : flip_positions) {
            std::vector<u8> t(sealed->bytes, sealed->bytes + sealed->len);
            t[pos] = static_cast<u8>(t[pos] ^ (pos == 0u ? 0x80u : 0x01u));
            CountingSelector selector(&model);
            const ParseStatus s = open_and_parse(t.data(), static_cast<u32>(t.size()), keys,
                                                 Direction::InitiatorToResponder, c, selector, *parsed);
            ITEST_TRUE(s == ParseStatus::AuthenticationFailed);
            ITEST_EQ(selector.calls, 0u);   // no model selected, no symbol decoded
        }
    }
}

ITEST(C41_wrong_key_session_direction_counter_or_length_is_rejected) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const SessionKeys keys = test_keys();
    SessionKeys other_session = keys;
    other_session.session_id ^= 1u;
    const SessionKeys other_key = test_keys(1u);

    for (std::size_t i = 0u; i < f.vectors.size(); ++i) {
        const golden::GoldenVector& v = f.vectors[i];
        const SeqCounter c = counter_for(i, v.metadata.seq);
        const std::vector<u8> sealed = seal(keys, Direction::InitiatorToResponder, c, v.payload);

        ITEST_TRUE(open(other_key, Direction::InitiatorToResponder, c, sealed).status == AeadStatus::AuthenticationFailed);
        ITEST_TRUE(open(other_session, Direction::InitiatorToResponder, c, sealed).status == AeadStatus::AuthenticationFailed);
        ITEST_TRUE(open(keys, Direction::ResponderToInitiator, c, sealed).status == AeadStatus::AuthenticationFailed);
        ITEST_TRUE(open(keys, Direction::InitiatorToResponder, c + 1u, sealed).status == AeadStatus::AuthenticationFailed);
        ITEST_TRUE(open(keys, Direction::InitiatorToResponder, c + 256u, sealed).status == AeadStatus::AuthenticationFailed);

        std::vector<u8> shorter(sealed.begin(), sealed.end() - 1);
        ITEST_TRUE(open(keys, Direction::InitiatorToResponder, c, shorter).status == AeadStatus::AuthenticationFailed);
        std::vector<u8> longer = sealed;
        longer.push_back(0x00);
        ITEST_TRUE(open(keys, Direction::InitiatorToResponder, c, longer).status == AeadStatus::AuthenticationFailed);

        if (i > 0u) {   // another packet's tag
            const golden::GoldenVector& prev = f.vectors[i - 1u];
            const std::vector<u8> prev_sealed = seal(keys, Direction::InitiatorToResponder, counter_for(i - 1u, prev.metadata.seq), prev.payload);
            std::vector<u8> swapped = sealed;
            for (u32 k = 0u; k < kAeadTagBytes; ++k) {
                swapped[v.payload.size() + k] = prev_sealed[prev.payload.size() + k];
            }
            ITEST_TRUE(open(keys, Direction::InitiatorToResponder, c, swapped).status == AeadStatus::AuthenticationFailed);
        }
    }
}

// ---------------------------------------------------------------------------
// seq ↔ nonce counter binding
// ---------------------------------------------------------------------------

ITEST(seq_on_the_wire_is_bound_to_the_nonce_counter) {
    ITEST_TRUE(require_loaded());
    if (!loaded().ok) return;
    const golden::GoldenFile& f = loaded().file;
    const golden::GoldenVector& v = f.vectors[2];
    const golden::ScheduledModel model(f.models, v);
    const SessionKeys keys = test_keys();
    const SeqCounter c = counter_for(0u, v.metadata.seq);
    auto sealed = std::make_unique<SealedPayload>();

    // Sender: a counter whose low 8 bits are not the seq is refused.
    ITEST_TRUE(assemble_sealed(golden::input_of(v, model), keys, Direction::InitiatorToResponder, c + 1u, *sealed) ==
               AsmResult::InvalidCounter);
    ITEST_EQ(sealed->len, 0u);
    ITEST_TRUE(assemble_sealed(golden::input_of(v, model), keys, Direction::InitiatorToResponder, 0u, *sealed) ==
               AsmResult::InvalidCounter);

    // Receiver: a packet that authenticates under counter c but whose
    // plaintext seq is not c's low bits is reported, not decoded.
    const std::vector<u8> mismatched = seal(keys, Direction::InitiatorToResponder, c + 1u, v.payload);
    auto parsed = std::make_unique<ParsedPayload>();
    CountingSelector selector(&model);
    ITEST_TRUE(open_and_parse(mismatched.data(), static_cast<u32>(mismatched.size()), keys,
                              Direction::InitiatorToResponder, c + 1u, selector, *parsed) == ParseStatus::SeqMismatch);
    ITEST_EQ(selector.calls, 0u);

    ITEST_TRUE(open_and_parse(mismatched.data(), static_cast<u32>(mismatched.size()), keys,
                              Direction::InitiatorToResponder, 0u, selector, *parsed) == ParseStatus::InvalidCounter);
}

int main(int argc, char** argv) {
    std::printf("C-42 [P] SKIPPED on host: replay of an accepted packet needs the pair rig (Phase 12)\n");
    std::printf("C-43 [D] SKIPPED on host: release device build (Phase 11); host precondition is CTest "
                "'C-43.precondition.release-guard'\n");
    if (kAeadBypassed) {
        std::printf("AEAD-BYPASSED: C-40/C-41 are meaningless with ITANTRA_DISABLE_AEAD\n");
        return 0;
    }
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--vectors") == 0) loaded().path = argv[a + 1];
    }
    std::vector<u8> bytes;
    if (loaded().path == nullptr || !golden::read_file(loaded().path, bytes)) {
        loaded().error = "cannot read --vectors file";
    } else {
        loaded().ok = golden::deserialize(bytes, loaded().file, loaded().error);
    }
    return ::itest::run_all("conformance.c40_c43");
}
