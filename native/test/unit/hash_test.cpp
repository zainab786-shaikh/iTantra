// Unit tests — common/hash. Implementation plan Phase 1.
//
//   hash is stable across process restarts
//   hash changes when any (current, ver) pair changes
//   hash ignores recent[], age, seq, context_id

#include "common/hash.h"
#include "itest.h"

using namespace itantra;

namespace {

ContextHashInput zero_table() {
    return ContextHashInput{};
}

ContextHashInput sample_table() {
    // LAST_REF holds a slot index (context §2.4); 2 = SLOT_LOCATION.
    const u16 current[SLOT_COUNT] = {0x0001u, 0x0138u, 0x0000u, 0x0003u,
                                     0x0005u, 0xFFFFu, 0x002Fu, 0x0002u};
    const u8  ver[SLOT_COUNT]     = {1u, 2u, 0u, 3u, 4u, 255u, 7u, 1u};
    ContextHashInput in{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        in.current[s] = current[s];
        in.ver[s]     = ver[s];
    }
    return in;
}

// A mirror of context Appendix B's structures. The real Context arrives in
// Phase 5; this lets the exclusion rule be exercised now. Phase 5 repeats the
// test against the real type.
struct SpecSlot {
    u16 current;
    u16 recent[2];
    u8  ver;
    u8  age;
};

struct SpecContext {
    SpecSlot slots[SLOT_COUNT];
    u16      context_id;
    u16      hash;
    u8       seq;
};

ContextHashInput pairs_of(const SpecContext& ctx) {
    ContextHashInput in{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        in.current[s] = ctx.slots[s].current;
        in.ver[s]     = ctx.slots[s].ver;
    }
    return in;
}

}  // namespace

// ---------------------------------------------------------------------------
// The pinned function
// ---------------------------------------------------------------------------

