// Unit tests — crypto/. Implementation plan Phase 4.
//
//   unit  nonce derivation is deterministic for a given counter
//   unit  replay window accepts in-order, rejects seen, tolerates gaps
//
// Plus the known answers the pinned cipher suite and KDF are held to:
//   - RFC 8439 §2.8.2 AEAD test vector, byte for byte, through the vendored
//     Monocypher directly and through crypto/aead (4-byte tag)
//   - HKDF-SHA-512 session keys against an independent implementation
//     (.NET HMACSHA512, itself checked against RFC 4231 test case 2)

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "crypto/replay.h"
#include "third_party/monocypher/monocypher.h"
#include "itest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

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

// ---- RFC 8439 §2.8.2, copied from the RFC text ----------------------------

const char kRfcPlaintext[] =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
    "sunscreen would be it.";

const u8 kRfcAad[12] = {0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};

// 32-bit fixed-common part 07 00 00 00, then IV 40 … 47
const u8 kRfcNonce[12] = {0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};

const u8 kRfcCiphertext[114] = {
    0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2,
    0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe, 0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6,
    0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
    0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6, 0x7e, 0xcd, 0x3b, 0x36,
    0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c, 0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58,
    0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
    0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5, 0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b,
    0x61, 0x16,
};

const u8 kRfcTag[16] = {0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a,
                        0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91};

void rfc_key(u8 key[32]) {
    for (u32 i = 0u; i < 32u; ++i) key[i] = static_cast<u8>(0x80u + i);
}

const u8* rfc_plaintext() {
    return reinterpret_cast<const u8*>(kRfcPlaintext);
}

std::vector<u8> rfc_sealed() {
    std::vector<u8> s(kRfcCiphertext, kRfcCiphertext + 114);
    s.insert(s.end(), kRfcTag, kRfcTag + kAeadTagBytes);
    return s;
}

// ---- HKDF-SHA-512 reference (computed independently, see file header) ------

void kdf_inputs(u8 psk[32], u8 initiator[32], u8 responder[32]) {
    for (u32 i = 0u; i < 32u; ++i) {
        psk[i]       = static_cast<u8>(i);
        initiator[i] = static_cast<u8>(32u + i);
        responder[i] = static_cast<u8>(64u + i);
    }
}

const u8 kExpectedKey[32] = {0x69, 0x65, 0x09, 0xa1, 0x0c, 0x4f, 0xe6, 0x06, 0xa2, 0x18, 0xd3,
                             0xc5, 0x95, 0x2c, 0x2e, 0x90, 0xf6, 0x96, 0xfe, 0x47, 0x42, 0xc4,
                             0x89, 0x8e, 0xdd, 0xd2, 0x09, 0x61, 0x29, 0x02, 0xb7, 0x2e};
constexpr u32 kExpectedSessionId = 0xD4180CFAu;

const u8 kSwappedKey[32] = {0x30, 0x6d, 0x06, 0x6f, 0xbf, 0x67, 0x31, 0xb2, 0x7d, 0x44, 0x38,
                            0x36, 0x6d, 0xbf, 0xe2, 0xf9, 0x09, 0x7e, 0x48, 0xf8, 0xbf, 0xbf,
                            0x61, 0x42, 0x41, 0x1b, 0x08, 0xd8, 0x77, 0x4d, 0xa2, 0x95};
constexpr u32 kSwappedSessionId = 0x278FFEB2u;

}  // namespace

// ---------------------------------------------------------------------------
// Cipher suite — RFC 8439 known answer
// ---------------------------------------------------------------------------

ITEST(rfc8439_plaintext_is_the_114_bytes_the_rfc_lists) {
    ITEST_EQ(std::strlen(kRfcPlaintext), 114u);
}

ITEST(vendored_monocypher_ietf_aead_matches_rfc8439_section_2_8_2) {
    u8 key[32];
    rfc_key(key);
    u8 cipher[114];
    u8 tag[16];
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, kRfcNonce);
    crypto_aead_write(&ctx, cipher, tag, kRfcAad, 12u, rfc_plaintext(), 114u);
    ITEST_TRUE(std::memcmp(cipher, kRfcCiphertext, 114u) == 0);
    ITEST_TRUE(std::memcmp(tag, kRfcTag, 16u) == 0);

    u8 plain[114];
    crypto_aead_init_ietf(&ctx, key, kRfcNonce);
    ITEST_EQ(crypto_aead_read(&ctx, plain, kRfcTag, kRfcAad, 12u, kRfcCiphertext, 114u), 0u);
    ITEST_TRUE(std::memcmp(plain, rfc_plaintext(), 114u) == 0);
}

