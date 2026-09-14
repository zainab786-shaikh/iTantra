#pragma once

// Clause segmentation — language-layer-spec §6.1 step 4, context §7.
//
// Runs on step-3 text, where punctuation still survives. Sender-only, a fixed
// deterministic rule, driven entirely by the pack's normalize.json:
//
//   - a `clause_punctuation` codepoint ENDS a clause; it stays with the clause
//     it ends ("fire at north gate," | "send help")
//   - a token equal to a `clause_conjunctions` entry (case-folded) STARTS a new
//     clause; it stays with the clause it starts ("fire" | "and send help")
//   - a token is a maximal run of codepoints that are neither U+0020 nor clause
//     punctuation
//   - each clause is trimmed of spaces; a piece with no content (only spaces
//     and clause punctuation) is attached to the clause before it, or to the
//     clause after it if it comes first — so "Fire!!" stays one clause
//
// Clauses are returned in order and never overlap. Every non-space byte of
// the text belongs to exactly one clause.
//
// Known limit, recorded as a spec gap: conjunctions are whole tokens only;
// a conjunction written as a suffix (e.g. a clitic joined to a word) does not
// split a clause.

#include <vector>

#include "common/types.h"
#include "lang/normalize.h"

namespace itantra {

struct ClauseSpan {
    u32 begin;   // byte range in MappedText::text
    u32 end;
};

std::vector<ClauseSpan> segment_clauses(const MappedText& utterance, const NormalizeRules& rules);

}  // namespace itantra
