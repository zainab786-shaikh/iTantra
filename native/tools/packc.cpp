// Language pack compiler — language-layer-spec §4, Appendix B. Plan Phase 6.
// Tier 2 table compiler — tier §6.2–6.4. Plan Phase 7.
//
//   itantra-packc <source-dir> <output-dir> <language-code>...
//
// Turns human-reviewable pack sources into the runtime layout of lang/pack.h,
// then loads what it wrote with the runtime loader, so a pack that compiles is
// a pack that loads.
//
// Source layout (UTF-8; TSV = tab-separated, '#' at line start = comment):
//
//   common/schema_version     decimal integer
//   common/categories.tsv     bit  name
//   common/concepts.tsv       id  name  slot|-  class  categories|-
//   common/intents.tsv        id  name  expected|-  required|-  head_implied  is_alert
//   lang/<code>/meta.json     copied as-is after validation
//   lang/<code>/normalize.json copied as-is after validation
//   lang/<code>/lexicon.tsv   surface  concept  base|inflected  native|loanword|stt_variant
//   lang/<code>/numbers.tsv   surface  value
//   lang/<code>/patterns.tsv  slot  pattern  a  b  c  min  max    ({N} = a number)
//   lang/<code>/forms.tsv     concept  form  text
//   lang/<code>/templates.tsv intent  template
//   tier2/subwords.tsv        subword                 (optional directory, below)
//   tier2/train.tsv           language  text
//   tier2/params.tsv          key  value              (boost_magnitude)
//
// Surface forms, number words and pattern words are stored normalised exactly
// as matched text is (normalize_surface: §6.1 steps 1–3, 5, 6, with the pack's
// own normalize.json), so a lexicon entry and the spoken word compare equal
// whatever Unicode form either was written in (C-27). Forms and templates are
// stored in NFC.
//
// Names (concept, intent, category) exist only in the sources; the packs
// carry the numeric IDs, which are the wire contract (§7.4).
//
// Tier 2 tables — written to <output-dir>/tier2/ when <source-dir>/tier2/
// exists (tier2/tables.h). One set for all compiled languages. The runtime
// contract is only the three files and the tokenizer rule; how the vocabulary
// and counts are chosen is this compiler's (fixture) policy:
//
//   vocabulary   every tier2/subwords.tsv entry; every lexicon surface of every
//                compiled language in NFC, whole and each space-separated word;
//                every space-separated word of the training texts — the derived
//                ones kept only if they are valid subwords (2–64 bytes of UTF-8).
//                Sorted by byte value; token IDs 256… in that order.
//   n-gram       estimate_kneser_ney over the training texts (as written) and
//                the NFC lexicon surfaces, each tokenized by the runtime
//                tokenizer loaded from the subwords.bin just written.
//   boost        for every concept with a slot, the subword tokens (ID >= 256)
//                of all its NFC lexicon surfaces in every compiled language,
//                keyed (slot, concept ID); magnitude from params.tsv.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "common/types.h"
#include "lang/lexicon.h"
#include "lang/normalize.h"
#include "lang/pack.h"
#include "lang/utf8.h"
#include "tier2/tables.h"

using namespace itantra;

namespace {

[[noreturn]] void die(const std::string& message) {
    std::fprintf(stderr, "packc: %s\n", message.c_str());
    std::exit(1);
}

std::vector<u8> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) die("cannot read " + path);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::vector<u8>(raw.begin(), raw.end());
}

void write_bytes(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) die("cannot write " + path);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) die("write failed: " + path);
}

struct Row {
    std::vector<std::string> cols;
    std::string              where;
};

