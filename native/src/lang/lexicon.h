#pragma once

// Lexicon matching — language-layer-spec §8.1.
//
// "Single-pass multi-pattern automaton (Aho-Corasick)… Match on NFC-normalised
// UTF-8 bytes, with a codepoint-boundary check on each reported match… Use a
// compact representation (double-array trie or sparse transitions). Dense
// 256-way arrays per node will exceed the memory budget."
//
// Representation: sparse transitions. Every node stores a sorted run of
// (byte, child) edges, a failure link and a dictionary-suffix ("output") link.
// Built once by the pack compiler, serialised into lexicon.bin / numbers.bin,
// and used in place — no copy, no per-load construction — so the buffer can be
// mmap'd (language spec §4.4, plan Phase 6).
//
// Serialised layout (big-endian, all offsets checked by attach()):
//
//   u32 node_count
//   node × node_count       u32 first_edge, u16 edge_count, u32 fail,
//                           u32 output, u32 pattern        (18 bytes)
//   u32 edge_count
//   edge × edge_count       u8 byte, u32 child             (5 bytes)
//   u32 pattern_count
//   pattern × pattern_count u32 byte_length, u32 codepoint_length (8 bytes)
//
//   Nodes are in breadth-first order: node 0 is the root, every child index
//   exceeds its parent's, every fail and output link points to an earlier node.
//
// Selection — the two §8.1 rules, applied as a total order so every
// implementation agrees:
//
//   1  longest match wins        (length in CODEPOINTS, so scripts with 3-byte
//                                 characters are not favoured over Latin)
//   2  leftmost where equal-length matches overlap
//
//   Candidates are taken greedily in order (codepoints desc, begin asc,
//   kind asc, index asc); any candidate overlapping one already taken is
//   dropped. E.g. "north gate" beats "gate"; of "a b" and "b c" over "a b c",
//   "a b" is taken.
//
// Boundaries — both are required of every accepted match:
//   codepoint   both ends fall on codepoint starts (§8.1, L4)
//   token       both ends fall on the text's ends or next to U+0020 — so
//               "gate" never matches inside "gateway". Language-neutral: all
//               ten languages separate words with spaces, and inflected forms
//               are listed whole (§14.1), not found inside longer words.

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"

namespace itantra {

constexpr u32 kNoIndex = 0xFFFFFFFFu;

class AutomatonBuilder {
public:
    // Adds a non-empty byte pattern. Identical patterns share one id. Returns
    // the pattern id, or kNoIndex for an empty pattern.
    u32 add(const std::string& bytes);

    u32 pattern_count() const noexcept { return static_cast<u32>(patterns_.size()); }

    // Appends the serialised automaton to `out`. Pattern ids are preserved.
    void serialize(std::vector<u8>& out) const;

private:
    std::vector<std::string> patterns_;
};

class Automaton {
public:
    struct Hit {
        u32 pattern;
        u32 begin;
        u32 end;
    };

    // Validates the serialised automaton at data[0 ..) and attaches to it. The
    // bytes are not copied and must outlive this object. `consumed` receives
    // the number of bytes the automaton occupies.
    bool attach(const u8* data, std::size_t length, std::size_t& consumed, std::string& error);

    bool attached() const noexcept { return nodes_ != nullptr; }
    u32  pattern_count() const noexcept { return pattern_count_; }
    u32  pattern_bytes(u32 pattern) const noexcept;
    u32  pattern_codepoints(u32 pattern) const noexcept;

    // Every occurrence of every pattern, byte-level, in scan order.
    void find_all(const u8* text, std::size_t length, std::vector<Hit>& out) const;

private:
    u32 node_first_edge(u32 n) const noexcept;
    u32 node_edge_count(u32 n) const noexcept;
    u32 node_fail(u32 n) const noexcept;
    u32 node_output(u32 n) const noexcept;
    u32 node_pattern(u32 n) const noexcept;
    u32 child(u32 n, u8 byte) const noexcept;

    const u8* nodes_         = nullptr;
    const u8* edges_         = nullptr;
    const u8* patterns_      = nullptr;
    u32       node_count_    = 0u;
    u32       edge_count_    = 0u;
    u32       pattern_count_ = 0u;
};

// A potential match from any source (lexicon, number words, digit runs).
struct MatchCandidate {
    u32 begin;        // bytes in the matched text
    u32 end;
    u32 codepoints;
    u8  kind;         // caller-defined; lower kinds win exact ties
    u32 index;        // caller-defined
};

// Codepoint-start flags for text[0, n]: true where a codepoint (or an invalid
// byte unit) begins, and at n.
std::vector<bool> codepoint_starts(const u8* text, std::size_t length);

// §8.1 boundary checks.
bool on_codepoint_boundaries(const std::vector<bool>& starts, u32 begin, u32 end) noexcept;
bool on_token_boundaries(const u8* text, std::size_t length, u32 begin, u32 end) noexcept;

// Longest (codepoints), then leftmost, non-overlapping; returned in text order.
std::vector<MatchCandidate> select_matches(std::vector<MatchCandidate> candidates);

}  // namespace itantra
