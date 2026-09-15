#include "tier2/subword.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>

#include "lang/pack.h"
#include "lang/utf8.h"
#include "tier2/wire.h"

namespace itantra {

namespace {

constexpr u32 kNoToken = 0xFFFFFFFFu;
constexpr u32 kNoNode  = 0xFFFFFFFFu;

bool fail(std::string& error, const char* why) {
    error = why;
    return false;
}

// The bytes of every byte token, so token_bytes() can return a view.
const char* byte_chars() noexcept {
    static const std::array<char, 256> table = [] {
        std::array<char, 256> t{};
        for (u32 b = 0u; b < 256u; ++b) t[b] = static_cast<char>(static_cast<u8>(b));
        return t;
    }();
    return table.data();
}

// Unsigned byte order, so each trie node's edges come out sorted by byte value.
bool bytes_less(const std::string& a, const std::string& b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    const int c = n == 0u ? 0 : std::memcmp(a.data(), b.data(), n);
    return c != 0 ? c < 0 : a.size() < b.size();
}

}  // namespace

const char* subword_fault(const std::string& bytes) noexcept {
    if (bytes.size() < kSubwordMinBytes) return "subword shorter than 2 bytes (single bytes are byte tokens)";
    if (bytes.size() > kSubwordMaxBytes) return "subword longer than 64 bytes";
    const auto* d = reinterpret_cast<const u8*>(bytes.data());
    for (std::size_t i = 0u; i < bytes.size();) {
        u32  cp    = 0u;
        bool valid = true;
        i += utf8::decode(d, bytes.size(), i, cp, valid);
        if (!valid) return "subword is not complete, shortest-form UTF-8";
    }
    return nullptr;
}

bool SubwordVocabulary::load(const u8* payload, std::size_t length, std::string& error) {
    *this = SubwordVocabulary{};
    tier2wire::Reader r(payload, length);

    u32 version = 0u;
    u32 count   = 0u;
    if (!r.read32(version) || !r.read32(count)) return fail(error, "truncated header");
    if (version != kTokenizerVersion) return fail(error, "unsupported tokenizer version");
    if (count > kMaxVocabularySize - kByteTokenCount) return fail(error, "vocabulary larger than 65536 tokens");
    if (count > r.remaining() / (1u + kSubwordMinBytes)) return fail(error, "truncated subwords");

    std::vector<std::string> subwords;
    subwords.reserve(count);
    for (u32 k = 0u; k < count; ++k) {
        u8        n     = 0u;
        const u8* bytes = nullptr;
        if (!r.read8(n) || !r.take(n, bytes)) return fail(error, "truncated subwords");
        std::string s(reinterpret_cast<const char*>(bytes), n);
        if (const char* why = subword_fault(s)) return fail(error, why);
        subwords.push_back(std::move(s));
    }
    if (r.remaining() != 0u) return fail(error, "trailing bytes");

    std::vector<u32> order(count);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&subwords](u32 a, u32 b) { return bytes_less(subwords[a], subwords[b]); });
    for (u32 k = 1u; k < count; ++k) {
        if (subwords[order[k - 1u]] == subwords[order[k]]) return fail(error, "duplicate subword");
    }

    subwords_ = std::move(subwords);
    build(order, 0u, count, 0u);
    return true;
}

// Builds the node for the prefix shared by subwords order[lo, hi) (all at least
// `depth` bytes, sorted). A node's edges are one contiguous run, reserved before
// its children are built, so they stay sorted by byte.
u32 SubwordVocabulary::build(const std::vector<u32>& order, u32 lo, u32 hi, u32 depth) {
    const u32 node = static_cast<u32>(nodes_.size());
    nodes_.push_back(Node{0u, 0u, kNoToken});
    if (lo < hi && subwords_[order[lo]].size() == depth) {
        nodes_[node].token = kByteTokenCount + order[lo];
        ++lo;
    }

    struct Run {
        u8  byte;
        u32 lo;
        u32 hi;
    };
    std::vector<Run> runs;
    for (u32 i = lo; i < hi;) {
        const u8 b = static_cast<u8>(subwords_[order[i]][depth]);
        u32 j = i + 1u;
        while (j < hi && static_cast<u8>(subwords_[order[j]][depth]) == b) ++j;
        runs.push_back(Run{b, i, j});
        i = j;
    }

    const u32 first = static_cast<u32>(edges_.size());
    nodes_[node].first_edge = first;
    nodes_[node].edge_count = static_cast<u32>(runs.size());
    edges_.resize(edges_.size() + runs.size());
    for (std::size_t k = 0u; k < runs.size(); ++k) {
        const u32 c = build(order, runs[k].lo, runs[k].hi, depth + 1u);
        edges_[first + k] = Edge{c, runs[k].byte};
    }
    return node;
}

u32 SubwordVocabulary::child(u32 node, u8 byte) const noexcept {
    const Node& n = nodes_[node];
    u32 lo = n.first_edge;
    u32 hi = n.first_edge + n.edge_count;
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2u;
        const u8  b   = edges_[mid].byte;
        if (b < byte) {
            lo = mid + 1u;
        } else if (b > byte) {
            hi = mid;
        } else {
            return edges_[mid].child;
        }
    }
    return kNoNode;
}

std::string_view SubwordVocabulary::token_bytes(u32 token) const noexcept {
    if (token < kByteTokenCount) return std::string_view(byte_chars() + token, 1u);
    if (token < size()) return subwords_[token - kByteTokenCount];
    return std::string_view();
}

void SubwordVocabulary::tokenize(const u8* text, std::size_t length, std::vector<Symbol>& out) const {
    out.clear();
    if (text == nullptr) return;
    out.reserve(length);
    std::size_t i = 0u;
    while (i < length) {
        u32         best_token  = 0u;
        std::size_t best_length = 0u;
        if (!nodes_.empty()) {
            u32 node = 0u;
            for (std::size_t j = i; j < length; ++j) {
                node = child(node, text[j]);
                if (node == kNoNode) break;
                if (nodes_[node].token != kNoToken) {
                    best_token  = nodes_[node].token;
                    best_length = j - i + 1u;
                }
            }
        }
        if (best_length != 0u) {
            out.push_back(best_token);
            i += best_length;
        } else {
            out.push_back(text[i]);   // byte fallback
            ++i;
        }
    }
}

bool SubwordVocabulary::detokenize(const Symbol* tokens, std::size_t count, std::string& out) const {
    for (std::size_t k = 0u; k < count; ++k) {
        const std::string_view bytes = token_bytes(tokens[k]);
        if (bytes.empty()) return false;
        out.append(bytes.data(), bytes.size());
    }
    return true;
}

std::vector<u8> serialize_subwords(const std::vector<std::string>& subwords) {
    std::vector<u8> payload;
    tier2wire::put32(payload, kTokenizerVersion);
    tier2wire::put32(payload, static_cast<u32>(subwords.size()));
    for (const std::string& s : subwords) {
        tier2wire::put8(payload, static_cast<u32>(s.size()));
        for (char c : s) payload.push_back(static_cast<u8>(c));
    }
    return wrap_container(PackKind::Subwords, payload);
}

}  // namespace itantra
