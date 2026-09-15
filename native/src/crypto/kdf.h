#pragma once

// Session key derivation — packet §6.7, §8.3.
//
// "Pre-shared key, provisioned at pairing → per-session key =
//  KDF(PSK, nonce_A, nonce_B)". Both phones contribute randomness at HELLO.
//
// PINNED (Phase 4) as KDF version 1 — HKDF-SHA-512, RFC 5869:
//
//   PRK = HKDF-Extract(salt = initiator_nonce ‖ responder_nonce,   64 bytes
//                      IKM  = PSK)                                 32 bytes
//   OKM = HKDF-Expand(PRK, info = ASCII "iTantra-session-v1", L = 36)
//
//   session_key = OKM[0 .. 32)
//   session_id  = OKM[32 .. 36), big-endian u32        (nonce bytes 0..3)
//
// Nonce order is by HELLO role, never by value, so both phones build the same
// salt. The PSK is the secret; the HELLO nonces are public randomness, which
// is what HKDF's salt is for.
//
// NOT here: provisioning or storing the PSK, generating the HELLO nonces (a
// CSPRNG on the device), and the HELLO exchange itself (pairing, Phase 12).

#include "common/types.h"

namespace itantra {

constexpr u8  kKdfVersion      = 1u;
constexpr u32 kPskBytes        = 32u;
constexpr u32 kHelloNonceBytes = 32u;
constexpr u32 kSessionKeyBytes = 32u;

constexpr char kKdfInfo[]    = "iTantra-session-v1";
constexpr u32  kKdfInfoBytes = sizeof(kKdfInfo) - 1u;   // no terminator

struct SessionKeys {
    u8  key[kSessionKeyBytes];
    u32 session_id;
};

void derive_session_keys(const u8* psk, const u8* initiator_nonce, const u8* responder_nonce,
                         SessionKeys& out) noexcept;

// Overwrites the key material. Call when a session ends.
void wipe_session_keys(SessionKeys& keys) noexcept;

}  // namespace itantra
