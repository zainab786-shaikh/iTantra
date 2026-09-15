#include "lang/normalize.h"

#include <algorithm>

#include "lang/json.h"
#include "lang/unicode_tables.h"
#include "lang/utf8.h"

namespace itantra {

namespace {

// A codepoint — or an invalid input byte, flagged — with the input bytes it
// came from.
constexpr u32 kInvalidByte = 0x80000000u;

struct Unit {
    u32 cp;
    u32 begin;
    u32 end;
};

bool is_invalid(u32 cp) noexcept {
    return (cp & kInvalidByte) != 0u;
}

// Hangul syllable composition (The Unicode Standard §3.12).
constexpr u32 kSBase = 0xAC00u;
constexpr u32 kLBase = 0x1100u;
constexpr u32 kVBase = 0x1161u;
constexpr u32 kTBase = 0x11A7u;
constexpr u32 kLCount = 19u;
constexpr u32 kVCount = 21u;
constexpr u32 kTCount = 28u;
constexpr u32 kNCount = kVCount * kTCount;
constexpr u32 kSCount = kLCount * kNCount;

template <typename T, std::size_t N>
const T* find_range(const T (&table)[N], u32 cp) noexcept {
    std::size_t lo = 0u;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        if (table[mid].last < cp) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (lo < N && table[lo].first <= cp && cp <= table[lo].last) ? &table[lo] : nullptr;
}

template <typename T, std::size_t N>
const T* find_cp(const T (&table)[N], u32 cp) noexcept {
    std::size_t lo = 0u;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        if (table[mid].cp < cp) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (lo < N && table[lo].cp == cp) ? &table[lo] : nullptr;
}

template <std::size_t N>
bool find_composition(const ucd::Composition (&table)[N], u32 a, u32 b, u32& out) noexcept {
    std::size_t lo = 0u;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        if (table[mid].first < a || (table[mid].first == a && table[mid].second < b)) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < N && table[lo].first == a && table[lo].second == b) {
        out = table[lo].composite;
        return true;
    }
    return false;
}

bool compose_pair(u32 a, u32 b, u32& out) noexcept {
    if (a >= kLBase && a < kLBase + kLCount && b >= kVBase && b < kVBase + kVCount) {
        out = kSBase + ((a - kLBase) * kVCount + (b - kVBase)) * kTCount;
        return true;
    }
    if (a >= kSBase && a < kSBase + kSCount && (a - kSBase) % kTCount == 0u && b > kTBase &&
        b < kTBase + kTCount) {
        out = a + (b - kTBase);
        return true;
    }
    return find_composition(ucd::kComposition, a, b, out);
}

void decompose_into(u32 cp, u32 begin, u32 end, std::vector<Unit>& out) {
    if (!is_invalid(cp)) {
        if (cp >= kSBase && cp < kSBase + kSCount) {
            const u32 s = cp - kSBase;
            out.push_back(Unit{kLBase + s / kNCount, begin, end});
            out.push_back(Unit{kVBase + (s % kNCount) / kTCount, begin, end});
            if (s % kTCount != 0u) out.push_back(Unit{kTBase + s % kTCount, begin, end});
            return;
        }
        if (const ucd::Decomposition* d = find_cp(ucd::kDecomposition, cp)) {
            decompose_into(d->first, begin, end, out);
            if (d->second != 0u) decompose_into(d->second, begin, end, out);
            return;
        }
    }
    out.push_back(Unit{cp, begin, end});
}

u32 class_of(u32 cp) noexcept {
    return is_invalid(cp) ? 0u : unicode::combining_class(cp);
}

// UAX #15 NFC: full canonical decomposition, canonical ordering, canonical
// composition. A composed unit covers the input bytes of everything it was
// composed from.
void to_nfc(std::vector<Unit>& units) {
    std::vector<Unit> d;
    d.reserve(units.size() + 8u);
    for (const Unit& u : units) decompose_into(u.cp, u.begin, u.end, d);

    for (std::size_t i = 0u; i < d.size();) {
        if (class_of(d[i].cp) == 0u) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < d.size() && class_of(d[j].cp) != 0u) ++j;
        std::stable_sort(d.begin() + static_cast<std::ptrdiff_t>(i), d.begin() + static_cast<std::ptrdiff_t>(j),
                         [](const Unit& a, const Unit& b) { return class_of(a.cp) < class_of(b.cp); });
        i = j;
    }

