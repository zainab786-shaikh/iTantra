#pragma once

// AEAD nonce derivation — packet §6.5, §3.6, §8.
//
// "The nonce must cost zero bytes. Derive it: nonce = f(session_id, direction,
// message_counter)." Nothing here is transmitted.
//
// PINNED (Phase 4, pairing contract — packet §8 "nonce derivation function",
// "counter width"):
//
//   nonce, 12 bytes (RFC 8439 96-bit nonce)
//     bytes 0..3    session_id, big-endian (crypto/kdf.h)
//     bytes 4..11   W, big-endian, where
//                   W = (direction << 63) | counter
//
//   direction   0 = initiator → responder, 1 = responder → initiator
//               (initiator / responder are the HELLO roles, context §18.1)
//   counter     the sender's wide local counter for that direction
//               (packet §3.6; low 8 bits ride as seq)
//
//   Valid counters: kFirstCounter (1) … kMaxCounter (2^63 − 1).
//
// Why nonces never repeat under one session key: (direction, counter) is
// unique per message — each direction has its own monotonic counter — and
// the map to W is injective on the valid range. A new session gets a new key
// and session_id from the KDF.
//
// Counter start, PINNED here: the first message in each direction carries
// counter 1; counter 0 is never sent. This matches context §5.3 (seq = 0
// before any message is applied) and receiver §3⑤ (expected = last seq + 1),
// so the first message does not read as a gap. A receiver's largest accepted
// counter therefore starts at 0 (crypto/replay.h).
//
// At kMaxCounter a sender must stop and re-key; derive_nonce refuses beyond it.

#include "common/types.h"
#include "packet/seq.h"

namespace itantra {

enum class Direction : u8 {
    InitiatorToResponder = 0,
    ResponderToInitiator = 1,
};

constexpr u32        kAeadNonceBytes = 12u;
constexpr SeqCounter kFirstCounter   = 1u;
constexpr SeqCounter kMaxCounter     = (SeqCounter{1} << 63) - 1u;

// Writes the 12-byte nonce. Returns false, writing nothing, if counter is 0,
// above kMaxCounter, or direction is not one of the two values.
bool derive_nonce(u32 session_id, Direction direction, SeqCounter counter, u8* nonce) noexcept;

}  // namespace itantra
