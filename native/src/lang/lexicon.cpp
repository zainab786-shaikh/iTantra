#include "lang/lexicon.h"

#include <algorithm>
#include <map>

#include "lang/utf8.h"

namespace itantra {

namespace {

constexpr std::size_t kNodeBytes    = 18u;
constexpr std::size_t kEdgeBytes    = 5u;
constexpr std::size_t kPatternBytes = 8u;

u32 be32(const u8* p) noexcept {
    return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | u32{p[3]};
}

u16 be16(const u8* p) noexcept {
    return static_cast<u16>((u32{p[0]} << 8) | u32{p[1]});
}

void put32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 24));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

void put16(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

u32 count_codepoints(const std::string& s) {
    const auto* data = reinterpret_cast<const u8*>(s.data());
    u32 n = 0u;
    for (std::size_t i = 0u; i < s.size(); ++n) {
        u32 cp = 0u;
        bool valid = true;
        i += utf8::decode(data, s.size(), i, cp, valid);
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

u32 AutomatonBuilder::add(const std::string& bytes) {
    if (bytes.empty()) return kNoIndex;
    for (std::size_t i = 0u; i < patterns_.size(); ++i) {
        if (patterns_[i] == bytes) return static_cast<u32>(i);
    }
    patterns_.push_back(bytes);
    return static_cast<u32>(patterns_.size() - 1u);
}

void AutomatonBuilder::serialize(std::vector<u8>& out) const {
    struct TrieNode {
        std::map<u8, u32> next;
        u32               pattern = kNoIndex;
    };
    std::vector<TrieNode> trie(1u);
    for (std::size_t p = 0u; p < patterns_.size(); ++p) {
        u32 node = 0u;
        for (char ch : patterns_[p]) {
            const u8 b = static_cast<u8>(ch);
            auto it = trie[node].next.find(b);
            if (it == trie[node].next.end()) {
                trie.emplace_back();
                const u32 created = static_cast<u32>(trie.size() - 1u);
                trie[node].next.emplace(b, created);
                node = created;
            } else {
                node = it->second;
            }
        }
        trie[node].pattern = static_cast<u32>(p);
    }

    // Breadth-first order, failure and output links.
    std::vector<u32> order;
    order.reserve(trie.size());
    order.push_back(0u);
    std::vector<u32> fail(trie.size(), 0u);
    std::vector<u32> output(trie.size(), kNoIndex);
    for (std::size_t head = 0u; head < order.size(); ++head) {
        const u32 u = order[head];
        for (const auto& edge : trie[u].next) {
            const u8  b = edge.first;
            const u32 v = edge.second;
            if (u == 0u) {
                fail[v] = 0u;
            } else {
                u32 f = fail[u];
                for (;;) {
                    auto it = trie[f].next.find(b);
                    if (it != trie[f].next.end()) {
                        fail[v] = it->second;
                        break;
                    }
                    if (f == 0u) {
                        fail[v] = 0u;
                        break;
                    }
                    f = fail[f];
                }
            }
            const u32 f = fail[v];
            output[v] = trie[f].pattern != kNoIndex ? f : output[f];
            order.push_back(v);
        }
    }

    std::vector<u32> rank(trie.size(), 0u);
    for (std::size_t i = 0u; i < order.size(); ++i) rank[order[i]] = static_cast<u32>(i);

    put32(out, static_cast<u32>(order.size()));
    u32 edge_cursor = 0u;
    for (u32 old : order) {
        put32(out, edge_cursor);
        put16(out, static_cast<u32>(trie[old].next.size()));
        put32(out, rank[fail[old]]);
        put32(out, output[old] == kNoIndex ? kNoIndex : rank[output[old]]);
        put32(out, trie[old].pattern);
        edge_cursor += static_cast<u32>(trie[old].next.size());
    }
    put32(out, edge_cursor);
    for (u32 old : order) {
        for (const auto& edge : trie[old].next) {
            out.push_back(edge.first);
            put32(out, rank[edge.second]);
        }
    }
    put32(out, static_cast<u32>(patterns_.size()));
    for (const std::string& p : patterns_) {
        put32(out, static_cast<u32>(p.size()));
        put32(out, count_codepoints(p));
    }
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

u32 Automaton::node_first_edge(u32 n) const noexcept { return be32(nodes_ + n * kNodeBytes); }
u32 Automaton::node_edge_count(u32 n) const noexcept { return be16(nodes_ + n * kNodeBytes + 4u); }
u32 Automaton::node_fail(u32 n) const noexcept { return be32(nodes_ + n * kNodeBytes + 6u); }
u32 Automaton::node_output(u32 n) const noexcept { return be32(nodes_ + n * kNodeBytes + 10u); }
u32 Automaton::node_pattern(u32 n) const noexcept { return be32(nodes_ + n * kNodeBytes + 14u); }

u32 Automaton::pattern_bytes(u32 pattern) const noexcept {
    return pattern < pattern_count_ ? be32(patterns_ + pattern * kPatternBytes) : 0u;
}

u32 Automaton::pattern_codepoints(u32 pattern) const noexcept {
    return pattern < pattern_count_ ? be32(patterns_ + pattern * kPatternBytes + 4u) : 0u;
}

u32 Automaton::child(u32 n, u8 byte) const noexcept {
    u32 lo = node_first_edge(n);
    u32 hi = lo + node_edge_count(n);
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2u;
        const u8  b   = edges_[mid * kEdgeBytes];
        if (b < byte) {
            lo = mid + 1u;
        } else if (b > byte) {
            hi = mid;
        } else {
            return be32(edges_ + mid * kEdgeBytes + 1u);
        }
    }
    return kNoIndex;
}

bool Automaton::attach(const u8* data, std::size_t length, std::size_t& consumed, std::string& error) {
    nodes_ = edges_ = patterns_ = nullptr;
    node_count_ = edge_count_ = pattern_count_ = 0u;
    consumed = 0u;
    auto fail_with = [&error](const char* why) {
        error = std::string("automaton: ") + why;
        return false;
    };
    if (data == nullptr) return fail_with("no data");

    std::size_t at = 0u;
    auto need = [&](std::size_t n) { return length >= at && length - at >= n; };

    if (!need(4u)) return fail_with("truncated");
    const u32 node_count = be32(data + at);
    at += 4u;
    if (node_count == 0u || !need(static_cast<std::size_t>(node_count) * kNodeBytes)) return fail_with("bad node table");
    const u8* nodes = data + at;
    at += static_cast<std::size_t>(node_count) * kNodeBytes;

    if (!need(4u)) return fail_with("truncated");
    const u32 edge_count = be32(data + at);
    at += 4u;
    if (!need(static_cast<std::size_t>(edge_count) * kEdgeBytes)) return fail_with("bad edge table");
    const u8* edges = data + at;
    at += static_cast<std::size_t>(edge_count) * kEdgeBytes;

    if (!need(4u)) return fail_with("truncated");
    const u32 pattern_count = be32(data + at);
    at += 4u;
    if (!need(static_cast<std::size_t>(pattern_count) * kPatternBytes)) return fail_with("bad pattern table");
    const u8* patterns = data + at;
    at += static_cast<std::size_t>(pattern_count) * kPatternBytes;

    nodes_ = nodes;
    edges_ = edges;
    patterns_ = patterns;
    node_count_ = node_count;
    edge_count_ = edge_count;
    pattern_count_ = pattern_count;

    // Structure: breadth-first order, links backwards, edges sorted, each
    // node reached exactly once, each pattern at a node of its own depth.
    std::vector<u32> depth(node_count, kNoIndex);
    depth[0] = 0u;
    std::vector<u32> pattern_seen(pattern_count, 0u);
    u32 expected_edge = 0u;
    for (u32 n = 0u; n < node_count; ++n) {
        const u32 first = node_first_edge(n);
        const u32 count = node_edge_count(n);
        const u32 f     = node_fail(n);
        const u32 o     = node_output(n);
        const u32 p     = node_pattern(n);
        if (depth[n] == kNoIndex) return fail_with("unreachable node");
        if (first != expected_edge || first + count > edge_count) return fail_with("edge range");
        expected_edge += count;
        if (n == 0u ? f != 0u : f >= n) return fail_with("failure link");
        if (o != kNoIndex && (o >= n || node_pattern(o) == kNoIndex)) return fail_with("output link");
        if (p != kNoIndex) {
            if (p >= pattern_count || pattern_seen[p] != 0u) return fail_with("pattern index");
            pattern_seen[p] = 1u;
            if (be32(patterns + p * kPatternBytes) != depth[n]) return fail_with("pattern length");
        }
        for (u32 e = first; e < first + count; ++e) {
            const u32 c = be32(edges + e * kEdgeBytes + 1u);
            if (e > first && edges[e * kEdgeBytes] <= edges[(e - 1u) * kEdgeBytes]) return fail_with("edges not sorted");
            if (c <= n || c >= node_count || depth[c] != kNoIndex) return fail_with("child index");
            depth[c] = depth[n] + 1u;
        }
    }
    if (expected_edge != edge_count) return fail_with("edge count");
    for (u32 p = 0u; p < pattern_count; ++p) {
        const u32 bytes = be32(patterns + p * kPatternBytes);
        const u32 cps   = be32(patterns + p * kPatternBytes + 4u);
        if (pattern_seen[p] == 0u || bytes == 0u || cps == 0u || cps > bytes) return fail_with("pattern table");
    }
    consumed = at;
    return true;
}

void Automaton::find_all(const u8* text, std::size_t length, std::vector<Hit>& out) const {
    if (nodes_ == nullptr || text == nullptr) return;
    u32 state = 0u;
    for (std::size_t i = 0u; i < length; ++i) {
        const u8 b = text[i];
        for (;;) {
            const u32 c = child(state, b);
            if (c != kNoIndex) {
                state = c;
                break;
            }
            if (state == 0u) break;
            state = node_fail(state);
        }
        u32 s = node_pattern(state) != kNoIndex ? state : node_output(state);
        while (s != kNoIndex) {
            const u32 p   = node_pattern(s);
            const u32 end = static_cast<u32>(i + 1u);
            out.push_back(Hit{p, end - pattern_bytes(p), end});
            s = node_output(s);
        }
    }
}

// ---------------------------------------------------------------------------
// Boundaries and selection
// ---------------------------------------------------------------------------

std::vector<bool> codepoint_starts(const u8* text, std::size_t length) {
    std::vector<bool> starts(length + 1u, false);
    for (std::size_t i = 0u; i < length;) {
        starts[i] = true;
        u32 cp = 0u;
        bool valid = true;
        i += utf8::decode(text, length, i, cp, valid);
    }
    starts[length] = true;
    return starts;
}

bool on_codepoint_boundaries(const std::vector<bool>& starts, u32 begin, u32 end) noexcept {
    return begin < end && end < starts.size() && starts[begin] && starts[end];
}

bool on_token_boundaries(const u8* text, std::size_t length, u32 begin, u32 end) noexcept {
    if (begin >= end || end > length) return false;
    const bool left  = begin == 0u || text[begin - 1u] == 0x20u;
    const bool right = end == length || text[end] == 0x20u;
    return left && right;
}

std::vector<MatchCandidate> select_matches(std::vector<MatchCandidate> candidates) {
    std::sort(candidates.begin(), candidates.end(), [](const MatchCandidate& a, const MatchCandidate& b) {
        if (a.codepoints != b.codepoints) return a.codepoints > b.codepoints;
        if (a.begin != b.begin) return a.begin < b.begin;
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.index < b.index;
    });
    std::vector<MatchCandidate> taken;
    for (const MatchCandidate& c : candidates) {
        bool overlaps = false;
        for (const MatchCandidate& t : taken) {
            if (c.begin < t.end && t.begin < c.end) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) taken.push_back(c);
    }
    std::sort(taken.begin(), taken.end(),
              [](const MatchCandidate& a, const MatchCandidate& b) { return a.begin < b.begin; });
    return taken;
}

}  // namespace itantra