ITEST(aead_seal_matches_rfc8439_with_the_4_byte_truncated_tag) {
    ITEST_TRUE(!kAeadBypassed);
    u8 key[32];
    rfc_key(key);
    u8 out[200];
    u32 len = 0u;
    ITEST_TRUE(aead_seal(key, kRfcNonce, kRfcAad, 12u, rfc_plaintext(), 114u, out, 200u, len) ==
               AeadStatus::Ok);
    ITEST_EQ(len, 118u);
    ITEST_TRUE(std::memcmp(out, kRfcCiphertext, 114u) == 0);
    ITEST_TRUE(std::memcmp(out + 114, kRfcTag, 4u) == 0);   // the FIRST 4 bytes of the tag
}

ITEST(aead_open_recovers_the_rfc8439_plaintext) {
    u8 key[32];
    rfc_key(key);
    const std::vector<u8> sealed = rfc_sealed();
    u8 plain[200];
    u32 len = 0u;
    ITEST_TRUE(aead_open(key, kRfcNonce, kRfcAad, 12u, sealed.data(), 118u, plain, 200u, len) ==
               AeadStatus::Ok);
    ITEST_EQ(len, 114u);
    ITEST_TRUE(std::memcmp(plain, rfc_plaintext(), 114u) == 0);
}

ITEST(aead_open_rejects_every_single_bit_flip_and_releases_nothing) {
    u8 key[32];
    rfc_key(key);
    const std::vector<u8> sealed = rfc_sealed();
    u32 rejected = 0u;
    for (u32 bit = 0u; bit < 118u * 8u; ++bit) {
        std::vector<u8> tampered = sealed;
        tampered[bit / 8u] = static_cast<u8>(tampered[bit / 8u] ^ (0x80u >> (bit % 8u)));
        u8 plain[200];
        std::memset(plain, 0xEE, sizeof plain);
        u32 len = 99u;
        const AeadStatus s = aead_open(key, kRfcNonce, kRfcAad, 12u, tampered.data(), 118u, plain, 200u, len);
        ITEST_TRUE(s == AeadStatus::AuthenticationFailed);
        ITEST_EQ(len, 0u);
        bool wiped = true;
        for (u32 i = 0u; i < 114u; ++i) wiped = wiped && plain[i] == 0u;
        ITEST_TRUE(wiped);
        if (s == AeadStatus::AuthenticationFailed) ++rejected;
    }
    ITEST_EQ(rejected, 118u * 8u);
}

