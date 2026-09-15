#pragma once

// ChaCha20-Poly1305 AEAD — packet §6.1–§6.4, §6.6, §6.10.
//
// Implementation: Monocypher 4.0.2 (third_party/monocypher, VENDOR.md). The
// cipher is not implemented here (implementation plan Phase 4).
//
// PINNED (Phase 4, cipher suite 1):
//
//   cipher        ChaCha20-Poly1305, RFC 8439, 32-byte key, 12-byte nonce
//                 (crypto/nonce.h), block counter 1 for the ciphertext
//   sealed        ciphertext ‖ tag, ciphertext the same length as the input
//   tag           the FIRST kAeadTagBytes (4) bytes of the 16-byte Poly1305
//                 tag (packet §6.4 "Truncate to 4 bytes")
//   associated    none for native payloads — the packet layer passes no AD.
//   data          The AD parameters exist so the RFC test vector, which has
//                 AD, can run through this exact code path.
//
// Order is binding: compress → encrypt (packet §6.1). The input is the whole
// assembled plaintext payload, metadata included (§6.2).
//
// Opening. Monocypher's crypto_aead_read checks a full 16-byte tag, which a
// 4-byte truncated tag cannot supply. aead_open therefore uses only
// crypto_aead_write — the same audited function aead_seal uses:
//   1  crypto_aead_write over the received ciphertext → candidate plaintext
//      (ChaCha20 is an XOR stream; the tag computed here is discarded)
//   2  crypto_aead_write over the candidate           → the tag over the
//      received ciphertext
//   3  compare the first 4 tag bytes in constant time
// On mismatch the candidate is wiped and AuthenticationFailed returned: no
// plaintext is ever released from a packet that fails authentication
// (packet §6.3, receiver §3②, contract C-41).
//
// ITANTRA_DISABLE_AEAD (packet §6.10.2) — debug builds only, a diagnostic:
// seal and open copy the plaintext payload through unchanged, with no tag, so
// sealed output is byte-identical to the frozen golden vectors and a C-01
// failure can be attributed to the payload rather than the crypto. Nothing is
// authenticated in this mode. A build with NDEBUG and the flag is a compile
// error (contract C-43).

#if defined(ITANTRA_DISABLE_AEAD) && defined(NDEBUG)
#error "ITANTRA_DISABLE_AEAD must not reach a release build (packet 6.10.2, contract C-43)"
#endif

#include "common/types.h"

namespace itantra {

constexpr u32 kAeadKeyBytes        = 32u;
constexpr u32 kAeadTagBytes        = 4u;
constexpr u32 kAeadMaxMessageBytes = 8192u;   // larger than any native payload

#ifdef ITANTRA_DISABLE_AEAD
constexpr bool kAeadBypassed = true;
#else
constexpr bool kAeadBypassed = false;
#endif

// Bytes aead_seal adds. The packet always carries kAeadTagBytes (§6.4); only
// the debug bypass differs, and packet-size decisions (tier selection, C-19)
// must use kAeadTagBytes, never this.
constexpr u32 kAeadOverheadBytes = kAeadBypassed ? 0u : kAeadTagBytes;

enum class AeadStatus : u8 {
    Ok,
    AuthenticationFailed,   // tag mismatch, or too short to hold a tag
    InvalidArgument,        // null pointer, message above kAeadMaxMessageBytes,
                            // or output capacity too small
};

// out receives length + kAeadOverheadBytes bytes. `out` may be the same
// pointer as `plaintext`; other overlap is not allowed.
AeadStatus aead_seal(const u8* key, const u8* nonce, const u8* ad, u32 ad_length,
                     const u8* plaintext, u32 length, u8* out, u32 capacity,
                     u32& out_length) noexcept;

// plaintext receives sealed_length − kAeadOverheadBytes bytes, only on Ok. On
// AuthenticationFailed those bytes are zeroed and plaintext_length is 0.
AeadStatus aead_open(const u8* key, const u8* nonce, const u8* ad, u32 ad_length,
                     const u8* sealed, u32 sealed_length, u8* plaintext, u32 capacity,
                     u32& plaintext_length) noexcept;

// Overwrites memory in a way the compiler may not remove (Monocypher
// crypto_wipe). For plaintext payloads and key material.
void secure_wipe(u8* data, u32 length) noexcept;

}  // namespace itantra
