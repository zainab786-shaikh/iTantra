#pragma once

// MSB-first bit writer and reader.
//
// packet §3    metadata is "bit-packed, MSB-first"
// packet §8    bit order within fields is part of the determinism surface
// packet §8.1  layouts are written field by field — never a memcpy'd struct,
//              never bitfields, byte order stated rather than assumed
//
// Bit numbering: bit 0 of a stream is the most significant bit of byte 0.
// A field of width w is written most significant bit first.

#include "common/types.h"

namespace itantra {

// Largest buffer either class addresses. Bit positions are u32, so this keeps
// capacity_bytes * 8 representable.
constexpr u32 kBitIoMaxBytes = 0x1FFFFFFFu;

class BitWriter {
public:
    // `buffer` need not be zeroed. Each byte is cleared as the writer enters
    // it, so the unused low bits of a partial final byte are always zero:
    // padding is deterministic whether or not pad_to_byte() is called
    // (packet §4.1, "Padding. Zeros only.").
    //
    // A null buffer is treated as zero capacity.
    BitWriter(u8* buffer, u32 capacity_bytes) noexcept;

    // Writes the low `width` bits of `value`, MSB first.
    //
    // Preconditions, asserted: 1 <= width <= 32, and value < 2^width.
    //
    // A write that does not fit, or has an invalid width, writes nothing and
    // latches ok() to false. Every later write is then ignored, so a failed
    // stream is never partially extended.
    void write(u32 value, u8 width) noexcept;

    // Advances to the next byte boundary; the skipped bits are zero.
    // No-op when already on a boundary or after a failure.
    void pad_to_byte() noexcept;

    u32  bit_length()  const noexcept { return pos_; }
    u32  byte_length() const noexcept { return (pos_ + 7u) >> 3; }
    bool ok()          const noexcept { return ok_; }

private:
    u8*  buf_;
    u32  cap_bits_;
    u32  pos_;
    bool ok_;
};

class BitReader {
public:
    // A null buffer is treated as zero length.
    BitReader(const u8* buffer, u32 length_bytes) noexcept;

    // Reads `width` bits, MSB first. Precondition, asserted: 1 <= width <= 32.
    //
    // Bits past the end of the buffer read as zero and set overran(). Memory
    // outside the buffer is never touched. The position still advances, so
    // bit_position() reports how many bits were consumed in total.
    u32 read(u8 width) noexcept;

    // Skips to the next byte boundary.
    void align_to_byte() noexcept;

    u32  bit_position() const noexcept { return pos_; }
    bool overran()      const noexcept { return overran_; }

private:
    const u8* buf_;
    u32       len_bits_;
    u32       pos_;
    bool      overran_;
};

}  // namespace itantra