std::vector<Row> read_tsv(const std::string& path, std::size_t columns) {
    const std::vector<u8> bytes = read_bytes(path);
    std::string text(bytes.begin(), bytes.end());
    std::vector<Row> rows;
    std::size_t line_no = 0u;
    std::size_t start = 0u;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1u;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') {
            if (end == text.size()) break;
            continue;
        }
        Row row;
        row.where = path + ":" + std::to_string(line_no);
        std::size_t s = 0u;
        for (;;) {
            const std::size_t tab = line.find('\t', s);
            row.cols.push_back(line.substr(s, tab == std::string::npos ? std::string::npos : tab - s));
            if (tab == std::string::npos) break;
            s = tab + 1u;
        }
        if (row.cols.size() != columns) {
            die(row.where + ": expected " + std::to_string(columns) + " tab-separated columns, found " +
                std::to_string(row.cols.size()));
        }
        rows.push_back(std::move(row));
        if (end == text.size()) break;
    }
    return rows;
}

u64 parse_uint(const std::string& s, const std::string& where) {
    if (s.empty() || s.size() > 10u) die(where + ": expected an unsigned integer, got '" + s + "'");
    u64 v = 0u;
    for (char c : s) {
        if (c < '0' || c > '9') die(where + ": expected an unsigned integer, got '" + s + "'");
        v = v * 10u + static_cast<u64>(c - '0');
    }
    return v;
}

i32 parse_int(const std::string& s, const std::string& where) {
    if (!s.empty() && s[0] == '-') return -static_cast<i32>(parse_uint(s.substr(1u), where));
    const u64 v = parse_uint(s, where);
    if (v > 0x7FFFFFFFu) die(where + ": integer out of range");
    return static_cast<i32>(v);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty() || s == "-") return out;
    std::size_t start = 0u;
    for (;;) {
        const std::size_t at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1u;
    }
    return out;
}

u8 slot_mask(const std::string& list, const std::string& where) {
    u8 mask = 0u;
    for (const std::string& name : split(list, ',')) {
        u8 slot = 0u;
        if (!slot_from_name(name, slot)) die(where + ": unknown slot '" + name + "'");
        mask = static_cast<u8>(mask | (1u << slot));
    }
    return mask;
}

bool valid_utf8(const std::string& s) {
    const auto* d = reinterpret_cast<const u8*>(s.data());
    for (std::size_t i = 0u; i < s.size();) {
        u32 cp = 0u;
        bool ok = true;
        i += utf8::decode(d, s.size(), i, cp, ok);
        if (!ok) return false;
    }
    return true;
}

std::string surface(const std::string& raw, const NormalizeRules& rules, const std::string& where) {
    if (!valid_utf8(raw)) die(where + ": not valid UTF-8");
    std::string n = normalize_surface(reinterpret_cast<const u8*>(raw.data()), raw.size(), rules);
    if (n.empty()) die(where + ": '" + raw + "' normalises to nothing");
    return n;
}

struct Common {
    std::map<std::string, u16> concepts;
    std::map<std::string, u16> intents;
    std::map<u16, u8>          concept_slots;   // concept ID → slot, or kNoSlot
};