ITEST(aead_open_rejects_wrong_key_nonce_associated_data_and_length) {
    u8 key[32];
    rfc_key(key);
    const std::vector<u8> sealed = rfc_sealed();
    u8 plain[200];
    u32 len = 0u;

    u8 other_key[32];
    rfc_key(other_key);
    other_key[31] ^= 1u;
    ITEST_TRUE(aead_open(other_key, kRfcNonce, kRfcAad, 12u, sealed.data(), 118u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);

    u8 other_nonce[12];
    std::memcpy(other_nonce, kRfcNonce, 12u);
    other_nonce[11] ^= 1u;
    ITEST_TRUE(aead_open(key, other_nonce, kRfcAad, 12u, sealed.data(), 118u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);

    u8 other_aad[12];
    std::memcpy(other_aad, kRfcAad, 12u);
    other_aad[0] ^= 1u;
    ITEST_TRUE(aead_open(key, kRfcNonce, other_aad, 12u, sealed.data(), 118u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);
    ITEST_TRUE(aead_open(key, kRfcNonce, nullptr, 0u, sealed.data(), 118u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);

    ITEST_TRUE(aead_open(key, kRfcNonce, kRfcAad, 12u, sealed.data(), 117u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);   // truncated
    std::vector<u8> longer = sealed;
    longer.push_back(0x00);
    ITEST_TRUE(aead_open(key, kRfcNonce, kRfcAad, 12u, longer.data(), 119u, plain, 200u, len) ==
               AeadStatus::AuthenticationFailed);   // extended
    for (u32 n = 0u; n < kAeadTagBytes; ++n) {
        ITEST_TRUE(aead_open(key, kRfcNonce, kRfcAad, 12u, sealed.data(), n, plain, 200u, len) ==
                   AeadStatus::AuthenticationFailed);   // cannot even hold a tag
    }
}

ITEST(aead_round_trips_every_length_up_to_300_and_in_place) {
    XorShift32 rng{0x0DDBA11u};
    for (u32 length = 0u; length <= 300u; ++length) {
        u8 key[32];
        u8 nonce[12];
        for (u8& b : key) b = static_cast<u8>(rng.next());
        for (u8& b : nonce) b = static_cast<u8>(rng.next());
        std::vector<u8> plain(length);
        for (u8& b : plain) b = static_cast<u8>(rng.next());

        std::vector<u8> sealed(length + kAeadTagBytes);
        u32 sealed_len = 0u;
        ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, plain.data(), length, sealed.data(),
                             static_cast<u32>(sealed.size()), sealed_len) == AeadStatus::Ok);
        ITEST_EQ(sealed_len, length + kAeadTagBytes);

        std::vector<u8> back(length + 1u);
        u32 back_len = 0u;
        ITEST_TRUE(aead_open(key, nonce, nullptr, 0u, sealed.data(), sealed_len, back.data(),
                             static_cast<u32>(back.size()), back_len) == AeadStatus::Ok);
        ITEST_EQ(back_len, length);
        ITEST_TRUE(std::memcmp(back.data(), plain.data(), length) == 0);

        // in place: seal into the plaintext buffer, open back into it
        std::vector<u8> buffer(plain);
        buffer.resize(length + kAeadTagBytes);
        u32 n = 0u;
        ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, buffer.data(), length, buffer.data(),
                             static_cast<u32>(buffer.size()), n) == AeadStatus::Ok);
        ITEST_TRUE(std::memcmp(buffer.data(), sealed.data(), sealed_len) == 0);
        ITEST_TRUE(aead_open(key, nonce, nullptr, 0u, buffer.data(), n, buffer.data(),
                             static_cast<u32>(buffer.size()), n) == AeadStatus::Ok);
        ITEST_TRUE(std::memcmp(buffer.data(), plain.data(), length) == 0);
    }
}

ITEST(aead_rejects_invalid_arguments) {
    u8 key[32] = {};
    u8 nonce[12] = {};
    u8 data[16] = {};
    u8 out[64];
    u32 len = 7u;
    ITEST_TRUE(aead_seal(nullptr, nonce, nullptr, 0u, data, 16u, out, 64u, len) == AeadStatus::InvalidArgument);
    ITEST_EQ(len, 0u);
    ITEST_TRUE(aead_seal(key, nullptr, nullptr, 0u, data, 16u, out, 64u, len) == AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_seal(key, nonce, nullptr, 3u, data, 16u, out, 64u, len) == AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, nullptr, 16u, out, 64u, len) == AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, data, 16u, out, 19u, len) == AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_seal(key, nonce, nullptr, 0u, data, kAeadMaxMessageBytes + 1u, out, 64u, len) ==
               AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_open(key, nonce, nullptr, 0u, data, 16u, out, 11u, len) == AeadStatus::InvalidArgument);
    ITEST_TRUE(aead_open(key, nonce, nullptr, 0u, nullptr, 16u, out, 64u, len) == AeadStatus::InvalidArgument);
}

ITEST(secure_wipe_zeroes_the_buffer) {
    u8 buf[33];
    std::memset(buf, 0xA5, sizeof buf);
    secure_wipe(buf, 32u);
    for (u32 i = 0u; i < 32u; ++i) ITEST_EQ(buf[i], 0u);
    ITEST_EQ(buf[32], 0xA5u);
    secure_wipe(nullptr, 5u);   // no crash
}

// ---------------------------------------------------------------------------
// Nonce derivation — packet §6.5
// ---------------------------------------------------------------------------

