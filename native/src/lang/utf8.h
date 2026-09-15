#pragma once

// UTF-8 helpers for the language layer. Language-neutral.
//
// Input text is expected to be UTF-8 (STT output), but the layer must never
// fail or crash on anything else (contract C-20). A byte that does not start
// a valid, shortest-form UTF-8 sequence is decoded as one "invalid byte"
// unit and carried through unchanged, so nothing the speaker produced is
// silently altered or dropped.

#include <cstddef>
#include <string>

#include "common/types.h"

namespace itantra {
namespace utf8 {

// Decodes the codepoint starting at data[i]. Returns the number of bytes
// consumed (1–4). On an invalid or truncated sequence returns 1, sets valid to
// false and cp to the byte value.
inline u32 decode(const u8* data, std::size_t length, std::size_t i, u32& cp, bool& valid) noexcept {
    const u8 b0 = data[i];
    valid = true;
    if (b0 < 0x80u) {
        cp = b0;
        return 1u;
    }
    u32 need = 0u;
    u32 min  = 0u;
    u32 v    = 0u;
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        need = 1u; min = 0x80u; v = b0 & 0x1Fu;
    } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
        need = 2u; min = 0x800u; v = b0 & 0x0Fu;
    } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
        need = 3u; min = 0x10000u; v = b0 & 0x07u;
    } else {
        valid = false;
        cp = b0;
        return 1u;
    }
    if (length - i < static_cast<std::size_t>(need) + 1u) {
        valid = false;
        cp = b0;
        return 1u;
    }
    for (u32 k = 1u; k <= need; ++k) {
        const u8 b = data[i + k];
        if ((b & 0xC0u) != 0x80u) {
            valid = false;
            cp = b0;
            return 1u;
        }
        v = (v << 6) | (b & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) {
        valid = false;
        cp = b0;
        return 1u;
    }
    cp = v;
    return need + 1u;
}

inline void append(std::string& out, u32 cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// True if `b` can begin a codepoint (not a continuation byte).
inline bool starts_codepoint(u8 b) noexcept {
    return (b & 0xC0u) != 0x80u;
}

}  // namespace utf8
}  // namespace itantra
