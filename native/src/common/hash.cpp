#include "common/hash.h"

#include <cassert>

namespace itantra {

u16 crc16_ccitt_false(const u8* data, u32 length) noexcept {
    assert(data != nullptr || length == 0u);

    // Bitwise, no lookup table: the whole definition is visible here, which
    // is what a cross-device determinism review needs to read.
    u16 crc = kCrc16CcittFalseInit;
    for (u32 i = 0u; i < length; ++i) {
        crc = static_cast<u16>(crc ^ (static_cast<u16>(data[i]) << 8));
        for (u32 b = 0u; b < 8u; ++b) {
            crc = ((crc & 0x8000u) != 0u)
                      ? static_cast<u16>((crc << 1) ^ kCrc16CcittFalsePoly)
                      : static_cast<u16>(crc << 1);
        }
    }
    return crc;
}

u16 context_hash(const ContextHashInput& in) noexcept {
    // Explicit big-endian serialisation, per hash.h. Never memcpy the struct:
    // its layout would make the hash depend on the ABI (packet §8.1).
    u8 bytes[kContextHashInputBytes];
    for (u32 s = 0u; s < kSlotCount; ++s) {
        bytes[3u * s]      = static_cast<u8>(in.current[s] >> 8);
        bytes[3u * s + 1u] = static_cast<u8>(in.current[s] & 0xFFu);
        bytes[3u * s + 2u] = in.ver[s];
    }
    return crc16_ccitt_false(bytes, kContextHashInputBytes);
}

}  // namespace itantra
