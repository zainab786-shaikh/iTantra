#include "tier2/tables.h"

namespace itantra {

namespace {

bool open(const PackFiles& files, const char* name, PackKind kind, const u8*& payload, std::size_t& length,
          std::string& error) {
    const std::vector<u8>* file = nullptr;
    for (const PackFile& f : files) {
        if (f.first == name) file = &f.second;
    }
    if (file == nullptr) {
        error = std::string("missing Tier 2 table ") + name;
        return false;
    }
    std::string why;
    if (!unwrap_container(*file, kind, payload, length, why)) {
        error = std::string(name) + ": " + why;
        return false;
    }
    return true;
}

}  // namespace

const std::vector<std::string>& Tier2Tables::file_names() {
    static const std::vector<std::string> names = {"subwords.bin", "ngram.bin", "boost.bin"};
    return names;
}

bool Tier2Tables::load(const PackFiles& files, std::string& error) {
    vocabulary_ = SubwordVocabulary{};
    ngram_      = NgramTable{};
    boost_      = BoostTable{};
    loaded_     = false;

    const u8*   payload = nullptr;
    std::size_t length  = 0u;
    std::string why;

    SubwordVocabulary vocabulary;
    if (!open(files, "subwords.bin", PackKind::Subwords, payload, length, error)) return false;
    if (!vocabulary.load(payload, length, why)) {
        error = "subwords.bin: " + why;
        return false;
    }

    NgramTable ngram;
    if (!open(files, "ngram.bin", PackKind::Ngram, payload, length, error)) return false;
    if (!ngram.load(payload, length, vocabulary.size(), why)) {
        error = "ngram.bin: " + why;
        return false;
    }

    BoostTable boost;
    if (!open(files, "boost.bin", PackKind::Boost, payload, length, error)) return false;
    if (!boost.load(payload, length, vocabulary.size(), why)) {
        error = "boost.bin: " + why;
        return false;
    }

    vocabulary_ = std::move(vocabulary);
    ngram_      = std::move(ngram);
    boost_      = std::move(boost);
    loaded_     = true;
    return true;
}

}  // namespace itantra
