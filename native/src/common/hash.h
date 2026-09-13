#pragma once

// Context hash — context-manager-spec §5.2.
//
// Fixed by the spec:
//   - computed over all eight (current, ver) pairs
//   - recent[], age, seq and context_id are excluded
//   - covers context state BEFORE the current message is applied; that timing
//     is the caller's obligation (Phase 5, context/)
//   - integer arithmetic only
//
// Not fixed by the spec body, and pinned here. A Phase 1 decision, recorded in
// the context-manager-spec change log ("Implementation resolutions — v1.2"):
//
//   Function  CRC-16/CCITT-FALSE
//               poly 0x1021, init 0xFFFF, no input/output reflection,
//               xorout 0x0000
//               check value over ASCII "123456789" = 0x29B1
//
//   Input     24 bytes. For each slot in SlotId order (SLOT_ACTOR first):
//               current >> 8,  current & 0xFF,  ver
//
//   Output    16 bits (context §5, Context::hash is uint16_t)
//
// Why a CRC: it detects every error burst no longer than its degree, so a
// change confined to one slot's `current` (16 bits) or one slot's `ver`
// (8 bits) always changes the hash. A change spanning both fields of a pair is
// not guaranteed to be detected; that residual is the collision rate §5.2
// already accepts.
//
// DEFERRED to Phase 3, deliberately not decided here: how this 16-bit value
// maps onto the 12-bit wire field (packet §3.3).
//
// Everything above is a pairing contract (context §20 D5, packet §8). Changing
// any of it makes two phones disagree on every hash.

#include "common/types.h"

namespace itantra {

constexpr u16 kCrc16CcittFalsePoly = 0x1021u;
constexpr u16 kCrc16CcittFalseInit = 0xFFFFu;

constexpr u32 kContextHashInputBytes = 3u * kSlotCount;

// Exactly the fields §5.2 covers, and nothing else.
struct ContextHashInput {
    u16 current[SLOT_COUNT];
    u8  ver[SLOT_COUNT];
};

// Precondition, asserted: data != nullptr unless length == 0.
u16 crc16_ccitt_false(const u8* data, u32 length) noexcept;

u16 context_hash(const ContextHashInput& in) noexcept;

}  // namespace itantra
