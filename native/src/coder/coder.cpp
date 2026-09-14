#include "coder/coder.h"

#include <cassert>

namespace itantra {

namespace {

constexpr u64 kTop           = (u64{1} << kCoderPrecisionBits) - 1u;
constexpr u64 kHalf          = u64{1} << (kCoderPrecisionBits - 1u);
constexpr u64 kQuarter       = u64{1} << (kCoderPrecisionBits - 2u);
constexpr u64 kThreeQuarters = kHalf + kQuarter;
constexpr u32 kFollowMax     = 0xFFFFFFFFu;

// The interval being narrowed is always wider than QUARTER, so a total no
// larger than QUARTER gives every p > 0 symbol a non-empty integer interval.
static_assert(kModelMaxTotal <= kQuarter, "model total exceeds coder resolution");
// range (<= 2^P) times a cumulative frequency (<= kModelMaxTotal) fits in u64.
static_assert(u64{kModelMaxTotal} <= (u64{1} << (64u - kCoderPrecisionBits)),
              "range * total overflows 64 bits");

// M1 and M2 for a symbol that is about to be coded, plus p > 0.
bool codable(const SymbolRange& r) noexcept {
    return r.total >= 1u && r.total <= kModelMaxTotal && r.low < r.high && r.high <= r.total;
}

}  // namespace

// ---------------------------------------------------------------------------
// ArithmeticEncoder
// ---------------------------------------------------------------------------

ArithmeticEncoder::ArithmeticEncoder(BitWriter& out) noexcept
    : out_(&out),
      low_(0u),
      high_(kTop),
      follow_(0u),
      start_bits_(out.bit_length()),
      ok_(out.ok()),
      finished_(false) {}

void ArithmeticEncoder::emit(u32 bit) noexcept {
    out_->write(bit, 1);
    const u32 inverse = bit ^ 1u;
    while (follow_ > 0u && out_->ok()) {
        out_->write(inverse, 1);
        --follow_;
    }
    follow_ = 0u;
}

bool ArithmeticEncoder::encode(const Model& model, u32 symbol) noexcept {
    return encode_range(model.range_of(symbol));
}

bool ArithmeticEncoder::encode_range(const SymbolRange& s) noexcept {
    assert(!finished_ && "encode after finish");
    assert(codable(s) && "zero-probability or malformed symbol range");
    if (!ok_ || finished_ || !codable(s)) {
        ok_ = false;
        return false;
    }

    const u64 range = high_ - low_ + 1u;
    high_ = low_ + (range * s.high) / s.total - 1u;
    low_  = low_ + (range * s.low) / s.total;

    for (;;) {
        if (high_ < kHalf) {
            emit(0u);
        } else if (low_ >= kHalf) {
            emit(1u);
            low_  -= kHalf;
            high_ -= kHalf;
        } else if (low_ >= kQuarter && high_ < kThreeQuarters) {
            if (follow_ == kFollowMax) {
                ok_ = false;
                return false;
            }
            ++follow_;
            low_  -= kQuarter;
            high_ -= kQuarter;
        } else {
            break;
        }
        low_  = low_ << 1u;
        high_ = (high_ << 1u) | 1u;
    }

    ok_ = out_->ok();
    return ok_;
}

bool ArithmeticEncoder::finish() noexcept {
    assert(!finished_ && "finish called twice");
    if (!ok_ || finished_ || follow_ == kFollowMax) {
        ok_ = false;
        return false;
    }
    finished_ = true;

    ++follow_;
    emit(low_ < kQuarter ? 0u : 1u);

    ok_ = out_->ok();
    return ok_;
}

u32 ArithmeticEncoder::committed_bits() const noexcept {
    return (out_->bit_length() - start_bits_) + follow_;
}

// ---------------------------------------------------------------------------
// ArithmeticDecoder
// ---------------------------------------------------------------------------

ArithmeticDecoder::ArithmeticDecoder(BitReader& in) noexcept
    : in_(&in),
      low_(0u),
      high_(kTop),
      value_(in.read(static_cast<u8>(kCoderPrecisionBits))),
      ok_(true) {}

bool ArithmeticDecoder::decode(const Model& model, u32& symbol) noexcept {
    symbol = 0u;

    const u32  total    = model.total();
    const bool total_ok = total >= 1u && total <= kModelMaxTotal;
    assert(total_ok && "model total out of range");
    if (!ok_ || !total_ok) {
        ok_ = false;
        return false;
    }

    // Holds for every input bit string: the chosen symbol's interval always
    // contains value, and renormalisation preserves that.
    assert(low_ <= value_ && value_ <= high_);

    const u64 range  = high_ - low_ + 1u;
    const u32 target = static_cast<u32>(((value_ - low_ + 1u) * total - 1u) / range);

    SymbolRange s{0u, 0u, 0u};
    const u32  found    = model.find(target, s);
    const bool found_ok = s.total == total && codable(s) && s.low <= target && target < s.high;
    assert(found_ok && "model find broke its contract");
    if (!found_ok) {
        ok_ = false;
        return false;
    }

    high_ = low_ + (range * s.high) / total - 1u;
    low_  = low_ + (range * s.low) / total;

    for (;;) {
        if (high_ < kHalf) {
            // Top bit 0 in low, high and value alike: nothing to subtract.
        } else if (low_ >= kHalf) {
            low_   -= kHalf;
            high_  -= kHalf;
            value_ -= kHalf;
        } else if (low_ >= kQuarter && high_ < kThreeQuarters) {
            low_   -= kQuarter;
            high_  -= kQuarter;
            value_ -= kQuarter;
        } else {
            break;
        }
        low_   = low_ << 1u;
        high_  = (high_ << 1u) | 1u;
        value_ = (value_ << 1u) | in_->read(1);
    }

    symbol = found;
    return true;
}

}  // namespace itantra