ITEST(nonce_layout_matches_hand_computed_bytes) {
    //   session_id 01020304 | direction 1, counter 5 → 80 00 00 00 00 00 00 05
    u8 n[12];
    ITEST_TRUE(derive_nonce(0x01020304u, Direction::ResponderToInitiator, 5u, n));
    const u8 a[12] = {0x01, 0x02, 0x03, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05};
    ITEST_TRUE(std::memcmp(n, a, 12u) == 0);

    //   session_id DEADBEEF | direction 0, counter 0123456789ABCDEF
    ITEST_TRUE(derive_nonce(0xDEADBEEFu, Direction::InitiatorToResponder, 0x0123456789ABCDEFull, n));
    const u8 b[12] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    ITEST_TRUE(std::memcmp(n, b, 12u) == 0);

    //   largest valid counter, direction 1 → all ones
    ITEST_TRUE(derive_nonce(0u, Direction::ResponderToInitiator, kMaxCounter, n));
    const u8 c[12] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ITEST_TRUE(std::memcmp(n, c, 12u) == 0);
}

ITEST(nonce_derivation_is_deterministic_for_a_given_counter) {
    XorShift32 rng{0x5EED5EEDu};
    for (u32 round = 0u; round < 1000u; ++round) {
        const u32 sid = rng.next();
        const SeqCounter counter = 1u + ((SeqCounter{rng.next()} << 31) ^ rng.next());
        const Direction dir = (rng.next() & 1u) != 0u ? Direction::ResponderToInitiator : Direction::InitiatorToResponder;
        u8 first[12];
        ITEST_TRUE(derive_nonce(sid, dir, counter, first));
        for (u32 again = 0u; again < 10u; ++again) {
            u8 n[12];
            ITEST_TRUE(derive_nonce(sid, dir, counter, n));
            ITEST_TRUE(std::memcmp(n, first, 12u) == 0);
        }
    }
}

ITEST(nonce_rejects_counter_zero_counters_above_the_maximum_and_bad_directions) {
    u8 n[12];
    std::memset(n, 0x77, sizeof n);
    ITEST_TRUE(!derive_nonce(1u, Direction::InitiatorToResponder, 0u, n));
    ITEST_TRUE(!derive_nonce(1u, Direction::InitiatorToResponder, kMaxCounter + 1u, n));
    ITEST_TRUE(!derive_nonce(1u, Direction::InitiatorToResponder, 0xFFFFFFFFFFFFFFFFull, n));
    ITEST_TRUE(!derive_nonce(1u, static_cast<Direction>(2u), 1u, n));
    ITEST_TRUE(!derive_nonce(1u, Direction::InitiatorToResponder, 1u, nullptr));
    for (u8 b : n) ITEST_EQ(b, 0x77u);   // nothing written
    ITEST_EQ(kFirstCounter, 1u);
}

ITEST(nonces_never_repeat_across_100000_messages_in_each_direction) {
    // C-33 is [D] (Phase 11). This is its host precondition: the derivation
    // itself cannot produce a repeat.
    std::vector<std::array<u8, 12>> all;
    all.reserve(200000u);
    const Direction dirs[2] = {Direction::InitiatorToResponder, Direction::ResponderToInitiator};
    for (Direction d : dirs) {
        for (SeqCounter c = kFirstCounter; c <= 100000u; ++c) {
            std::array<u8, 12> n{};
            ITEST_TRUE(derive_nonce(0x5A5A5A5Au, d, c, n.data()));
            all.push_back(n);
        }
    }
    std::sort(all.begin(), all.end());
    ITEST_TRUE(std::adjacent_find(all.begin(), all.end()) == all.end());
    ITEST_EQ(all.size(), 200000u);
}

// ---------------------------------------------------------------------------
// KDF — packet §6.7
// ---------------------------------------------------------------------------

ITEST(kdf_matches_an_independent_hkdf_sha512_implementation) {
    u8 psk[32], initiator[32], responder[32];
    kdf_inputs(psk, initiator, responder);
    SessionKeys keys{};
    derive_session_keys(psk, initiator, responder, keys);
    ITEST_TRUE(std::memcmp(keys.key, kExpectedKey, 32u) == 0);
    ITEST_EQ(keys.session_id, kExpectedSessionId);
}

