#pragma once

// Tier 2 probability model — tier §6.3, §6.6, §12 T3b, T4.
//
// DECIDED in Phase 7 (tier §6.3 "order-2 or order-3 over subwords, Kneser-Ney
// or PPM escape backoff"):
//
//   Interpolated Kneser-Ney, n-gram order 2 (the distribution of a token given
//   the ONE token before it), static, integer frequencies, ending in an explicit
//   uniform floor over the whole vocabulary.
//
//   Kneser-Ney, not PPM. KN yields a complete distribution over the vocabulary
//   in every context, so each token is exactly one coded symbol and a Tier 2
//   payload's symbol_count is its token count (packet/assemble.h). PPM codes
//   escape symbols as well: symbol_count would exceed the token count, and the
//   decoder would need escape exclusion.
//
//   Order 2. Order 3 remains a measured alternative (tier §13.2); adopting it
//   is a new table version (kNgramTableVersion), not a packet format change.
//
// The distribution for the token at position i. Its history h is the previous
// token of the same clause, or BOS (= V) at position 0 — so nothing crosses a
// clause boundary (§6.6):
//
//   freq(s | h) = 1                                       uniform floor
//               + floor(lambda_h * uni[s] / 2^16)         backoff: KN continuation unigram
//               + w_h[s]                                  discounted bigram (sparse)
//               + boost[s]                                context boost, hash_present = 1
//                                                         only (tier2/boost.h)
//
// All unsigned integers; total(h) is the sum over the V tokens.
//
// NON-ZERO FLOOR (§6.3, "a hard requirement"): the first term alone gives every
// token a frequency of at least 1 in every context, whatever the trained
// numbers are. The backoff chain ends in a uniform floor by construction; it
// does not depend on the trainer having seen a token.
//
// BOUNDS, enforced when the tables load, so no model built at runtime can break
// model.h M1 (total <= kModelMaxTotal):
//
//   V <= 2^16                                                 (tier2/subword.h)
//   sum of uni[] == 2^16  → sum over s of floor(lambda * uni[s] / 2^16) <= lambda
//   lambda_h + sum of w_h[] <= 2^23, every row and the default (kContextMassLimit)
//   boost mass <= 2^23 − 2^16                                  (tier2/boost.h)
//   → total <= 2^16 + 2^23 + (2^23 − 2^16) = 2^24
//
// Training (estimate_kneser_ney, build time only): D = 3/4 absolute discount;
// continuation counts N(• s) = number of distinct histories seen before s;
// for each seen history h with c(h) tokens after it and n(h) distinct ones,
//
//   w_h[s]   = floor(2^23 · (4·c(h, s) − 3) / (4·c(h)))
//   lambda_h = floor(2^23 · 3·n(h) / (4·c(h)))
//   uni[s]   = N(• s) scaled to sum to exactly 2^16 (largest remainder, ties
//              by token ID); default lambda for an unseen history = 2^23.
//
// ngram.bin payload (inside the lang/pack.h container, kind Ngram), big-endian:
//
//   u32 table_version       = kNgramTableVersion
//   u32 order               = kNgramOrder
//   u32 vocab_size          V — must equal the vocabulary's
//   u32 default_lambda      for a history with no row
//   u32 × V                 uni[s], summing to exactly 2^16
//   u32 row_count
//   row × row_count         u32 history (0 … V; V = BOS), u32 lambda, u32 entry_count
//                           — histories strictly ascending
//   entry × (sum of counts) u32 token (< V), u32 weight (>= 1) — rows in order,
//                           tokens strictly ascending within a row

#include <cstddef>
#include <string>
#include <vector>

#include "coder/model.h"
#include "common/types.h"
#include "packet/assemble.h"

namespace itantra {

class ContextBoost;

constexpr u32 kNgramTableVersion   = 1u;
constexpr u32 kNgramOrder          = 2u;
constexpr u32 kUnigramTotalBits    = 16u;
constexpr u32 kUnigramTotal        = u32{1} << kUnigramTotalBits;
constexpr u32 kContextMassLimit    = u32{1} << 23;
constexpr u32 kKneserNeyDiscountNum = 3u;
constexpr u32 kKneserNeyDiscountDen = 4u;

struct NgramEntry {
    u32 token;
    u32 weight;
};

class NgramTable {
public:
    // Parses an ngram.bin payload for a vocabulary of `vocab_size` tokens.
    bool load(const u8* payload, std::size_t length, u32 vocab_size, std::string& error);

    bool loaded() const noexcept { return vocab_size_ != 0u; }
    u32  vocab_size() const noexcept { return vocab_size_; }
    u32  bos() const noexcept { return vocab_size_; }   // the history at position 0

    const u32* unigrams() const noexcept { return unigram_.data(); }
    u32        default_lambda() const noexcept { return default_lambda_; }

    // lambda and the sparse bigram row for `history`. A history with no row
    // (never seen in training) gets the default lambda and an empty row.
    void row(u32 history, u32& lambda, const NgramEntry*& entries, u32& count) const noexcept;

    u32 row_count() const noexcept { return static_cast<u32>(rows_.size()); }
    u32 row_history(u32 index) const noexcept { return rows_[index].history; }

private:
    struct Row {
        u32 history;
        u32 lambda;
        u32 first;
        u32 count;
    };

    u32                     vocab_size_     = 0u;
    u32                     default_lambda_ = 0u;
    std::vector<u32>        unigram_;
    std::vector<Row>        rows_;
    std::vector<NgramEntry> entries_;
};

// The Tier 2 PayloadModel — packet/assemble.h contract P1–P6.
//
//   P1/P2  the table depends only on the position, on preceding[position − 1]
//          (never later entries) and on the table and boost fixed at construction
//   P3     the table and boost are const for the model's whole life: nothing is
//          learned while coding (§6.6)
//   P4     integer frequencies; every token >= 1 (the floor); total <= 2^24
//   P5     returns a scratch table, valid until the next model_at()
//
// One coder at a time per model (the scratch table is shared).
class Tier2Model final : public PayloadModel {
public:
    // `boost` null: unboosted. `table` and `boost` must outlive the model.
    Tier2Model(const NgramTable& table, const ContextBoost* boost);

    const Model& model_at(u32 position, const Symbol* preceding) const noexcept override;

private:
    class Distribution final : public Model {
    public:
        u32         total() const noexcept override;
        SymbolRange range_of(u32 symbol) const noexcept override;
        u32         find(u32 target, SymbolRange& range) const noexcept override;

        std::vector<u32> cum;   // cum[s] = sum of freq over tokens below s; size V + 1
    };

    const NgramTable&    table_;
    const ContextBoost*  boost_;
    mutable Distribution scratch_;
};

// ---------------------------------------------------------------------------
// Training — build time only (native/tools/packc.cpp). Integer arithmetic, so
// the same training data gives the same table on any machine.
// ---------------------------------------------------------------------------

struct NgramSource {
    struct Row {
        u32                     history;
        u32                     lambda;
        std::vector<NgramEntry> entries;
    };

    u32              vocab_size     = 0u;
    u32              default_lambda = 0u;
    std::vector<u32> unigram;
    std::vector<Row> rows;
};

// Estimates the table from training token sequences (one per training text;
// each starts at BOS). False if the vocabulary size is out of range, a token is
// >= vocab_size, or there are no tokens at all.
bool estimate_kneser_ney(u32 vocab_size, const std::vector<std::vector<Symbol>>& sequences, NgramSource& out,
                         std::string& error);

// A complete ngram.bin container file.
std::vector<u8> serialize_ngram(const NgramSource& source);

}  // namespace itantra