Common compile_common(const std::string& src, const std::string& out) {
    Common names;
    std::filesystem::create_directories(out + "/common");

    std::map<std::string, u32> categories;
    for (const Row& r : read_tsv(src + "/common/categories.tsv", 2u)) {
        const u64 bit = parse_uint(r.cols[0], r.where);
        if (bit > 31u || categories.count(r.cols[1]) != 0u) die(r.where + ": invalid or duplicate category");
        for (const auto& c : categories) {
            if (c.second == (u32{1} << bit)) die(r.where + ": duplicate category bit");
        }
        categories[r.cols[1]] = u32{1} << bit;
    }

    std::vector<ConceptInfo> concepts;
    std::set<u16> concept_ids;
    for (const Row& r : read_tsv(src + "/common/concepts.tsv", 5u)) {
        const u64 id = parse_uint(r.cols[0], r.where);
        if (id == 0u || id > 0xFFFFu || !concept_ids.insert(static_cast<u16>(id)).second) die(r.where + ": invalid or duplicate id");
        if (names.concepts.count(r.cols[1]) != 0u) die(r.where + ": duplicate concept name");
        u8 slot = kNoSlot;
        if (r.cols[2] != "-" && !slot_from_name(r.cols[2], slot)) die(r.where + ": unknown slot");
        static const std::map<std::string, ConceptClass> kClasses = {
            {"ACTION", ConceptClass::Action}, {"EVENT", ConceptClass::Event}, {"STATE", ConceptClass::State},
            {"ENTITY", ConceptClass::Entity}, {"MODIFIER", ConceptClass::Modifier}};
        const auto cls = kClasses.find(r.cols[3]);
        if (cls == kClasses.end()) die(r.where + ": unknown class");
        u32 mask = 0u;
        for (const std::string& c : split(r.cols[4], ',')) {
            const auto it = categories.find(c);
            if (it == categories.end()) die(r.where + ": unknown category '" + c + "'");
            mask |= it->second;
        }
        names.concepts[r.cols[1]] = static_cast<u16>(id);
        names.concept_slots[static_cast<u16>(id)] = slot;
        concepts.push_back(ConceptInfo{static_cast<u16>(id), slot, cls->second, mask});
    }

    std::vector<IntentInfo> intents;
    std::set<u16> intent_ids;
    for (const Row& r : read_tsv(src + "/common/intents.tsv", 6u)) {
        const u64 id = parse_uint(r.cols[0], r.where);
        if (id == 0u || id > 0xFFFFu || !intent_ids.insert(static_cast<u16>(id)).second) die(r.where + ": invalid or duplicate id");
        if (names.intents.count(r.cols[1]) != 0u) die(r.where + ": duplicate intent name");
        const u8 expected = slot_mask(r.cols[2], r.where);
        const u8 required = slot_mask(r.cols[3], r.where);
        if ((required & ~expected) != 0u) die(r.where + ": required slots must be expected");
        const u64 implied = parse_uint(r.cols[4], r.where);
        const u64 alert = parse_uint(r.cols[5], r.where);
        if (implied > 1u || alert > 1u) die(r.where + ": head_implied and is_alert are 0 or 1");
        names.intents[r.cols[1]] = static_cast<u16>(id);
        intents.push_back(IntentInfo{static_cast<u16>(id), expected, required, implied == 1u, alert == 1u});
    }

    write_bytes(out + "/common/concepts.bin", serialize_concepts(concepts));
    write_bytes(out + "/common/intents.bin", serialize_intents(intents));
    write_bytes(out + "/common/schema_version", read_bytes(src + "/common/schema_version"));
    return names;
}

