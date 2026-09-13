// Unit tests — common/bitio. Implementation plan Phase 1.
//
//   write/read every width 1…32, round-trip
//   MSB-first order verified against hand-computed bytes
//   padding to byte boundary
//   read past end returns zero, does not crash

#include "common/bitio.h"
#include "itest.h"

using namespace itantra;

namespace {

// Deterministic pseudo-random source. Not <random>: its distributions are
// implementation-defined, and these inputs must not vary between toolchains.
struct XorShift32 {
    u32 s;
    u32 next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

u32 mask(u32 width) {
    return width == 32u ? 0xFFFFFFFFu : ((1u << width) - 1u);
}

}  // namespace

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

ITEST(every_width_1_to_32_round_trips_boundary_values) {
    for (u32 w = 1u; w <= 32u; ++w) {
        const u32 values[] = {
            0u, 1u, mask(w), mask(w) & 0xAAAAAAAAu, mask(w) & 0x55555555u, mask(w) >> 1,
        };
        u8 buf[32] = {};
        BitWriter bw(buf, 32u);
        for (u32 v : values) bw.write(v, static_cast<u8>(w));
        ITEST_TRUE(bw.ok());
        ITEST_EQ(bw.bit_length(), 6u * w);

        BitReader br(buf, bw.byte_length());
        for (u32 v : values) ITEST_EQ(br.read(static_cast<u8>(w)), v);
        ITEST_TRUE(!br.overran());
    }
}

ITEST(every_width_1_to_32_round_trips_after_every_bit_offset) {
    // The same field must survive starting at any of the 8 offsets in a byte.
    for (u32 lead = 0u; lead < 8u; ++lead) {
        for (u32 w = 1u; w <= 32u; ++w) {
            const u32 v = 0xDEADBEEFu & mask(w);
            u8 buf[8] = {};
            BitWriter bw(buf, 8u);
            if (lead > 0u) bw.write(mask(lead), static_cast<u8>(lead));
            bw.write(v, static_cast<u8>(w));
            bw.write(1u, 1);
            ITEST_TRUE(bw.ok());

            BitReader br(buf, bw.byte_length());
            if (lead > 0u) ITEST_EQ(br.read(static_cast<u8>(lead)), mask(lead));
            ITEST_EQ(br.read(static_cast<u8>(w)), v);
            ITEST_EQ(br.read(1), 1u);
        }
    }
}

ITEST(mixed_width_random_sequences_round_trip) {
    XorShift32 rng{0x01234567u};
    for (u32 round = 0u; round < 1000u; ++round) {
        u8  buf[256] = {};
        u8  widths[64];
        u32 values[64];

        BitWriter bw(buf, 256u);   // 64 fields x at most 32 bits = 256 bytes
        for (u32 i = 0u; i < 64u; ++i) {
            widths[i] = static_cast<u8>(1u + rng.next() % 32u);
            values[i] = rng.next() & mask(widths[i]);
            bw.write(values[i], widths[i]);
        }
        ITEST_TRUE(bw.ok());

        BitReader br(buf, bw.byte_length());
        for (u32 i = 0u; i < 64u; ++i) ITEST_EQ(br.read(widths[i]), values[i]);
        ITEST_TRUE(!br.overran());
        ITEST_EQ(br.bit_position(), bw.bit_length());
    }
}

// ---------------------------------------------------------------------------
// MSB-first, against bytes worked out by hand
// ---------------------------------------------------------------------------

ITEST(msb_first_matches_hand_computed_bytes_mixed_fields) {
    // Widths 2,5,8,1,1 — the same widths as the packet §3.1 common prefix,
    // used here only as a familiar example; this is not the packet layer.
    //
    //   01 | 00101 | 10100101 | 1 | 0
    //   0100 1011   0100 1011   0 000 0000
    //     = 0x4B      = 0x4B      = 0x00
    u8 buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    BitWriter bw(buf, 4u);
    bw.write(0x1u, 2);
    bw.write(0x5u, 5);
    bw.write(0xA5u, 8);
    bw.write(0x1u, 1);
    bw.write(0x0u, 1);

    ITEST_TRUE(bw.ok());
    ITEST_EQ(bw.bit_length(), 17u);
    ITEST_EQ(bw.byte_length(), 3u);
    ITEST_EQ(buf[0], 0x4Bu);
    ITEST_EQ(buf[1], 0x4Bu);
    ITEST_EQ(buf[2], 0x00u);
    ITEST_EQ(buf[3], 0xFFu);   // never entered
}

ITEST(msb_first_matches_hand_computed_bytes_field_crossing_a_byte) {
    //   101 | 0 1010 1011 1100      (3 bits, then 0x0ABC in 13 bits)
    //   1010 1010   1011 1100
    //     = 0xAA      = 0xBC
    u8 buf[2] = {};
    BitWriter bw(buf, 2u);
    bw.write(0x5u, 3);
    bw.write(0x0ABCu, 13);
    ITEST_TRUE(bw.ok());
    ITEST_EQ(buf[0], 0xAAu);
    ITEST_EQ(buf[1], 0xBCu);
}

ITEST(msb_first_matches_hand_computed_bytes_32_bit_field) {
    // Big-endian on the wire regardless of host byte order (packet §8.1).
    u8 buf[4] = {};
    BitWriter bw(buf, 4u);
    bw.write(0x12345678u, 32);
    ITEST_EQ(buf[0], 0x12u);
    ITEST_EQ(buf[1], 0x34u);
    ITEST_EQ(buf[2], 0x56u);
    ITEST_EQ(buf[3], 0x78u);
}

ITEST(msb_first_matches_hand_computed_bytes_single_bits) {
    //   1 0 0 0 0 0 0 1  = 0x81
    u8 buf[1] = {};
    BitWriter bw(buf, 1u);
    const u32 bits[] = {1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
    for (u32 b : bits) bw.write(b, 1);
    ITEST_EQ(buf[0], 0x81u);
}

ITEST(reader_is_msb_first_against_hand_computed_bytes) {
    //   0xAC = 1 | 010 | 1100
    const u8 buf[1] = {0xAC};
    BitReader br(buf, 1u);
    ITEST_EQ(br.read(1), 0x1u);
    ITEST_EQ(br.read(3), 0x2u);
    ITEST_EQ(br.read(4), 0xCu);
    ITEST_TRUE(!br.overran());
}

// ---------------------------------------------------------------------------
// Padding and alignment
// ---------------------------------------------------------------------------

ITEST(pad_to_byte_fills_with_zeros_on_a_dirty_buffer) {
    u8 buf[2] = {0xFF, 0xFF};
    BitWriter bw(buf, 2u);
    bw.write(0x1u, 1);
    bw.pad_to_byte();
    ITEST_EQ(bw.bit_length(), 8u);
    ITEST_EQ(bw.byte_length(), 1u);
    ITEST_EQ(buf[0], 0x80u);
    ITEST_EQ(buf[1], 0xFFu);
}

ITEST(pad_to_byte_is_a_no_op_on_a_boundary) {
    u8 buf[2] = {};
    BitWriter bw(buf, 2u);
    bw.pad_to_byte();
    ITEST_EQ(bw.bit_length(), 0u);
    bw.write(0xFFu, 8);
    bw.pad_to_byte();
    ITEST_EQ(bw.bit_length(), 8u);
    bw.write(0x7u, 3);
    ITEST_EQ(buf[1], 0xE0u);
    ITEST_EQ(bw.byte_length(), 2u);
}

ITEST(unpadded_tail_bits_are_zero_on_a_dirty_buffer) {
    // Padding must be deterministic even if the caller forgets to pad.
    u8 buf[1] = {0xFF};
    BitWriter bw(buf, 1u);
    bw.write(0x5u, 3);
    ITEST_EQ(bw.byte_length(), 1u);
    ITEST_EQ(buf[0], 0xA0u);
}

ITEST(reader_align_to_byte) {
    const u8 buf[2] = {0xFF, 0x5A};
    BitReader br(buf, 2u);
    ITEST_EQ(br.read(3), 0x7u);
    br.align_to_byte();
    ITEST_EQ(br.bit_position(), 8u);
    br.align_to_byte();
    ITEST_EQ(br.bit_position(), 8u);
    ITEST_EQ(br.read(8), 0x5Au);
    ITEST_TRUE(!br.overran());
}

// ---------------------------------------------------------------------------
// Reading past the end, and writing past capacity
// ---------------------------------------------------------------------------

ITEST(read_past_end_returns_zero_and_flags_overrun) {
    const u8 buf[1] = {0xFF};
    BitReader br(buf, 1u);
    ITEST_EQ(br.read(8), 0xFFu);
    ITEST_TRUE(!br.overran());
    ITEST_EQ(br.read(8), 0u);
    ITEST_TRUE(br.overran());
    ITEST_EQ(br.read(32), 0u);
    ITEST_EQ(br.bit_position(), 48u);
}

ITEST(read_straddling_the_end_keeps_real_bits_then_zero_fills) {
    const u8 buf[1] = {0xFF};
    BitReader br(buf, 1u);
    ITEST_EQ(br.read(4), 0xFu);
    ITEST_EQ(br.read(8), 0xF0u);
    ITEST_TRUE(br.overran());
}

ITEST(read_32_bits_from_one_byte) {
    const u8 buf[1] = {0xFF};
    BitReader br(buf, 1u);
    ITEST_EQ(br.read(32), 0xFF000000u);
    ITEST_TRUE(br.overran());
}

ITEST(empty_and_null_buffers_do_not_crash) {
    BitReader empty(nullptr, 0u);
    ITEST_EQ(empty.read(32), 0u);
    ITEST_TRUE(empty.overran());

    BitReader null_with_length(nullptr, 16u);   // treated as empty
    ITEST_EQ(null_with_length.read(8), 0u);
    ITEST_TRUE(null_with_length.overran());

    BitWriter null_writer(nullptr, 16u);        // treated as zero capacity
    null_writer.write(0x1u, 1);
    ITEST_TRUE(!null_writer.ok());
    ITEST_EQ(null_writer.byte_length(), 0u);
}

ITEST(writer_overflow_never_touches_memory_past_capacity) {
    u8 buf[3] = {0x00, 0x00, 0xEE};   // buf[2] is a guard outside capacity
    BitWriter bw(buf, 2u);
    bw.write(0xFFFFu, 16);
    ITEST_TRUE(bw.ok());
    bw.write(0x1u, 1);
    ITEST_TRUE(!bw.ok());
    ITEST_EQ(bw.bit_length(), 16u);
    ITEST_EQ(buf[2], 0xEEu);
}

ITEST(writer_overflow_is_all_or_nothing_and_latches) {
    u8 buf[2] = {0xFF, 0xFF};
    BitWriter bw(buf, 1u);
    bw.write(0x0u, 5);
    bw.write(0xFu, 4);   // needs 4 bits, 3 remain: nothing written
    ITEST_TRUE(!bw.ok());
    ITEST_EQ(bw.bit_length(), 5u);

    bw.write(0x1u, 1);   // would fit, but the stream has already failed
    ITEST_EQ(bw.bit_length(), 5u);
    bw.pad_to_byte();
    ITEST_EQ(bw.bit_length(), 5u);
    ITEST_EQ(buf[0], 0x00u);
    ITEST_EQ(buf[1], 0xFFu);
}

ITEST_MAIN("unit.bitio")
