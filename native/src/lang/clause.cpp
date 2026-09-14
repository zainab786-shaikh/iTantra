#include "lang/clause.h"

#include <algorithm>
#include <string>

#include "lang/utf8.h"

namespace itantra {

namespace {

bool is_clause_punctuation(const NormalizeRules& rules, u32 cp) {
    return std::binary_search(rules.clause_punctuation.begin(), rules.clause_punctuation.end(), cp);
}

bool is_conjunction(const NormalizeRules& rules, const u8* data, std::size_t begin, std::size_t end) {
    if (rules.clause_conjunctions.empty()) return false;
    std::string folded;
    for (std::size_t i = begin; i < end;) {
        u32 cp = 0u;
        bool valid = true;
        const u32 n = utf8::decode(data, end, i, cp, valid);
        if (!valid) return false;
        utf8::append(folded, unicode::simple_case_fold(cp));
        i += n;
    }
    return std::find(rules.clause_conjunctions.begin(), rules.clause_conjunctions.end(), folded) !=
           rules.clause_conjunctions.end();
}

bool has_content(const NormalizeRules& rules, const u8* data, std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end;) {
        u32 cp = 0u;
        bool valid = true;
        const u32 n = utf8::decode(data, end, i, cp, valid);
        if (!valid || (cp != 0x20u && !is_clause_punctuation(rules, cp))) return true;
        i += n;
    }
    return false;
}

}  // namespace

std::vector<ClauseSpan> segment_clauses(const MappedText& utterance, const NormalizeRules& rules) {
    const auto*       data = reinterpret_cast<const u8*>(utterance.text.data());
    const std::size_t n    = utterance.text.size();

    std::vector<std::size_t> cuts;
    bool        in_token    = false;
    std::size_t token_begin = 0u;

    std::size_t i = 0u;
    while (i <= n) {
        u32  cp        = 0x20u;
        bool valid     = true;
        u32  width     = 1u;
        bool separator = true;   // end of text acts as a separator
        if (i < n) {
            width     = utf8::decode(data, n, i, cp, valid);
            separator = valid && (cp == 0x20u || is_clause_punctuation(rules, cp));
        }

        if (!separator && !in_token) {
            in_token    = true;
            token_begin = i;
        } else if (separator && in_token) {
            in_token = false;
            if (token_begin > 0u && is_conjunction(rules, data, token_begin, i)) cuts.push_back(token_begin);
        }
        if (i < n && valid && cp != 0x20u && is_clause_punctuation(rules, cp)) cuts.push_back(i + width);

        if (i == n) break;
        i += width;
    }

    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    std::vector<ClauseSpan> clauses;
    bool       have_prefix = false;
    ClauseSpan prefix{0u, 0u};
    std::size_t start = 0u;
    for (std::size_t k = 0u; k <= cuts.size(); ++k) {
        std::size_t b = start;
        std::size_t e = k < cuts.size() ? cuts[k] : n;
        start = e;
        while (b < e && data[b] == 0x20u) ++b;
        while (e > b && data[e - 1u] == 0x20u) --e;
        if (b == e) continue;

        const ClauseSpan piece{static_cast<u32>(b), static_cast<u32>(e)};
        if (!has_content(rules, data, b, e)) {
            if (!clauses.empty()) {
                clauses.back().end = piece.end;
            } else if (have_prefix) {
                prefix.end = piece.end;
            } else {
                have_prefix = true;
                prefix      = piece;
            }
            continue;
        }
        ClauseSpan clause = piece;
        if (have_prefix && clauses.empty()) {
            clause.begin = prefix.begin;
            have_prefix  = false;
        }
        clauses.push_back(clause);
    }
    if (have_prefix && clauses.empty()) clauses.push_back(prefix);
    return clauses;
}

}  // namespace itantra