void compile_language(const std::string& src, const std::string& out, const std::string& code, const Common& names) {
    const std::string in_dir = src + "/lang/" + code;
    const std::string out_dir = out + "/lang/" + code;
    std::filesystem::create_directories(out_dir);

    const std::vector<u8> normalize_json = read_bytes(in_dir + "/normalize.json");
    NormalizeRules rules;
    std::string why;
    if (!parse_normalize_rules(normalize_json.data(), normalize_json.size(), rules, why)) die(in_dir + "/" + why);

    auto concept_id = [&names](const Row& r, const std::string& name) {
        const auto it = names.concepts.find(name);
        if (it == names.concepts.end()) die(r.where + ": unknown concept '" + name + "'");
        return it->second;
    };

    // lexicon
    AutomatonBuilder lexicon;
    std::vector<std::vector<LexiconEntry>> lexicon_entries;
    for (const Row& r : read_tsv(in_dir + "/lexicon.tsv", 4u)) {
        const std::string s = surface(r.cols[0], rules, r.where);
        const u32 p = lexicon.add(s);
        if (p >= lexicon_entries.size()) lexicon_entries.resize(p + 1u);
        FormClass form_class = FormClass::Base;
        if (r.cols[2] == "inflected") form_class = FormClass::Inflected;
        else if (r.cols[2] != "base") die(r.where + ": form_class must be base or inflected");
        Origin origin = Origin::Native;
        if (r.cols[3] == "loanword") origin = Origin::Loanword;
        else if (r.cols[3] == "stt_variant") origin = Origin::SttVariant;
        else if (r.cols[3] != "native") die(r.where + ": origin must be native, loanword or stt_variant");
        const u16 id = concept_id(r, r.cols[1]);
        for (const LexiconEntry& e : lexicon_entries[p]) {
            if (e.concept_id == id) die(r.where + ": duplicate entry (same normalised surface and concept)");
        }
        lexicon_entries[p].push_back(LexiconEntry{id, form_class, origin});
    }

    // numbers
    AutomatonBuilder numbers;
    std::vector<u32> values;
    for (const Row& r : read_tsv(in_dir + "/numbers.tsv", 2u)) {
        const std::string s = surface(r.cols[0], rules, r.where);
        if (s.find_first_not_of("0123456789") == std::string::npos) die(r.where + ": digit runs are matched by code, not listed");
        const u64 v = parse_uint(r.cols[1], r.where);
        if (v > 999999999u) die(r.where + ": value out of range");
        const u32 p = numbers.add(s);
        if (p < values.size()) {
            if (values[p] != v) die(r.where + ": the same number word with two values");
            continue;
        }
        values.push_back(static_cast<u32>(v));
    }

    // patterns
    std::vector<ValuePattern> patterns;
    for (const Row& r : read_tsv(in_dir + "/patterns.tsv", 7u)) {
        ValuePattern vp{};
        if (!slot_from_name(r.cols[0], vp.slot)) die(r.where + ": unknown slot");
        for (const std::string& token : split(r.cols[1], ' ')) {
            if (token == "{N}") {
                vp.elements.push_back(PatternElement{PatternElementKind::Number, std::string()});
            } else {
                const std::string w = surface(token, rules, r.where);
                if (w.find(' ') != std::string::npos) die(r.where + ": pattern word '" + token + "' is not one token");
                vp.elements.push_back(PatternElement{PatternElementKind::Word, w});
            }
        }
        vp.a = parse_int(r.cols[2], r.where);
        vp.b = parse_int(r.cols[3], r.where);
        vp.c = parse_int(r.cols[4], r.where);
        vp.min = static_cast<u32>(parse_uint(r.cols[5], r.where));
        vp.max = static_cast<u32>(parse_uint(r.cols[6], r.where));
        patterns.push_back(std::move(vp));
    }

    // forms
    std::vector<FormSource> forms;
    std::set<std::pair<u16, std::string>> seen_forms;
    for (const Row& r : read_tsv(in_dir + "/forms.tsv", 3u)) {
        if (!valid_utf8(r.cols[2]) || r.cols[2].empty()) die(r.where + ": invalid form text");
        const u16 id = concept_id(r, r.cols[0]);
        if (!seen_forms.insert(std::make_pair(id, r.cols[1])).second) die(r.where + ": duplicate form");
        forms.push_back(FormSource{id, r.cols[1], unicode::nfc(r.cols[2])});
    }

    // templates
    std::vector<TemplateSource> templates;
    std::set<u16> seen_intents;
    for (const Row& r : read_tsv(in_dir + "/templates.tsv", 2u)) {
        const auto it = names.intents.find(r.cols[0]);
        if (it == names.intents.end()) die(r.where + ": unknown intent '" + r.cols[0] + "'");
        if (!seen_intents.insert(it->second).second) die(r.where + ": duplicate template");
        if (!valid_utf8(r.cols[1])) die(r.where + ": invalid template text");
        templates.push_back(TemplateSource{it->second, unicode::nfc(r.cols[1])});
    }

    write_bytes(out_dir + "/meta.json", read_bytes(in_dir + "/meta.json"));
    write_bytes(out_dir + "/normalize.json", normalize_json);
    write_bytes(out_dir + "/lexicon.bin", serialize_lexicon(lexicon, lexicon_entries));
    write_bytes(out_dir + "/numbers.bin", serialize_numbers(numbers, values));
    write_bytes(out_dir + "/patterns.bin", serialize_patterns(patterns));
    write_bytes(out_dir + "/forms.bin", serialize_forms(forms));
    write_bytes(out_dir + "/templates.bin", serialize_templates(templates));
}