    std::vector<Unit> out;
    out.reserve(d.size());
    bool        has_starter = false;
    std::size_t starter     = 0u;
    u32         last_class  = 0u;   // class of the last unit appended to out
    for (const Unit& u : d) {
        const u32 cls = class_of(u.cp);
        if (has_starter && !is_invalid(u.cp) && !is_invalid(out[starter].cp)) {
            // Not blocked: nothing between the starter and u, or everything
            // between has a lower, non-zero class (canonical order makes the
            // last one the highest).
            const bool adjacent  = out.size() - 1u == starter;
            const bool unblocked = adjacent || (last_class != 0u && last_class < cls);
            u32 composite = 0u;
            if (unblocked && compose_pair(out[starter].cp, u.cp, composite)) {
                Unit& s = out[starter];
                s.cp    = composite;
                s.begin = std::min(s.begin, u.begin);
                s.end   = std::max(s.end, u.end);
                continue;
            }
        }
        if (cls == 0u) {
            has_starter = true;
            starter     = out.size();
        }
        last_class = cls;
        out.push_back(u);
    }
    units.swap(out);
}

std::vector<Unit> decode_units(const u8* data, std::size_t length) {
    std::vector<Unit> units;
    units.reserve(length);
    std::size_t i = 0u;
    while (i < length) {
        u32 cp = 0u;
        bool valid = true;
        const u32 n = utf8::decode(data, length, i, cp, valid);
        units.push_back(Unit{valid ? cp : (kInvalidByte | cp), static_cast<u32>(i), static_cast<u32>(i + n)});
        i += n;
    }
    return units;
}

std::vector<Unit> decode_mapped(const MappedText& t, u32 begin, u32 end) {
    std::vector<Unit> units;
    const auto* data = reinterpret_cast<const u8*>(t.text.data());
    std::size_t i = begin;
    while (i < end) {
        u32 cp = 0u;
        bool valid = true;
        const u32 n = utf8::decode(data, end, i, cp, valid);
        u32 b = t.source[i].begin;
        u32 e = t.source[i].end;
        for (u32 k = 1u; k < n; ++k) {
            b = std::min(b, t.source[i + k].begin);
            e = std::max(e, t.source[i + k].end);
        }
        units.push_back(Unit{valid ? cp : (kInvalidByte | cp), b, e});
        i += n;
    }
    return units;
}

// §6.1 step 3: each White_Space run → one U+0020; leading and trailing removed.
void collapse_white_space(std::vector<Unit>& units) {
    std::vector<Unit> out;
    out.reserve(units.size());
    bool pending = false;
    Unit space{0x20u, 0u, 0u};
    for (const Unit& u : units) {
        if (!is_invalid(u.cp) && unicode::is_white_space(u.cp)) {
            if (out.empty()) continue;
            if (!pending) {
                pending = true;
                space   = Unit{0x20u, u.begin, u.end};
            } else {
                space.begin = std::min(space.begin, u.begin);
                space.end   = std::max(space.end, u.end);
            }
            continue;
        }
        if (pending) {
            out.push_back(space);
            pending = false;
        }
        out.push_back(u);
    }
    units.swap(out);
}

MappedText encode(const std::vector<Unit>& units) {
    MappedText out;
    out.text.reserve(units.size());
    out.source.reserve(units.size());
    for (const Unit& u : units) {
        const std::size_t before = out.text.size();
        if (is_invalid(u.cp)) {
            out.text.push_back(static_cast<char>(u.cp & 0xFFu));
        } else {
            utf8::append(out.text, u.cp);
        }
        for (std::size_t k = before; k < out.text.size(); ++k) out.source.push_back(SourceSpan{u.begin, u.end});
    }
    return out;
}

bool single_codepoint(const std::string& s, u32& cp) {
    if (s.empty()) return false;
    bool valid = true;
    const u32 n = utf8::decode(reinterpret_cast<const u8*>(s.data()), s.size(), 0u, cp, valid);
    return valid && n == s.size();
}

