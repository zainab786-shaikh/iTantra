#include "packet/seq.h"

namespace itantra {

namespace {

constexpr SeqCounter kWindow     = 256u;
constexpr SeqCounter kCounterMax = 0xFFFFFFFFFFFFFFFFull;

}  // namespace

u8 seq_to_wire(SeqCounter counter) noexcept {
    return static_cast<u8>(counter & 0xFFu);
}

SeqCounter seq_reconstruct(SeqCounter expected, u8 wire_seq) noexcept {
    const SeqCounter candidate = (expected & ~SeqCounter{0xFFu}) | wire_seq;

    if (candidate <= expected) {
        if (expected - candidate >= kSeqWindowHalf && candidate <= kCounterMax - kWindow) {
            return candidate + kWindow;
        }
    } else {
        if (candidate - expected > kSeqWindowHalf && candidate >= kWindow) {
            return candidate - kWindow;
        }
    }
    return candidate;
}

}  // namespace itantra
