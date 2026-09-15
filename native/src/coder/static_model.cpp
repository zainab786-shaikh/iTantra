#include "coder/static_model.h"

namespace itantra {

bool StaticModel::assign(const u32* frequencies, u32 count) {
    cum_.clear();
    if (frequencies == nullptr || count == 0u || count > kModelMaxTotal) return false;

    u64 sum = 0u;
    for (u32 i = 0u; i < count; ++i) {
        sum += frequencies[i];
        if (sum > kModelMaxTotal) return false;   // checked per step: cannot overflow u64
    }
    if (sum == 0u) return false;

    std::vector<u32> cum(static_cast<std::size_t>(count) + 1u);
    u32 acc = 0u;
    cum[0] = 0u;
    for (u32 i = 0u; i < count; ++i) {
        acc += frequencies[i];
        cum[static_cast<std::size_t>(i) + 1u] = acc;
    }
    cum_.swap(cum);
    return true;
}

u32 StaticModel::alphabet_size() const noexcept {
    return cum_.empty() ? 0u : static_cast<u32>(cum_.size() - 1u);
}

u32 StaticModel::frequency(u32 symbol) const noexcept {
    if (symbol >= alphabet_size()) return 0u;
    return cum_[static_cast<std::size_t>(symbol) + 1u] - cum_[symbol];
}

u32 StaticModel::total() const noexcept {
    return cum_.empty() ? 0u : cum_.back();
}

SymbolRange StaticModel::range_of(u32 symbol) const noexcept {
    const u32 t = total();
    if (symbol >= alphabet_size()) return SymbolRange{0u, 0u, t};
    return SymbolRange{cum_[symbol], cum_[static_cast<std::size_t>(symbol) + 1u], t};
}

u32 StaticModel::find(u32 target, SymbolRange& range) const noexcept {
    const u32 n = alphabet_size();
    const u32 t = total();
    if (n == 0u || target >= t) {
        range = SymbolRange{0u, 0u, t};
        return 0u;
    }

    // Largest s in [0, n) with cum_[s] <= target. Because cum_[n] = total >
    // target, cum_[s + 1] > target, so s is the one symbol whose interval
    // holds target — and it cannot be a zero-frequency symbol, whose
    // successor would have the same cumulative value.
    u32 lo = 0u;
    u32 hi = n;
    while (hi - lo > 1u) {
        const u32 mid = lo + (hi - lo) / 2u;
        if (cum_[mid] <= target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    range = SymbolRange{cum_[lo], cum_[static_cast<std::size_t>(lo) + 1u], t};
    return lo;
}

}  // namespace itantra
