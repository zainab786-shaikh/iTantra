#include "tier2/ngram.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "lang/pack.h"
#include "tier2/boost.h"
#include "tier2/subword.h"
#include "tier2/wire.h"

namespace itantra {

static_assert(u64{kMaxVocabularySize} + kContextMassLimit + kBoostMassLimit <= kModelMaxTotal,
              "Tier 2 frequency bounds must keep every total within the coder's limit");

namespace {

bool fail(std::string& error, const char* why) {
    error = why;
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// NgramTable
// ---------------------------------------------------------------------------

bool NgramTable::load(const u8* payload, std::size_t length, u32 vocab_size, std::string& error) {
    *this = NgramTable{};
    tier2wire::Reader r(payload, length);

    u32 version        = 0u;
    u32 order          = 0u;
    u32 v              = 0u;
    u32 default_lambda = 0u;
    if (!r.read32(version) || !r.read32(order) || !r.read32(v) || !r.read32(default_lambda)) {
        return fail(error, "truncated header");
    }
    if (version != kNgramTableVersion) return fail(error, "unsupported table version");
    if (order != kNgramOrder) return fail(error, "unsupported n-gram order");
    if (v != vocab_size || v < kByteTokenCount || v > kMaxVocabularySize) {
        return fail(error, "vocabulary size does not match the subword vocabulary");
    }
    if (default_lambda > kContextMassLimit) return fail(error, "default lambda above the context mass limit");

    std::vector<u32> unigram(v);
    u64 sum = 0u;
    for (u32 s = 0u; s < v; ++s) {
        if (!r.read32(unigram[s])) return fail(error, "truncated unigram table");
        sum += unigram[s];
    }
    if (sum != kUnigramTotal) return fail(error, "unigram frequencies do not sum to 2^16");

    u32 row_count = 0u;
    if (!r.read32(row_count)) return fail(error, "truncated row count");
    if (row_count > v + 1u || row_count > r.remaining() / 12u) return fail(error, "truncated or oversized rows");

    std::vector<Row> rows(row_count);
    u64 total_entries = 0u;
    for (u32 k = 0u; k < row_count; ++k) {
        Row& row = rows[k];
        if (!r.read32(row.history) || !r.read32(row.lambda) || !r.read32(row.count)) {
            return fail(error, "truncated rows");
        }
        if (row.history > v) return fail(error, "history out of range");
        if (k != 0u && row.history <= rows[k - 1u].history) return fail(error, "histories not strictly ascending");
        if (row.count > v) return fail(error, "row longer than the vocabulary");
        row.first = static_cast<u32>(total_entries);
        total_entries += row.count;
    }
    if (total_entries > r.remaining() / 8u) return fail(error, "truncated entries");

    std::vector<NgramEntry> entries(static_cast<std::size_t>(total_entries));
    for (const Row& row : rows) {
        u64 mass = row.lambda;
        for (u32 j = 0u; j < row.count; ++j) {
            NgramEntry& e = entries[static_cast<std::size_t>(row.first) + j];
            if (!r.read32(e.token) || !r.read32(e.weight)) return fail(error, "truncated entries");
            if (e.token >= v) return fail(error, "entry token out of range");
            if (j != 0u && e.token <= entries[static_cast<std::size_t>(row.first) + j - 1u].token) {
                return fail(error, "entry tokens not strictly ascending");
            }
            if (e.weight == 0u) return fail(error, "zero entry weight");
            mass += e.weight;
        }
        if (mass > kContextMassLimit) return fail(error, "row mass above the context mass limit");
    }
    if (r.remaining() != 0u) return fail(error, "trailing bytes");

    vocab_size_     = v;
    default_lambda_ = default_lambda;
    unigram_        = std::move(unigram);
    rows_           = std::move(rows);
    entries_        = std::move(entries);
    return true;
}

void NgramTable::row(u32 history, u32& lambda, const NgramEntry*& entries, u32& count) const noexcept {
    lambda  = default_lambda_;
    entries = nullptr;
    count   = 0u;
    std::size_t lo = 0u;
    std::size_t hi = rows_.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        if (rows_[mid].history < history) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < rows_.size() && rows_[lo].history == history) {
        lambda = rows_[lo].lambda;
        count  = rows_[lo].count;
        if (count != 0u) entries = &entries_[rows_[lo].first];
    }
}

// ---------------------------------------------------------------------------
// Tier2Model
// ---------------------------------------------------------------------------

Tier2Model::Tier2Model(const NgramTable& table, const ContextBoost* boost) : table_(table), boost_(boost) {
    scratch_.cum.assign(static_cast<std::size_t>(table.vocab_size()) + 1u, 0u);
}

const Model& Tier2Model::model_at(u32 position, const Symbol* preceding) const noexcept {
    const u32 v       = table_.vocab_size();
    const u32 history = position == 0u ? table_.bos() : preceding[position - 1u];   // P2

    u32               lambda  = 0u;
    const NgramEntry* entries = nullptr;
    u32               count   = 0u;
    table_.row(history, lambda, entries, count);

    const u32* boosted       = nullptr;
    u32        boosted_count = 0u;
    u32        magnitude     = 0u;
    if (boost_ != nullptr) {
        boosted       = boost_->tokens();
        boosted_count = boost_->size();
        magnitude     = boost_->magnitude();
    }

    const u32* uni = table_.unigrams();
    u32* cum = scratch_.cum.data();
    cum[0] = 0u;
    u32 e = 0u;
    u32 b = 0u;
    for (u32 s = 0u; s < v; ++s) {
        u32 f = 1u + static_cast<u32>((static_cast<u64>(lambda) * uni[s]) >> kUnigramTotalBits);
        if (e < count && entries[e].token == s) {
            f += entries[e].weight;
            ++e;
        }
        if (b < boosted_count && boosted[b] == s) {
            f += magnitude;
            ++b;
        }
        cum[s + 1u] = cum[s] + f;
    }
    assert(cum[v] <= kModelMaxTotal && "Tier 2 table bounds violated");
    return scratch_;
}

u32 Tier2Model::Distribution::total() const noexcept {
    return cum.empty() ? 0u : cum.back();
}

SymbolRange Tier2Model::Distribution::range_of(u32 symbol) const noexcept {
    const u32 t = total();
    if (cum.empty() || symbol >= cum.size() - 1u) return SymbolRange{0u, 0u, t};
    return SymbolRange{cum[symbol], cum[static_cast<std::size_t>(symbol) + 1u], t};
}

u32 Tier2Model::Distribution::find(u32 target, SymbolRange& range) const noexcept {
    const u32 t = total();
    if (cum.size() < 2u || target >= t) {
        range = SymbolRange{0u, 0u, t};
        return 0u;
    }
    // Largest s with cum[s] <= target; cum[V] = total > target (see StaticModel::find).
    u32 lo = 0u;
    u32 hi = static_cast<u32>(cum.size() - 1u);
    while (hi - lo > 1u) {
        const u32 mid = lo + (hi - lo) / 2u;
        if (cum[mid] <= target) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    range = SymbolRange{cum[lo], cum[static_cast<std::size_t>(lo) + 1u], t};
    return lo;
}

// ---------------------------------------------------------------------------
// Training
// ---------------------------------------------------------------------------

bool estimate_kneser_ney(u32 vocab_size, const std::vector<std::vector<Symbol>>& sequences, NgramSource& out,
                         std::string& error) {
    out = NgramSource{};
    if (vocab_size < kByteTokenCount || vocab_size > kMaxVocabularySize) {
        return fail(error, "vocabulary size out of range");
    }

    // Every (history, token) occurrence, history V = BOS at each sequence start.
    std::vector<u64> pairs;
    for (const std::vector<Symbol>& sequence : sequences) {
        u32 history = vocab_size;
        for (Symbol token : sequence) {
            if (token >= vocab_size) return fail(error, "training token out of range");
            pairs.push_back((u64{history} << 32) | token);
            history = token;
        }
    }
    if (pairs.empty()) return fail(error, "no training tokens");
    std::sort(pairs.begin(), pairs.end());

    // Continuation counts N(• s): distinct histories before each token.
    std::vector<u64> continuation(vocab_size, 0u);
    u64 distinct_pairs = 0u;
    for (std::size_t i = 0u; i < pairs.size(); ++i) {
        if (i == 0u || pairs[i] != pairs[i - 1u]) {
            ++continuation[static_cast<u32>(pairs[i])];
            ++distinct_pairs;
        }
    }

    // uni[]: N(• s) scaled to exactly 2^16, largest remainder, ties by token.
    out.unigram.assign(vocab_size, 0u);
    std::vector<std::pair<u64, u32>> remainders;
    u64 assigned = 0u;
    for (u32 s = 0u; s < vocab_size; ++s) {
        const u64 scaled = continuation[s] * kUnigramTotal;
        out.unigram[s] = static_cast<u32>(scaled / distinct_pairs);
        assigned += out.unigram[s];
        const u64 rem = scaled % distinct_pairs;
        if (rem != 0u) remainders.emplace_back(rem, s);
    }
    std::sort(remainders.begin(), remainders.end(),
              [](const std::pair<u64, u32>& a, const std::pair<u64, u32>& b) {
                  return a.first != b.first ? a.first > b.first : a.second < b.second;
              });
    for (std::size_t k = 0u; assigned < kUnigramTotal && k < remainders.size(); ++k) {
        ++out.unigram[remainders[k].second];
        ++assigned;
    }
    if (assigned != kUnigramTotal) return fail(error, "unigram quantisation did not reach 2^16");

    // One row per seen history: discounted bigram weights and backoff lambda.
    const u64 mass = kContextMassLimit;
    for (std::size_t i = 0u; i < pairs.size();) {
        const u32 history = static_cast<u32>(pairs[i] >> 32);
        std::vector<std::pair<u32, u64>> counts;
        u64 total    = 0u;
        u64 distinct = 0u;
        std::size_t j = i;
        while (j < pairs.size() && static_cast<u32>(pairs[j] >> 32) == history) {
            std::size_t k = j;
            while (k < pairs.size() && pairs[k] == pairs[j]) ++k;
            counts.emplace_back(static_cast<u32>(pairs[j]), static_cast<u64>(k - j));
            total += k - j;
            ++distinct;
            j = k;
        }

        NgramSource::Row row;
        row.history = history;
        const u64 den = u64{kKneserNeyDiscountDen} * total;
        for (const std::pair<u32, u64>& c : counts) {
            const u64 w = mass * (u64{kKneserNeyDiscountDen} * c.second - kKneserNeyDiscountNum) / den;
            if (w != 0u) row.entries.push_back(NgramEntry{c.first, static_cast<u32>(w)});
        }
        row.lambda = static_cast<u32>(mass * kKneserNeyDiscountNum * distinct / den);
        out.rows.push_back(std::move(row));
        i = j;
    }

    out.vocab_size     = vocab_size;
    out.default_lambda = kContextMassLimit;
    return true;
}

std::vector<u8> serialize_ngram(const NgramSource& source) {
    std::vector<u8> payload;
    tier2wire::put32(payload, kNgramTableVersion);
    tier2wire::put32(payload, kNgramOrder);
    tier2wire::put32(payload, source.vocab_size);
    tier2wire::put32(payload, source.default_lambda);
    for (u32 u : source.unigram) tier2wire::put32(payload, u);
    tier2wire::put32(payload, static_cast<u32>(source.rows.size()));
    for (const NgramSource::Row& row : source.rows) {
        tier2wire::put32(payload, row.history);
        tier2wire::put32(payload, row.lambda);
        tier2wire::put32(payload, static_cast<u32>(row.entries.size()));
    }
    for (const NgramSource::Row& row : source.rows) {
        for (const NgramEntry& e : row.entries) {
            tier2wire::put32(payload, e.token);
            tier2wire::put32(payload, e.weight);
        }
    }
    return wrap_container(PackKind::Ngram, payload);
}

}  // namespace itantra