ITEST(kdf_nonce_order_is_part_of_the_derivation) {
    u8 psk[32], initiator[32], responder[32];
    kdf_inputs(psk, initiator, responder);
    SessionKeys swapped{};
    derive_session_keys(psk, responder, initiator, swapped);
    ITEST_TRUE(std::memcmp(swapped.key, kSwappedKey, 32u) == 0);
    ITEST_EQ(swapped.session_id, kSwappedSessionId);
}

ITEST(kdf_is_deterministic_and_every_input_bit_changes_the_key) {
    u8 psk[32], initiator[32], responder[32];
    kdf_inputs(psk, initiator, responder);
    SessionKeys base{};
    derive_session_keys(psk, initiator, responder, base);
    for (u32 i = 0u; i < 100u; ++i) {
        SessionKeys again{};
        derive_session_keys(psk, initiator, responder, again);
        ITEST_TRUE(std::memcmp(again.key, base.key, 32u) == 0 && again.session_id == base.session_id);
    }
    u8* const inputs[3] = {psk, initiator, responder};
    for (u8* input : inputs) {
        for (u32 bit = 0u; bit < 256u; ++bit) {
            input[bit / 8u] = static_cast<u8>(input[bit / 8u] ^ (1u << (bit % 8u)));
            SessionKeys k{};
            derive_session_keys(psk, initiator, responder, k);
            ITEST_TRUE(std::memcmp(k.key, base.key, 32u) != 0);
            input[bit / 8u] = static_cast<u8>(input[bit / 8u] ^ (1u << (bit % 8u)));
        }
    }
}

ITEST(kdf_constants_are_the_pinned_version_1_values) {
    ITEST_EQ(kKdfVersion, 1u);
    ITEST_EQ(kPskBytes, 32u);
    ITEST_EQ(kHelloNonceBytes, 32u);
    ITEST_EQ(kKdfInfoBytes, 18u);
    ITEST_TRUE(std::memcmp(kKdfInfo, "iTantra-session-v1", 18u) == 0);
}

ITEST(wipe_session_keys_clears_the_key) {
    u8 psk[32], initiator[32], responder[32];
    kdf_inputs(psk, initiator, responder);
    SessionKeys keys{};
    derive_session_keys(psk, initiator, responder, keys);
    wipe_session_keys(keys);
    for (u8 b : keys.key) ITEST_EQ(b, 0u);
    ITEST_EQ(keys.session_id, 0u);
}

// ---------------------------------------------------------------------------
// Replay window — packet §6.8
// ---------------------------------------------------------------------------

ITEST(replay_window_accepts_in_order_and_rejects_what_it_has_seen) {
    ReplayWindow w;
    ITEST_EQ(w.largest_accepted(), 0u);
    for (SeqCounter c = 1u; c <= 1000u; ++c) {
        ITEST_TRUE(w.check(c) == ReplayVerdict::Fresh);
        ITEST_TRUE(w.accept(c));
        ITEST_TRUE(w.check(c) == ReplayVerdict::Replayed);
        ITEST_TRUE(!w.accept(c));
    }
    ITEST_EQ(w.largest_accepted(), 1000u);
    for (SeqCounter c = 873u; c <= 1000u; ++c) ITEST_TRUE(w.check(c) == ReplayVerdict::Replayed);
}

ITEST(replay_window_tolerates_gaps_and_late_arrivals_inside_the_window) {
    ReplayWindow w;
    ITEST_TRUE(w.accept(1u));
    ITEST_TRUE(w.accept(5u));      // 2, 3, 4 lost or late
    ITEST_TRUE(w.accept(40u));
    ITEST_TRUE(w.accept(3u));      // late, inside the window: accepted once
    ITEST_TRUE(!w.accept(3u));
    ITEST_TRUE(w.check(2u) == ReplayVerdict::Fresh);
    ITEST_TRUE(w.check(4u) == ReplayVerdict::Fresh);
    ITEST_TRUE(w.check(5u) == ReplayVerdict::Replayed);
    ITEST_TRUE(w.accept(200u));    // jump of 160 — 40 falls out
    ITEST_TRUE(w.check(40u) == ReplayVerdict::TooOld);
    ITEST_TRUE(w.check(73u) == ReplayVerdict::Fresh);    // age 127, never seen
    ITEST_TRUE(w.check(72u) == ReplayVerdict::TooOld);   // age 128
    ITEST_EQ(w.largest_accepted(), 200u);
}

