#pragma once

// Shared helpers for the Phase 7 Tier 2 tests: the compiled fixture packs and
// Tier 2 tables, the fixture corpus, pre-message contexts built from extracted
// slots, and which tokens the trained table has never seen.

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "context/context.h"
#include "lang/extract.h"
#include "lang_fixture.h"
#include "packet/metadata.h"
#include "tier2/tables.h"

namespace t2fx {

using itantra::u16;
using itantra::u32;
using itantra::u8;

struct Utterance {
    std::string id;
    std::string lang;
    std::string text;
};

struct Fixture {
    langfx::Fixture        lang;
    itantra::Tier2Tables   tables;
    std::vector<Utterance> corpus;
    std::string            error;

    bool load(const std::string& packs_root, const std::string& source_root) {
        if (!lang.load(packs_root, source_root)) {
            error = lang.error;
            return false;
        }
        itantra::PackFiles files;
        if (!itantra::read_pack_directory(packs_root + "/tier2", itantra::Tier2Tables::file_names(), files, error) ||
            !tables.load(files, error)) {
            return false;
        }
        for (const auto& row : langfx::read_tsv(source_root + "/corpus/utterances.tsv")) {
            if (row.size() == 6u) corpus.push_back(Utterance{row[0], row[1], row[4]});
        }
        if (corpus.empty()) {
            error = "corpus not found under " + source_root;
            return false;
        }
        return true;
    }
};

// Test-only LangIds for the three fixture languages. Which 4-bit value names
// which language is an open wire-contract item; these values assert nothing.
inline itantra::LangId test_language_id(const std::string& code) {
    if (code == "hi") return 1u;
    if (code == "ta") return 2u;
    return 3u;
}

struct XorShift32 {
    u32 s;
    u32 next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

// Stands in for the same-language Tier 2 commit both phones make from the text
// (tier §11.2, receiver §8.3): every slot with exactly one extracted value is
// written, clause by clause.
inline bool commit_extraction(itantra::Context& ctx, const itantra::UtteranceExtraction& extraction, u8 seq) {
    for (const itantra::ClauseExtraction& clause : extraction.clauses) {
        itantra::CommitPayload payload{};
        payload.seq = seq;
        for (u32 s = 0u; s < itantra::kConceptSlotCount; ++s) {
            if (clause.slots[s].state == itantra::SlotState::Value) {
                payload.slots[s].op    = itantra::SlotOp::Write;
                payload.slots[s].value = clause.slots[s].value;
            }
        }
        if (itantra::commit(ctx, payload) != itantra::CommitResult::Ok) return false;
    }
    return true;
}

// Tokens the trained table gives no mass at all: unigram 0 and in no bigram
// row. Their probability comes from the uniform floor (and a boost) alone.
inline std::vector<bool> untrained_tokens(const itantra::NgramTable& table) {
    std::vector<bool> untrained(table.vocab_size(), false);
    for (u32 s = 0u; s < table.vocab_size(); ++s) untrained[s] = table.unigrams()[s] == 0u;
    for (u32 i = 0u; i < table.row_count(); ++i) {
        u32                        lambda  = 0u;
        const itantra::NgramEntry* entries = nullptr;
        u32                        count   = 0u;
        table.row(table.row_history(i), lambda, entries, count);
        for (u32 k = 0u; k < count; ++k) untrained[entries[k].token] = false;
    }
    return untrained;
}

// A string of raw bytes.
inline std::string bytes(std::initializer_list<u32> list) {
    std::string s;
    for (u32 b : list) s.push_back(static_cast<char>(static_cast<u8>(b)));
    return s;
}

inline std::string repeat_byte(std::size_t n, u32 byte) {
    std::string s;
    for (std::size_t i = 0u; i < n; ++i) s.push_back(static_cast<char>(static_cast<u8>(byte)));
    return s;
}

}  // namespace t2fx
