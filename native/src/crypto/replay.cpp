#include "crypto/replay.h"

namespace itantra {

ReplayVerdict ReplayWindow::check(SeqCounter counter) const noexcept {
    if (counter < kFirstCounter || counter > kMaxCounter) return ReplayVerdict::Invalid;
    if (counter > largest_) return ReplayVerdict::Fresh;
    const u64 age = largest_ - counter;
    if (age >= kReplayWindowCounters) return ReplayVerdict::TooOld;
    return seen(age) ? ReplayVerdict::Replayed : ReplayVerdict::Fresh;
}

bool ReplayWindow::accept(SeqCounter counter) noexcept {
    if (check(counter) != ReplayVerdict::Fresh) return false;
    if (counter > largest_) {
        advance(counter - largest_);
        largest_ = counter;
        mark(0u);
    } else {
        mark(largest_ - counter);
    }
    return true;
}

bool ReplayWindow::seen(u64 age) const noexcept {
    return age < 64u ? ((bits_[0] >> age) & 1u) != 0u : ((bits_[1] >> (age - 64u)) & 1u) != 0u;
}

void ReplayWindow::mark(u64 age) noexcept {
    if (age < 64u) {
        bits_[0] |= u64{1} << age;
    } else {
        bits_[1] |= u64{1} << (age - 64u);
    }
}

void ReplayWindow::advance(u64 distance) noexcept {
    if (distance >= kReplayWindowCounters) {
        bits_[0] = 0u;
        bits_[1] = 0u;
    } else if (distance >= 64u) {
        bits_[1] = bits_[0] << (distance - 64u);
        bits_[0] = 0u;
    } else if (distance > 0u) {
        bits_[1] = (bits_[1] << distance) | (bits_[0] >> (64u - distance));
        bits_[0] = bits_[0] << distance;
    }
}

}  // namespace itantra