bool string_array(const JsonValue* v, const char* key, std::string& error) {
    if (v == nullptr || v->kind != JsonValue::Kind::Array) {
        error = std::string("normalize.json: \"") + key + "\" must be an array";
        return false;
    }
    for (const JsonValue& item : v->items) {
        if (item.kind != JsonValue::Kind::String) {
            error = std::string("normalize.json: \"") + key + "\" must contain only strings";
            return false;
        }
    }
    return true;
}

bool sorted_unique(std::vector<u32>& cps) {
    std::sort(cps.begin(), cps.end());
    return std::adjacent_find(cps.begin(), cps.end()) == cps.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// Unicode primitives
// ---------------------------------------------------------------------------

namespace unicode {

u8 combining_class(u32 cp) noexcept {
    const ucd::CccRange* r = find_range(ucd::kCombiningClass, cp);
    return r != nullptr ? r->ccc : u8{0};
}

bool is_white_space(u32 cp) noexcept {
    return find_range(ucd::kWhiteSpace, cp) != nullptr;
}

bool is_punctuation(u32 cp) noexcept {
    return find_range(ucd::kPunctuation, cp) != nullptr;
}

u32 simple_case_fold(u32 cp) noexcept {
    const ucd::CodeMapping* m = find_cp(ucd::kSimpleCaseFolding, cp);
    return m != nullptr ? m->to : cp;
}

std::string nfc(const std::string& text) {
    std::vector<Unit> units = decode_units(reinterpret_cast<const u8*>(text.data()), text.size());
    to_nfc(units);
    return encode(units).text;
}

std::string nfd(const std::string& text) {
    const std::vector<Unit> units = decode_units(reinterpret_cast<const u8*>(text.data()), text.size());
    std::vector<Unit> d;
    for (const Unit& u : units) decompose_into(u.cp, u.begin, u.end, d);
    for (std::size_t i = 0u; i < d.size();) {
        if (class_of(d[i].cp) == 0u) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < d.size() && class_of(d[j].cp) != 0u) ++j;
        std::stable_sort(d.begin() + static_cast<std::ptrdiff_t>(i), d.begin() + static_cast<std::ptrdiff_t>(j),
                         [](const Unit& a, const Unit& b) { return class_of(a.cp) < class_of(b.cp); });
        i = j;
    }
    return encode(d).text;
}

}  // namespace unicode

// ---------------------------------------------------------------------------
// MappedText
// ---------------------------------------------------------------------------

