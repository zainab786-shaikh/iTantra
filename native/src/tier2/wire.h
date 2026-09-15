#pragma once

// Big-endian reading and writing for the Tier 2 table files (tier2/subword.h,
// tier2/ngram.h, tier2/boost.h). Internal to tier2/. Never a memcpy'd struct:
// every field is read and written byte by byte, so the files are identical on
// every device (packet §8.1).

#include <cstddef>
#include <vector>

#include "common/types.h"

namespace itantra {
namespace tier2wire {

class Reader {
public:
    Reader(const u8* data, std::size_t length) noexcept : data_(data), length_(data == nullptr ? 0u : length) {}

    bool read8(u8& v) noexcept {
        if (length_ - at_ < 1u) return false;
        v = data_[at_];
        at_ += 1u;
        return true;
    }

    bool read16(u16& v) noexcept {
        if (length_ - at_ < 2u) return false;
        v = static_cast<u16>((u32{data_[at_]} << 8) | u32{data_[at_ + 1u]});
        at_ += 2u;
        return true;
    }

    bool read32(u32& v) noexcept {
        if (length_ - at_ < 4u) return false;
        v = (u32{data_[at_]} << 24) | (u32{data_[at_ + 1u]} << 16) | (u32{data_[at_ + 2u]} << 8) |
            u32{data_[at_ + 3u]};
        at_ += 4u;
        return true;
    }

    // Points `bytes` at the next `n` bytes and moves past them.
    bool take(std::size_t n, const u8*& bytes) noexcept {
        if (length_ - at_ < n) return false;
        bytes = data_ + at_;
        at_ += n;
        return true;
    }

    std::size_t remaining() const noexcept { return length_ - at_; }

private:
    const u8*   data_;
    std::size_t length_;
    std::size_t at_ = 0u;
};

inline void put8(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v));
}

inline void put16(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

inline void put32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 24));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

}  // namespace tier2wire
}  // namespace itantra