struct LexiconSurface {
    std::string text;         // NFC, as written in lexicon.tsv
    u16         concept_id;
    u8          slot;
};

void compile_tier2(const std::string& src, const std::string& out, const std::vector<std::string>& languages,
                   const Common& names) {
    const std::string in_dir = src + "/tier2";
    const std::string out_dir = out + "/tier2";
    std::filesystem::create_directories(out_dir);

    // ---- vocabulary ----
    std::set<std::string> vocabulary;
    for (const Row& r : read_tsv(in_dir + "/subwords.tsv", 1u)) {
        const std::string& s = r.cols[0];
        if (const char* why = subword_fault(s)) die(r.where + ": " + why);
        if (s.front() == ' ' || s.back() == ' ') die(r.where + ": leading or trailing space in a subword");
        if (!vocabulary.insert(s).second) die(r.where + ": duplicate subword");
    }
    const auto offer = [&vocabulary](const std::string& s) {
        if (subword_fault(s) == nullptr) vocabulary.insert(s);
    };

    std::vector<LexiconSurface> surfaces;
    for (const std::string& code : languages) {
        for (const Row& r : read_tsv(src + "/lang/" + code + "/lexicon.tsv", 4u)) {
            const auto it = names.concepts.find(r.cols[1]);
            if (it == names.concepts.end()) die(r.where + ": unknown concept '" + r.cols[1] + "'");
            surfaces.push_back(LexiconSurface{unicode::nfc(r.cols[0]), it->second, names.concept_slots.at(it->second)});
        }
    }

    std::vector<std::string> training;
    for (const Row& r : read_tsv(in_dir + "/train.tsv", 2u)) {
        if (std::find(languages.begin(), languages.end(), r.cols[0]) == languages.end()) {
            die(r.where + ": language '" + r.cols[0] + "' is not being compiled");
        }
        if (r.cols[1].empty() || !valid_utf8(r.cols[1])) die(r.where + ": training text must be non-empty UTF-8");
        training.push_back(r.cols[1]);
    }
    if (training.empty()) die(in_dir + "/train.tsv: no training texts");

    for (const LexiconSurface& s : surfaces) {
        offer(s.text);
        for (const std::string& w : split(s.text, ' ')) offer(w);
    }
    for (const std::string& t : training) {
        for (const std::string& w : split(t, ' ')) offer(w);
    }
    const std::vector<std::string> subwords(vocabulary.begin(), vocabulary.end());
    if (subwords.size() > kMaxVocabularySize - kByteTokenCount) die("tier2: vocabulary larger than 65536 tokens");
    write_bytes(out_dir + "/subwords.bin", serialize_subwords(subwords));

    // Tokenize with the vocabulary exactly as the runtime loads it.
    SubwordVocabulary vocab;
    {
        const std::vector<u8> file = read_bytes(out_dir + "/subwords.bin");
        const u8*   payload = nullptr;
        std::size_t length  = 0u;
        std::string why;
        if (!unwrap_container(file, PackKind::Subwords, payload, length, why) || !vocab.load(payload, length, why)) {
            die("tier2: subwords.bin does not load: " + why);
        }
    }
    const auto tokens_of = [&vocab](const std::string& text) {
        std::vector<Symbol> t;
        vocab.tokenize(reinterpret_cast<const u8*>(text.data()), text.size(), t);
        return t;
    };

    // ---- n-gram table ----
    std::vector<std::vector<Symbol>> sequences;
    for (const std::string& t : training) sequences.push_back(tokens_of(t));
    for (const LexiconSurface& s : surfaces) sequences.push_back(tokens_of(s.text));
    NgramSource ngram;
    std::string why;
    if (!estimate_kneser_ney(vocab.size(), sequences, ngram, why)) die("tier2: " + why);
    write_bytes(out_dir + "/ngram.bin", serialize_ngram(ngram));

    // ---- boost table ----
    u64  magnitude      = 0u;
    bool have_magnitude = false;
    for (const Row& r : read_tsv(in_dir + "/params.tsv", 2u)) {
        if (r.cols[0] != "boost_magnitude") die(r.where + ": unknown parameter '" + r.cols[0] + "'");
        magnitude      = parse_uint(r.cols[1], r.where);
        have_magnitude = true;
    }
    if (!have_magnitude || magnitude == 0u || magnitude > 0xFFFFFFFFu) {
        die(in_dir + "/params.tsv: boost_magnitude is required, at least 1");
    }

    std::map<std::pair<u8, u16>, std::set<u32>> boost_sets;
    for (const LexiconSurface& s : surfaces) {
        if (s.slot == kNoSlot) continue;
        std::set<u32>& set = boost_sets[std::make_pair(s.slot, s.concept_id)];
        for (Symbol t : tokens_of(s.text)) {
            if (t >= kByteTokenCount) set.insert(t);
        }
    }
    BoostSource boost;
    boost.vocab_size = vocab.size();
    boost.magnitude  = static_cast<u32>(magnitude);
    for (const auto& kv : boost_sets) {
        if (kv.second.empty()) continue;
        boost.entries.push_back(
            BoostSource::Entry{kv.first.first, kv.first.second, std::vector<u32>(kv.second.begin(), kv.second.end())});
    }
    write_bytes(out_dir + "/boost.bin", serialize_boost(boost));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: itantra-packc <source-dir> <output-dir> <language-code>...\n");
        return 2;
    }
    const std::string src = argv[1];
    const std::string out = argv[2];
    const Common names = compile_common(src, out);

    // Self-check: what was written must load with the runtime loader.
    std::string error;
    PackFiles common_files;
    CommonPack common;
    if (!read_pack_directory(out + "/common", CommonPack::file_names(), common_files, error) ||
        !common.load(std::move(common_files), error)) {
        die("compiled common data does not load: " + error);
    }
    std::vector<std::string> languages;
    for (int i = 3; i < argc; ++i) {
        const std::string code = argv[i];
        languages.push_back(code);
        compile_language(src, out, code, names);
        PackFiles files;
        LanguagePack pack;
        if (!read_pack_directory(out + "/lang/" + code, LanguagePack::file_names(), files, error) ||
            !pack.load(std::move(files), common, error)) {
            die("compiled pack '" + code + "' does not load: " + error);
        }
        if (pack.language() != code) die("pack directory '" + code + "' declares language '" + pack.language() + "'");
        std::printf("packc: %s  %u lexicon surfaces, %u number words, %zu patterns\n", code.c_str(),
                    pack.lexicon().pattern_count(), pack.numbers().pattern_count(), pack.patterns().size());
    }
    std::printf("packc: common  %zu concepts, %zu intents, schema %u\n", common.concepts().size(),
                common.intents().size(), common.schema_version());

    if (std::filesystem::is_directory(src + "/tier2")) {
        compile_tier2(src, out, languages, names);
        PackFiles tier2_files;
        Tier2Tables tables;
        if (!read_pack_directory(out + "/tier2", Tier2Tables::file_names(), tier2_files, error) ||
            !tables.load(tier2_files, error)) {
            die("compiled Tier 2 tables do not load: " + error);
        }
        std::printf("packc: tier2  %u tokens (%u subwords), %u n-gram rows, %u boost entries, boost magnitude %u\n",
                    tables.vocabulary().size(), tables.vocabulary().size() - kByteTokenCount,
                    tables.ngram().row_count(), tables.boost().entry_count(), tables.boost().magnitude());
    }
    return 0;
}