SourceSpan MappedText::source_of(u32 begin, u32 end) const noexcept {
    if (begin >= end || end > source.size()) return SourceSpan{0u, 0u};
    SourceSpan s = source[begin];
    for (u32 i = begin + 1u; i < end; ++i) {
        s.begin = std::min(s.begin, source[i].begin);
        s.end   = std::max(s.end, source[i].end);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool parse_normalize_rules(const u8* json, std::size_t length, NormalizeRules& out, std::string& error) {
    out = NormalizeRules{};
    JsonValue root;
    if (!parse_json(json, length, root, error)) {
        error = "normalize.json: " + error;
        return false;
    }
    if (root.kind != JsonValue::Kind::Object) {
        error = "normalize.json: top level must be an object";
        return false;
    }
    static const char* const kKeys[] = {"digit_sets", "strip_codepoints", "clause_punctuation", "clause_conjunctions"};
    for (const std::string& key : root.keys) {
        bool known = false;
        for (const char* k : kKeys) known = known || key == k;
        if (!known) {
            error = "normalize.json: unknown key \"" + key + "\"";
            return false;
        }
    }
    for (const char* k : kKeys) {
        if (!string_array(root.member(k), k, error)) return false;
    }

    std::vector<u32> digit_cps;
    for (const JsonValue& set : root.member("digit_sets")->items) {
        std::vector<u32> cps;
        const auto* data = reinterpret_cast<const u8*>(set.string.data());
        for (std::size_t i = 0u; i < set.string.size();) {
            u32 cp = 0u;
            bool valid = true;
            i += utf8::decode(data, set.string.size(), i, cp, valid);
            if (!valid) {
                error = "normalize.json: digit_sets entry is not valid UTF-8";
                return false;
            }
            cps.push_back(cp);
        }
        if (cps.size() != 10u) {
            error = "normalize.json: each digit_sets entry must be exactly ten codepoints";
            return false;
        }
        if (out.primary_digits.empty()) out.primary_digits = cps;
        for (u32 d = 0u; d < 10u; ++d) {
            out.digits.emplace_back(cps[d], static_cast<u8>(d));
            digit_cps.push_back(cps[d]);
        }
    }
    if (!sorted_unique(digit_cps)) {
        error = "normalize.json: a digit codepoint appears twice";
        return false;
    }
    std::sort(out.digits.begin(), out.digits.end());

    struct CpList {
        const char*       key;
        std::vector<u32>* into;
    };
    const CpList lists[] = {{"strip_codepoints", &out.strip}, {"clause_punctuation", &out.clause_punctuation}};
    for (const CpList& list : lists) {
        for (const JsonValue& item : root.member(list.key)->items) {
            u32 cp = 0u;
            if (!single_codepoint(item.string, cp)) {
                error = std::string("normalize.json: \"") + list.key + "\" entries must be single codepoints";
                return false;
            }
            list.into->push_back(cp);
        }
        if (!sorted_unique(*list.into)) {
            error = std::string("normalize.json: duplicate entry in \"") + list.key + "\"";
            return false;
        }
    }

    // Conjunctions are compared with case-folded tokens of step-3 text, so
    // they are stored the same way: steps 1–3, then case folding.
    for (const JsonValue& item : root.member("clause_conjunctions")->items) {
        const MappedText n = normalize_utterance(reinterpret_cast<const u8*>(item.string.data()), item.string.size(), out);
        std::string folded;
        std::vector<Unit> units = decode_units(reinterpret_cast<const u8*>(n.text.data()), n.text.size());
        for (const Unit& u : units) {
            if (is_invalid(u.cp) || u.cp == 0x20u) {
                error = "normalize.json: a clause_conjunctions entry must be one valid token";
                return false;
            }
            utf8::append(folded, unicode::simple_case_fold(u.cp));
        }
        if (folded.empty()) {
            error = "normalize.json: empty clause_conjunctions entry";
            return false;
        }
        out.clause_conjunctions.push_back(folded);
    }
    return true;
}

// ---------------------------------------------------------------------------
// §6.1
// ---------------------------------------------------------------------------

MappedText normalize_utterance(const u8* input, std::size_t length, const NormalizeRules& rules) {
    if (input == nullptr) length = 0u;
    std::vector<Unit> units = decode_units(input, length);

    // 1  NFC, then the pack's strip rules
    to_nfc(units);
    if (!rules.strip.empty()) {
        units.erase(std::remove_if(units.begin(), units.end(),
                                   [&rules](const Unit& u) {
                                       return !is_invalid(u.cp) &&
                                              std::binary_search(rules.strip.begin(), rules.strip.end(), u.cp);
                                   }),
                    units.end());
    }

    // 2  digit unification
    for (Unit& u : units) {
        if (is_invalid(u.cp)) continue;
        const auto it = std::lower_bound(rules.digits.begin(), rules.digits.end(), std::make_pair(u.cp, u8{0}));
        if (it != rules.digits.end() && it->first == u.cp) u.cp = u32{'0'} + it->second;
    }

    // 3  whitespace collapse
    collapse_white_space(units);
    return encode(units);
}

MappedText normalize_clause(const MappedText& utterance, u32 begin, u32 end) {
    const u32 size = static_cast<u32>(utterance.text.size());
    if (end > size) end = size;
    if (begin > end) begin = end;
    std::vector<Unit> units = decode_mapped(utterance, begin, end);

    for (Unit& u : units) {
        if (is_invalid(u.cp)) continue;
        u.cp = unicode::simple_case_fold(u.cp);           // 5  case folding
        if (unicode::is_punctuation(u.cp)) u.cp = 0x20u;  // 6  punctuation stripping
    }
    collapse_white_space(units);
    return encode(units);
}

std::string normalize_surface(const u8* input, std::size_t length, const NormalizeRules& rules) {
    const MappedText u = normalize_utterance(input, length, rules);
    return normalize_clause(u, 0u, static_cast<u32>(u.text.size())).text;
}

}  // namespace itantra
