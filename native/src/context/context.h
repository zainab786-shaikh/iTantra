#pragma once

// Context Manager — context-manager-spec.md v1.2.
//
// A small, deterministic memory held independently on both phones (§1.1).
// Both phones derive it ONLY from committed payloads (§13.4, D4), through the
// ONE commit() below (§10.2, D2, tier §12 T6, receiver §11 R3).
//
// This file is shared wire-contract logic: the context hash computed from it
// is compared across phones (§5.2), and LAST_REF / slot indices are part of
// the frame (Appendix B). Sender-only judgement — staleness (§13), pronoun
// resolution (§1.3), which slots to INHERIT — is NOT here.
//
// Resolved in Phase 5 (recorded in the spec's implementation resolutions):
//
//   LAST_REF    current = target slot index + 1; 0 = null (no reference).
//               Targets ACTOR … STATE, stored 1 … 7. See encode_last_ref().
//   empty slot  a first write (current == 0) sets current, ver++, age = 0 and
//               pushes nothing into recent[] (§4.2's "S.current != 0").
//   value 0     means empty (Appendix B) and is not writable.
//   age         saturates at 255; it never wraps back to "fresh".
//   non-writes  every slot the message does not write — INHERIT, REF,
//               LITERAL (never stored, tier §5.7), or absent — ages by one.
//   TIME        INHERIT and REF are refused (tier §5.6, register #9).
//   atomic      commit() validates the whole payload first; an invalid
//               payload changes nothing.

#include "common/types.h"