ITEST(crc16_matches_published_check_value) {
    // CRC-16/CCITT-FALSE catalogue check value over ASCII "123456789".
    const u8 msg[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    ITEST_EQ(crc16_ccitt_false(msg, 9u), 0x29B1u);
}

ITEST(crc16_of_no_bytes_is_the_init_value) {
    ITEST_EQ(crc16_ccitt_false(nullptr, 0u), kCrc16CcittFalseInit);
}

ITEST(input_is_big_endian_triples_in_slot_order) {
    const ContextHashInput in = sample_table();
    const u8 expected_bytes[kContextHashInputBytes] = {
        0x00, 0x01, 1,     // ACTOR
        0x01, 0x38, 2,     // OBJECT
        0x00, 0x00, 0,     // LOCATION
        0x00, 0x03, 3,     // SEVERITY
        0x00, 0x05, 4,     // QUANTITY
        0xFF, 0xFF, 255,   // TIME
        0x00, 0x2F, 7,     // STATE
        0x00, 0x02, 1,     // LAST_REF
    };
    ITEST_EQ(context_hash(in), crc16_ccitt_false(expected_bytes, kContextHashInputBytes));
}

ITEST(hash_input_holds_only_the_pairs) {
    // No room for recent[], age, seq or context_id to leak in.
    ITEST_EQ(sizeof(ContextHashInput), 3u * kSlotCount);
    ITEST_EQ(kContextHashInputBytes, 24u);
}

// ---------------------------------------------------------------------------
// Stable across process restarts
// ---------------------------------------------------------------------------

ITEST(hash_is_stable_across_process_restarts) {
    // These constants were computed by a separate implementation of the pinned
    // definition, not by this code (that implementation reproduced the 0x29B1
    // check value first). A result that depended on anything but the input —
    // an address, the clock, uninitialised memory, iteration order — could not
    // equal a constant compiled into the test on every run.
    ITEST_EQ(context_hash(zero_table()),   0xEF8Au);
    ITEST_EQ(context_hash(sample_table()), 0x0FE1u);
}

ITEST(hash_is_repeatable_within_a_process) {
    const ContextHashInput in = sample_table();
    const u16 first = context_hash(in);
    for (u32 i = 0u; i < 1000u; ++i) ITEST_EQ(context_hash(in), first);
}

// ---------------------------------------------------------------------------
// Changes when any (current, ver) pair changes
// ---------------------------------------------------------------------------

ITEST(any_change_confined_to_one_current_changes_the_hash) {
    // Exhaustive: every slot, every other 16-bit value. Guaranteed by the CRC's
    // burst-detection property; checking one base table suffices, because
    // hash(a) XOR hash(b) depends only on a XOR b for equal-length inputs.
    const ContextHashInput base = sample_table();
    const u16 h0 = context_hash(base);
    for (u32 s = 0u; s < kSlotCount; ++s) {
        for (u32 v = 0u; v <= 0xFFFFu; ++v) {
            if (v == static_cast<u32>(base.current[s])) continue;
            ContextHashInput in = base;
            in.current[s] = static_cast<u16>(v);
            ITEST_TRUE(context_hash(in) != h0);
        }
    }
}

ITEST(any_change_confined_to_one_ver_changes_the_hash) {
    const ContextHashInput bases[] = {zero_table(), sample_table()};
    for (const ContextHashInput& base : bases) {
        const u16 h0 = context_hash(base);
        for (u32 s = 0u; s < kSlotCount; ++s) {
            for (u32 v = 0u; v <= 0xFFu; ++v) {
                if (v == static_cast<u32>(base.ver[s])) continue;
                ContextHashInput in = base;
                in.ver[s] = static_cast<u8>(v);
                ITEST_TRUE(context_hash(in) != h0);
            }
        }
    }
}

ITEST(every_single_bit_flip_in_any_pair_changes_the_hash) {
    const ContextHashInput bases[] = {zero_table(), sample_table()};
    for (const ContextHashInput& base : bases) {
        const u16 h0 = context_hash(base);
        for (u32 s = 0u; s < kSlotCount; ++s) {
            for (u32 bit = 0u; bit < 16u; ++bit) {
                ContextHashInput in = base;
                in.current[s] = static_cast<u16>(in.current[s] ^ (1u << bit));
                ITEST_TRUE(context_hash(in) != h0);
            }
            for (u32 bit = 0u; bit < 8u; ++bit) {
                ContextHashInput in = base;
                in.ver[s] = static_cast<u8>(in.ver[s] ^ (1u << bit));
                ITEST_TRUE(context_hash(in) != h0);
            }
        }
    }
}

ITEST(moving_a_value_to_another_slot_changes_the_hash) {
    // Slot order is part of the input, not just the multiset of values.
    ContextHashInput a{};
    ContextHashInput b{};
    a.current[SLOT_LOCATION] = 312u;
    b.current[SLOT_OBJECT]   = 312u;
    ITEST_TRUE(context_hash(a) != context_hash(b));
}

// ---------------------------------------------------------------------------
// Ignores recent[], age, seq, context_id
// ---------------------------------------------------------------------------

ITEST(hash_ignores_recent_age_seq_and_context_id) {
    const ContextHashInput pairs = sample_table();

    SpecContext a{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        a.slots[s].current = pairs.current[s];
        a.slots[s].ver     = pairs.ver[s];
    }

    SpecContext b = a;
    for (u32 s = 0u; s < kSlotCount; ++s) {
        b.slots[s].recent[0] = static_cast<u16>(0x1111u * (s + 1u));
        b.slots[s].recent[1] = static_cast<u16>(0xFFFFu - s);
        b.slots[s].age       = static_cast<u8>(200u + s);
    }
    b.context_id = 0xBEEFu;
    b.seq        = 0x7Fu;
    b.hash       = 0x1234u;

    ITEST_EQ(context_hash(pairs_of(a)), context_hash(pairs_of(b)));
    ITEST_EQ(context_hash(pairs_of(b)), 0x0FE1u);
}

ITEST_MAIN("unit.hash")