ITEST(replay_window_edge_of_the_128_counter_window) {
    ReplayWindow w;
    ITEST_TRUE(w.accept(1000u));
    for (u32 age = 1u; age < kReplayWindowCounters; ++age) {
        ITEST_TRUE(w.check(1000u - age) == ReplayVerdict::Fresh);
        ITEST_TRUE(w.accept(1000u - age));
        ITEST_TRUE(w.check(1000u - age) == ReplayVerdict::Replayed);
    }
    ITEST_TRUE(w.check(1000u - kReplayWindowCounters) == ReplayVerdict::TooOld);
    // advancing by 64 exactly and by 63 keeps the right bits
    ITEST_TRUE(w.accept(1064u));
    ITEST_TRUE(w.check(1000u) == ReplayVerdict::Replayed);
    ITEST_TRUE(w.check(937u) == ReplayVerdict::Replayed);    // age 127
    ITEST_TRUE(w.check(936u) == ReplayVerdict::TooOld);
    ITEST_TRUE(w.accept(1127u));
    ITEST_TRUE(w.check(1000u) == ReplayVerdict::Replayed);   // age 127
    ITEST_TRUE(w.check(1064u) == ReplayVerdict::Replayed);
    ITEST_TRUE(w.check(1100u) == ReplayVerdict::Fresh);
}

ITEST(replay_check_never_marks_so_forgeries_cannot_poison_the_window) {
    ReplayWindow w;
    ITEST_TRUE(w.accept(10u));
    for (u32 i = 0u; i < 1000u; ++i) {
        ITEST_TRUE(w.check(11u) == ReplayVerdict::Fresh);   // e.g. a packet that then fails the tag
        ITEST_TRUE(w.check(9u) == ReplayVerdict::Fresh);
    }
    ITEST_EQ(w.largest_accepted(), 10u);
    ITEST_TRUE(w.accept(11u));
    ITEST_TRUE(w.accept(9u));
}

ITEST(replay_window_rejects_counter_zero_and_counters_above_the_maximum) {
    ReplayWindow w;
    ITEST_TRUE(w.check(0u) == ReplayVerdict::Invalid);
    ITEST_TRUE(!w.accept(0u));
    ITEST_TRUE(w.check(kMaxCounter + 1u) == ReplayVerdict::Invalid);
    ITEST_TRUE(w.accept(kMaxCounter));
    ITEST_TRUE(w.check(kMaxCounter) == ReplayVerdict::Replayed);
    ITEST_EQ(w.largest_accepted(), kMaxCounter);
}

ITEST(replay_window_matches_a_reference_model_over_random_traffic) {
    ReplayWindow w;
    std::set<SeqCounter> seen;
    SeqCounter largest = 0u;
    SeqCounter head = 1u;
    XorShift32 rng{0xFACEB00Cu};
    for (u32 i = 0u; i < 200000u; ++i) {
        const u32 r = rng.below(100u);
        SeqCounter c = 0u;
        if (r < 60u) {
            head += rng.below(4u);          // new, small gaps
            c = head;
        } else if (r < 90u) {
            const SeqCounter back = rng.below(200u);   // late, some too old
            c = head > back ? head - back : 1u;
        } else {
            head += 100u + rng.below(200u); // big jump
            c = head;
        }

        ReplayVerdict expect = ReplayVerdict::Fresh;
        if (c > largest) {
            expect = ReplayVerdict::Fresh;
        } else if (largest - c >= kReplayWindowCounters) {
            expect = ReplayVerdict::TooOld;
        } else if (seen.count(c) != 0u) {
            expect = ReplayVerdict::Replayed;
        }
        const ReplayVerdict got = w.check(c);
        ITEST_TRUE(got == expect);

        if (got == ReplayVerdict::Fresh && rng.below(10u) != 0u) {   // most fresh packets authenticate
            ITEST_TRUE(w.accept(c));
            seen.insert(c);
            if (c > largest) largest = c;
        }
    }
}

int main() {
    if (kAeadBypassed) {
        std::printf("AEAD-BYPASSED: this build has ITANTRA_DISABLE_AEAD; the cipher known answers do not apply\n");
        return 0;
    }
    return ::itest::run_all("unit.crypto");
}
