#pragma once

// Fixed-width aliases and the slot enumeration.
//
// packet §8.1: "Fixed-width types everywhere. uint16_t, never int." Anything
// that reaches the wire, the context hash or a probability model uses these.

#include <cstdint>

namespace itantra {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// context Appendix B, verbatim.
//
// Slot indices are a wire contract — LAST_REF holds one, and the slot presence
// mask is indexed by them. Never reorder, never renumber.
//
// NEGATION is deliberately absent: it is carried explicitly in every message
// and is never context (context §2.3).
enum SlotId : u8 {
    SLOT_ACTOR    = 0,
    SLOT_OBJECT   = 1,
    SLOT_LOCATION = 2,
    SLOT_SEVERITY = 3,
    SLOT_QUANTITY = 4,
    SLOT_TIME     = 5,
    SLOT_STATE    = 6,
    SLOT_LAST_REF = 7,     // holds a SlotId, not a value (context §2.4).
                           // Null-index representation DEFERRED to Phase 5.
    SLOT_COUNT    = 8
};

// SLOT_COUNT as an unsigned loop bound, so comparisons stay unsigned/unsigned.
constexpr u32 kSlotCount = SLOT_COUNT;

static_assert(sizeof(SlotId) == 1, "SlotId is a uint8_t wire value");
static_assert(SLOT_ACTOR == 0 && SLOT_OBJECT == 1 && SLOT_LOCATION == 2 &&
              SLOT_SEVERITY == 3 && SLOT_QUANTITY == 4 && SLOT_TIME == 5 &&
              SLOT_STATE == 6 && SLOT_LAST_REF == 7 && SLOT_COUNT == 8,
              "slot indices are a wire contract (context Appendix B)");

}  // namespace itantra
