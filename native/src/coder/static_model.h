#pragma once

// Static frequency-table model.
//
// tier §5.9  Tier 1 codes with STATIC tables, no context boost
// tier §6.6  Tier 2 resets to its static table at every clause boundary
// tier §3.2  frequencies and cumulative totals are integers
//
// Immutable once assigned (model.h M5). Where the frequencies come from —
// build-time tables, a language pack, a boosted copy — is the caller's
// concern; this class only turns a frequency table into coder intervals.

#include <vector>

#include "coder/model.h"
#include "common/types.h"

namespace itantra {

class StaticModel final : public Model {
public:
    // Empty: total() == 0, which the coder rejects. Usable once assign()
    // has succeeded.
    StaticModel() = default;

    // Builds the model from `count` frequencies; symbol i has frequency
    // frequencies[i]. Zero frequencies are allowed (p == 0 symbols).
    //
    // Returns false, leaving the model empty, if frequencies is null, count is
    // 0 or above kModelMaxTotal, or the frequencies sum to 0 or to more than
    // kModelMaxTotal.
    bool assign(const u32* frequencies, u32 count);

    bool valid()         const noexcept { return !cum_.empty(); }
    u32  alphabet_size() const noexcept;
    u32  frequency(u32 symbol) const noexcept;

    u32         total() const noexcept override;
    SymbolRange range_of(u32 symbol) const noexcept override;
    u32         find(u32 target, SymbolRange& range) const noexcept override;

private:
    // cum_[i] = sum of frequencies[0 .. i-1]; size alphabet_size() + 1, or 0
    // when empty.
    std::vector<u32> cum_;
};

}  // namespace itantra
