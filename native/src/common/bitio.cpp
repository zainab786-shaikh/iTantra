#include "common/bitio.h"

#include <cassert>

namespace itantra {

namespace {

constexpr u32 kPosMax = 0xFFFFFFFFu;

u32 capacity_bits(bool has_buffer, u32 bytes) noexcept {
    if (!has_buffer) return 0u;
    return (bytes > kBitIoMaxBytes ? kBitIoMaxBytes : bytes) << 3;
}

}  // namespace

// ---------------------------------------------------------------------------
// BitWriter
// ---------------------------------------------------------------------------

BitWriter::BitWriter(u8* buffer, u32 capacity_bytes) noexcept
    : buf_(buffer),
      cap_bits_(capacity_bits(buffer != nullptr, capacity_bytes)),
      pos_(0u),
      ok_(true) {}

void BitWriter::write(u32 value, u8 width) noexcept {
    const u32 w = width;
    assert(w >= 1u && w <= 32u);
    assert(w == 32u || (value >> w) == 0u);

    if (!ok_ || w < 1u || w > 32u || w > cap_bits_ - pos_) {
        ok_ = false;
        return;
    }

    for (u32 i = w; i > 0u; --i) {
        const u32 byte   = pos_ >> 3;
        const u32 offset = pos_ & 7u;
        if (offset == 0u) buf_[byte] = 0u;
        const u32 bit = (value >> (i - 1u)) & 1u;
        buf_[byte] = static_cast<u8>(buf_[byte] | (bit << (7u - offset)));
        ++pos_;
    }
}

void BitWriter::pad_to_byte() noexcept {
    if (!ok_) return;
    // A mid-byte position means that byte was entered, and so zeroed, by
    // write(). The skipped bits are already zero; only the position moves.
    // cap_bits_ is a multiple of 8, so this cannot pass it.
    pos_ = (pos_ + 7u) & ~7u;
}

// ---------------------------------------------------------------------------
// BitReader
// ---------------------------------------------------------------------------

BitReader::BitReader(const u8* buffer, u32 length_bytes) noexcept
    : buf_(buffer),
      len_bits_(capacity_bits(buffer != nullptr, length_bytes)),
      pos_(0u),
      overran_(false) {}

u32 BitReader::read(u8 width) noexcept {
    const u32 w = width;
    assert(w >= 1u && w <= 32u);
    if (w < 1u || w > 32u) return 0u;

    u32 value = 0u;
    for (u32 i = 0u; i < w; ++i) {
        u32 bit = 0u;
        if (pos_ < len_bits_) {
            bit = (static_cast<u32>(buf_[pos_ >> 3]) >> (7u - (pos_ & 7u))) & 1u;
        } else {
            overran_ = true;
        }
        value = (value << 1) | bit;
        if (pos_ != kPosMax) ++pos_;
    }
    return value;
}

void BitReader::align_to_byte() noexcept {
    pos_ = (pos_ > kPosMax - 7u) ? kPosMax : ((pos_ + 7u) & ~7u);
}

}  // namespace itantra
