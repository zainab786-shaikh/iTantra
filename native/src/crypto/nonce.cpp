#include "crypto/nonce.h"

namespace itantra {

bool derive_nonce(u32 session_id, Direction direction, SeqCounter counter, u8* nonce) noexcept {
    if (nonce == nullptr) return false;
    if (counter < kFirstCounter || counter > kMaxCounter) return false;
    if (direction != Direction::InitiatorToResponder && direction != Direction::ResponderToInitiator) {
        return false;
    }

    const u64 word = (u64{static_cast<u8>(direction)} << 63) | counter;
    for (u32 i = 0u; i < 4u; ++i) {
        nonce[i] = static_cast<u8>(session_id >> (24u - 8u * i));
    }
    for (u32 i = 0u; i < 8u; ++i) {
        nonce[4u + i] = static_cast<u8>(word >> (56u - 8u * i));
    }
    return true;
}

}  // namespace itantra
