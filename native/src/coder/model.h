#pragma once

// Probability model interface for the arithmetic coder.
//
// tier §3.1  one coder, two symbol sources — Tier 1 (intent, slot modes, slot
//            values) and Tier 2 (subwords). Only the meaning of the symbols
//            differs, so the coder sees nothing but cumulative frequencies.
// tier §3.2  integer only. A probability is a frequency over a total, both
//            unsigned integers; there is no other representation.
//
// A model assigns each symbol of its alphabet a half-open cumulative interval
// [low, high) of total(). The symbol's probability is (high - low) / total().
//
// Every Model must satisfy, for as long as a coder is using it:
//
//   M1  1 <= total() <= kModelMaxTotal
//   M2  range_of(s) returns {low, high, total()} with low <= high <= total(),
//       for every s. high == low means p == 0 — including any s outside the
//       alphabet.
//   M3  the intervals of distinct symbols do not overlap, and together they
//       tile [0, total()).
//   M4  find(t, r), for every t < total(), returns the unique symbol s with
//       r = range_of(s) and r.low <= t < r.high.
//   M5  nothing above changes while the coder uses the model. A model that
//       adapts mid-message diverges the two phones' tables on one lost packet
//       (tier §6.6); a context boost (tier §6.4) is applied by building the
//       model, before coding starts.
//
// A zero-probability symbol is legal IN a model — a static table can have
// unused IDs. ENCODING one is a programming error: it has no finite code
// (tier §6.3), and the coder asserts rather than emit garbage. Guaranteeing
// that every Tier 2 token has p > 0 is the Tier 2 model's job (Phase 7,
// tier §6.3 "non-zero floor"), not the coder's.

#include "common/types.h"

namespace itantra {

// Largest total() the coder accepts. NOT a wire contract: the bytes depend
// only on each symbol's (low, high, total), so raising this later changes no
// existing payload. It is bounded by the coder's precision (coder.h):
//
//   - the interval being narrowed is always wider than 2^30, so a total up to
//     2^30 still gives every p > 0 symbol at least one integer code value;
//   - range * total must fit in 64 bits: 2^32 * 2^24 = 2^56;
//   - integer truncation costs under 1.44 * total / 2^30 bits per symbol,
//     which at 2^24 is under 0.023 bits even for a p = 1/total symbol.
//
// 2^24 leaves a 32k-token Tier 2 vocabulary (tier §6.2) room for a uniform
// floor plus trained counts, and keeps the truncation loss negligible.
constexpr u32 kModelMaxTotal = u32{1} << 24;

// Cumulative frequency interval [low, high) out of total.
struct SymbolRange {
    u32 low;
    u32 high;
    u32 total;
};

class Model {
public:
    virtual ~Model() = default;

    virtual u32 total() const noexcept = 0;

    virtual SymbolRange range_of(u32 symbol) const noexcept = 0;

    // Precondition: target < total(). Writes the symbol's interval to `range`
    // and returns the symbol.
    virtual u32 find(u32 target, SymbolRange& range) const noexcept = 0;
};

}  // namespace itantra
