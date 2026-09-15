#pragma once

// Replay window — packet §6.8, receiver §3⑥.
//
// "AEAD stops forgery but not replay. seq is already tracked, so add a sliding
// window and reject anything already seen."
//
// Operates on the WIDE counter (after seq reconstruction, packet/seq.h), per
// direction, per session.
//
//   largest    the largest counter accepted so far; 0 before the first message
//              (counters start at 1, crypto/nonce.h)
//   window     kReplayWindowCounters counters: largest, largest − 1, …,
//              largest − 127. Covers every counter packet/seq.h can
//              reconstruct behind the expectation (127).
//
//   check(c)   Invalid   c = 0 or c > kMaxCounter
//              Fresh     c > largest, or c inside the window and not yet seen
//              Replayed  c inside the window and already seen
//              TooOld    c < largest − 127: cannot be told apart from a replay
//
// check() never changes state. accept() records a counter and must be called
// ONLY after the packet has authenticated (receiver §2 ②): marking before
// authentication would let a forged packet block the genuine one.
//
// Local receiver state, not a wire or pairing contract.

#include "common/types.h"
#include "crypto/nonce.h"
#include "packet/seq.h"

namespace itantra {

constexpr u32 kReplayWindowCounters = 128u;

enum class ReplayVerdict : u8 {
    Fresh,
    Replayed,
    TooOld,
    Invalid,
};

class ReplayWindow {
public:
    ReplayVerdict check(SeqCounter counter) const noexcept;

    // Records `counter` if check() says Fresh. Returns false, changing
    // nothing, otherwise.
    bool accept(SeqCounter counter) noexcept;

    SeqCounter largest_accepted() const noexcept { return largest_; }

private:
    bool seen(u64 age) const noexcept;
    void mark(u64 age) noexcept;
    void advance(u64 distance) noexcept;

    SeqCounter largest_ = 0u;
    u64        bits_[2] = {0u, 0u};   // bit a of the 128: counter largest_ − a seen
};

}  // namespace itantra
