#pragma once

// Tier 2 subword tokenizer — tier §6.2, §12 T3, T3a.
//
// One vocabulary for all ten languages (tier §6.2), part of the shared,
// versioned wire contract (tier §3.4). Shipped as subwords.bin with the n-gram
// and boost tables (tier2/tables.h).
//
// Token IDs:
//
//   0 … 255        byte tokens: token b stands for the single byte b. "All 256
//                  byte values are vocabulary entries" (§6.2) — by construction,
//                  so they are not stored in the file.
//   256 … V − 1    subwords, in file order. Each is 2 … 64 bytes of complete,
//                  shortest-form UTF-8 (so a subword never splits a codepoint);
//                  no two are equal.
//
// Decomposition rule, FIXED (§6.2 "longest match against the subword vocabulary
// first, byte tokens for the remainder"): at each position take the longest
// subword equal to the input bytes starting there; if there is none, emit the
// byte token of that one byte and move on by one byte. Greedy, left to right.
//
// Consequences (each tested):
//
//   total      every byte string tokenizes; there is no failure path and no
//              <unk> token (§6.2, T3a)
//   lossless   the tokens' bytes, concatenated, are the input exactly
//   canonical  a subword is at least two bytes, so no byte token has a subword
//              twin, and the rule gives each input exactly one token sequence
//
// The input is whatever bytes the caller passes — invalid UTF-8 included.
// Tier 2 codes the clause's original bytes (tier2/encode.h).
//
// Versioned: kTokenizerVersion is written into subwords.bin and checked on
// load. Any change to the rule above is a new version (T3); both phones must
// hold the same one.
//
// subwords.bin payload (inside the lang/pack.h container, kind Subwords),
// big-endian:
//
//   u32 tokenizer_version      = kTokenizerVersion
//   u32 subword_count          V − 256
//   subword × subword_count    u8 byte_length (2 … 64), then the bytes

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "packet/assemble.h"

namespace itantra {

constexpr u32 kTokenizerVersion = 1u;
constexpr u32 kByteTokenCount   = 256u;
constexpr u32 kSubwordMinBytes  = 2u;
constexpr u32 kSubwordMaxBytes  = 64u;

// Largest vocabulary, byte tokens included. Bounds the model's uniform floor to
// V frequency units (tier2/ngram.h).
constexpr u32 kMaxVocabularySize = u32{1} << 16;

// Why `bytes` cannot be a subword, or nullptr if it can.
const char* subword_fault(const std::string& bytes) noexcept;

class SubwordVocabulary {
public:
    // Parses a subwords.bin payload (the container already unwrapped) and builds
    // the match trie. On failure the vocabulary is empty and `error` says why.
    bool load(const u8* payload, std::size_t length, std::string& error);

    bool loaded() const noexcept { return !nodes_.empty(); }

    // V: byte tokens plus subwords.
    u32 size() const noexcept { return kByteTokenCount + static_cast<u32>(subwords_.size()); }

    // The bytes `token` stands for; empty for token >= size().
    std::string_view token_bytes(u32 token) const noexcept;

    // Replaces `out` with the tokens of text[0, length). Cannot fail. `text` may
    // be null when length is 0.
    void tokenize(const u8* text, std::size_t length, std::vector<Symbol>& out) const;

    // Appends the bytes of tokens[0, count) to `out`. False if a token is
    // >= size(); `out` is then unspecified.
    bool detokenize(const Symbol* tokens, std::size_t count, std::string& out) const;

private:
    struct Node {
        u32 first_edge;
        u32 edge_count;
        u32 token;        // the subword ending here, or kNoToken
    };
    struct Edge {
        u32 child;
        u8  byte;
    };

    u32 build(const std::vector<u32>& order, u32 lo, u32 hi, u32 depth);
    u32 child(u32 node, u8 byte) const noexcept;

    std::vector<std::string> subwords_;   // token 256 + i
    std::vector<Node>        nodes_;      // node 0 is the root; edges sorted by byte
    std::vector<Edge>        edges_;
};

// A complete subwords.bin container file. Subword i gets token 256 + i.
// Precondition: every entry passes subword_fault(); packc checks.
std::vector<u8> serialize_subwords(const std::vector<std::string>& subwords);

}  // namespace itantra
