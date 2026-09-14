#pragma once

// Tier 2 tables — the shared, versioned Tier 2 wire contract (tier §3.4:
// "Subword vocabulary, n-gram table — shared").
//
//   tier2/
//     subwords.bin   vocabulary and tokenizer version      tier2/subword.h
//     ngram.bin      static Kneser-Ney table                tier2/ngram.h
//     boost.bin      (slot, value) → subwords, magnitude    tier2/boost.h
//
// One set for all ten languages (tier §6.2, §6.4). Each file is a lang/pack.h
// container (magic, kind, length, CRC-32), so a corrupted table is refused on
// load rather than coded with.
//
// Loaded once and immutable afterwards: the table is never adapted, so every
// clause starts from the same static table (tier §6.6, T4). Identical files on
// both phones are required (packet/assemble.h P6); how pairing verifies that is
// not decided here.

#include <string>
#include <vector>

#include "lang/pack.h"
#include "tier2/boost.h"
#include "tier2/ngram.h"
#include "tier2/subword.h"

namespace itantra {

class Tier2Tables {
public:
    Tier2Tables() = default;
    Tier2Tables(const Tier2Tables&) = delete;
    Tier2Tables& operator=(const Tier2Tables&) = delete;
    Tier2Tables(Tier2Tables&&) = default;
    Tier2Tables& operator=(Tier2Tables&&) = default;

    static const std::vector<std::string>& file_names();

    // Loads and cross-checks all three files. On failure nothing is loaded.
    bool load(const PackFiles& files, std::string& error);

    bool loaded() const noexcept { return loaded_; }

    const SubwordVocabulary& vocabulary() const noexcept { return vocabulary_; }
    const NgramTable&        ngram() const noexcept { return ngram_; }
    const BoostTable&        boost() const noexcept { return boost_; }

private:
    SubwordVocabulary vocabulary_;
    NgramTable        ngram_;
    BoostTable        boost_;
    bool              loaded_ = false;
};

}  // namespace itantra