namespace itantra {

// context §4, Appendix B — field for field.
struct Slot {
    u16 current;     // the one inheritable value; codebook ID, 0 = empty
    u16 recent[2];   // FIFO of displaced values; never inherited (§4.2)
    u8  ver;         // increments on write to current; wraps at 256 (§4.4)
    u8  age;         // messages since current was written; NOT reset by INHERIT/REF (§4.3)
};

// context §5, Appendix B — field for field.
//
// NEGATION is deliberately absent (§2.3): it is explicit in every message and
// is never context.
struct Context {
    Slot slots[SLOT_COUNT];
    u16  context_id;   // metadata, not a slot (§2.3); commit() never changes it
    u16  hash;         // context_hash() of the current state = the PRE-message
                       // hash for the next message (§5.2)
    u8   seq;          // seq of the last message applied (§5.1)
};

constexpr u8 kAgeMax = 255u;

// ---------------------------------------------------------------------------
// LAST_REF — §2.4 "a slot index (0–7), not a copy of a value … Null index when
// no reference is active."
// ---------------------------------------------------------------------------
//
// Stored as index + 1 so that 0 — the value every slot starts with (§5.3) —
// means "no reference active". Storing the bare index would make the initial
// all-zero table claim a reference to ACTOR.
//
// A reference points at an entity slot: ACTOR (0) … STATE (6). LAST_REF
// cannot point at itself.

constexpr u16 kLastRefNull      = 0u;
constexpr u16 kLastRefMaxStored = static_cast<u16>(SLOT_STATE) + 1u;   // 7

// Precondition: target is ACTOR … STATE. Returns kLastRefNull otherwise.
constexpr u16 encode_last_ref(SlotId target) noexcept {
    return target < SLOT_LAST_REF ? static_cast<u16>(static_cast<u16>(target) + 1u) : kLastRefNull;
}

// True, with the target, for a stored reference; false for null or invalid.
constexpr bool decode_last_ref(u16 stored, SlotId& target) noexcept {
    if (stored == kLastRefNull || stored > kLastRefMaxStored) return false;
    target = static_cast<SlotId>(stored - 1u);
    return true;
}

// ---------------------------------------------------------------------------
// The payload commit() reads — §10 "commit what it actually encoded"
// ---------------------------------------------------------------------------
//
// The context view of one decoded (receiver) or encoded (sender) message: one
// operation per slot. It is built from the payload, never from what the
// sender extracted (§10, tier §11.2):
//
//   Tier 1 slot mode   ID → Write    LITERAL → Literal
//                      INHERIT → Inherit    REF → Ref    not present → Absent
//   Tier 2             concepts extracted from the transmitted text → Write
//                      (same-language sessions only; tier §11.3)
//
// Building it is the tiers' and the receiver's job (Phases 6–10).

enum class SlotOp : u8 {
    Absent  = 0,   // the message says nothing about this slot
    Write   = 1,   // explicitly sent value: current = value (§4.2)
    Literal = 2,   // text for a concept not in the codebook; never stored (tier §5.7)
    Inherit = 3,   // filled from context.current; NOT a write (§4.3)
    Ref     = 4,   // same as the previous message; NOT a write (§4.3)
};

struct SlotUpdate {
    SlotOp op;
    u16    value;   // Write only; must be 0 for every other op
};

struct CommitPayload {
    SlotUpdate slots[SLOT_COUNT];
    u8         seq;
};

enum class CommitResult : u8 {
    Ok,
    InvalidOp,         // op outside SlotOp
    EmptyWrite,        // Write of 0 (0 means empty)
    ValueOnNonWrite,   // a value on Absent / Literal / Inherit / Ref
    TimeInherited,     // Inherit or Ref on TIME
    InvalidLastRef,    // Write to LAST_REF outside 1 … kLastRefMaxStored
};

// ---------------------------------------------------------------------------
// Periodic refresh — §16.1 PERIODIC_EXPLICIT (the mode §16.2 selects for UDP),
// with §13.4's reset carried by the counter rather than a packet field.
// ---------------------------------------------------------------------------
//
// DECIDED in Phase 12. The hash covers every slot's (current, ver) (§5.2), and
// ver counts writes, so a fully explicit message alone cannot restore identity
// after a divergence: the unmentioned slots and every ver still differ. A
// refresh therefore resets. The message whose wide counter is a refresh point is
// encoded and committed, on BOTH phones, against the §5.3 initial state:
//
//   sender    context := initial; encode fully explicit (no INHERIT / REF, no
//             hash, Tier 2 unboosted); commit; send.
//   receiver  after authentication and the replay check, when the counter is a
//             refresh point AND newer than every counter accepted before it:
//             context := initial; then ⑦ … ⑪ as usual.
//
// The wide counter is authenticated (packet §6.5, receiver counter recovery), so
// nothing on the wire changes and a forged or replayed packet cannot trigger a
// reset. A late refresh (older than the newest accepted counter) resets nothing.
// A message older than the last applied reset belongs to the previous epoch and
// commits nothing (the sender's reset already discarded its update).
// Loss or reordering therefore diverges the contexts for at most the rest of
// the current interval.
constexpr u64 kContextRefreshInterval = 16u;

constexpr bool is_context_refresh(u64 counter) noexcept {
    return counter != 0u && counter % kContextRefreshInterval == 0u;
}

// §5.3 initial state: all slots zero, context_id 0, seq 0, hash of the all-zero
// slot table. Every field is written, whatever the memory held.
void init_context(Context& ctx) noexcept;

// THE commit — §10.2 "Both phones call the identical function". Sender on the
// payload it encoded, receiver on the payload it decoded. Applies `payload`
// to `ctx` in slot order and recomputes ctx.hash; on any result other than Ok
// nothing in `ctx` changes.
//
// When NOT to call it (authentication failure, negation disagreement, replay,
// hash mismatch, cross-language Tier 2) is the receiver's decision (receiver
// §8.1, §8.3), not this function's.
CommitResult commit(Context& ctx, const CommitPayload& payload) noexcept;

// §5.2 over the eight (current, ver) pairs — recent[], age, seq, context_id
// excluded. The pinned CRC-16/CCITT-FALSE of common/hash.h. Implemented in
// context/hash.cpp. For the 12-bit wire field apply wire_context_hash()
// (packet/metadata.h).
u16 context_hash(const Context& ctx) noexcept;

}  // namespace itantra
