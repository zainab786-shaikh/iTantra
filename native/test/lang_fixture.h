#pragma once

// Shared helpers for the Phase 6 language tests: loading the compiled
// synthetic fixture packs, reading the fixture's TSV sources, and building
// strings from codepoints (test sources stay free of hand-typed script text
// where exact codepoints matter).

#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "lang/extract.h"
#include "lang/pack.h"
#include "lang/utf8.h"

namespace langfx {

using itantra::u16;
using itantra::u32;
using itantra::u8;

inline std::string cps(std::initializer_list<u32> list) {
    std::string s;
    for (u32 cp : list) itantra::utf8::append(s, cp);
    return s;
}

inline bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

// Non-empty, non-comment lines split on tabs.
inline std::vector<std::vector<std::string>> read_tsv(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    std::string text;
    if (!read_file(path, text)) return rows;
    std::size_t start = 0u;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1u;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::size_t s = 0u;
        for (;;) {
            const std::size_t tab = line.find('\t', s);
            cols.push_back(line.substr(s, tab == std::string::npos ? std::string::npos : tab - s));
            if (tab == std::string::npos) break;
            s = tab + 1u;
        }
        rows.push_back(std::move(cols));
    }
    return rows;
}

inline std::vector<std::string> split(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    std::size_t start = 0u;
    for (;;) {
        const std::size_t at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + sep.size();
    }
    return out;
}

inline const std::vector<std::string>& fixture_languages() {
    static const std::vector<std::string> languages = {"hi", "ta", "en"};
    return languages;
}

struct Fixture {
    bool                                        ok = false;
    std::string                                 error;
    std::string                                 packs_dir;
    std::string                                 src_dir;
    itantra::CommonPack                         common;
    std::map<std::string, itantra::LanguagePack> packs;
    std::map<std::string, u16>                  concepts;   // name → id
    std::map<std::string, u16>                  intents;    // name → id

    bool load(const std::string& packs_root, const std::string& source_root) {
        packs_dir = packs_root;
        src_dir   = source_root;
        itantra::PackFiles files;
        if (!itantra::read_pack_directory(packs_dir + "/common", itantra::CommonPack::file_names(), files, error) ||
            !common.load(std::move(files), error)) {
            return false;
        }
        for (const std::string& code : fixture_languages()) {
            itantra::PackFiles lang_files;
            itantra::LanguagePack pack;
            if (!itantra::read_pack_directory(packs_dir + "/lang/" + code, itantra::LanguagePack::file_names(),
                                              lang_files, error) ||
                !pack.load(std::move(lang_files), common, error)) {
                error = code + ": " + error;
                return false;
            }
            packs.emplace(code, std::move(pack));
        }
        for (const auto& row : read_tsv(src_dir + "/common/concepts.tsv")) {
            concepts[row.at(1)] = static_cast<u16>(std::stoul(row.at(0)));
        }
        for (const auto& row : read_tsv(src_dir + "/common/intents.tsv")) {
            intents[row.at(1)] = static_cast<u16>(std::stoul(row.at(0)));
        }
        ok = !concepts.empty() && !intents.empty();
        if (!ok) error = "fixture sources not found under " + src_dir;
        return ok;
    }

    const itantra::LanguagePack& pack(const std::string& code) const { return packs.at(code); }

    itantra::UtteranceExtraction extract(const std::string& code, const std::string& text) const {
        itantra::UtteranceExtraction out;
        itantra::extract_utterance(pack(code), common, reinterpret_cast<const u8*>(text.data()), text.size(), out);
        return out;
    }
};

}  // namespace langfx
