#pragma once

// Build-time rule compiler — tier §12.1, T7. Implementation plan Phase 8.
//
// NOT part of the runtime library (not in sources.cmake): it is linked only
// into the host tools (itantra-packc, itantra-rulec) and the tests. A malformed
// table fails the build, never a phone at runtime (§12.1 "Enforce in the rule
// compiler, not at runtime").
//
// Sources (UTF-8 TSV, '#' comments):
//
//   <common>/categories.tsv    bit  name                (as packc)
//   <common>/concepts.tsv      id  name  slot|-  class  categories|-
//   <common>/intents.tsv       id  name  expected|-  required|-  head_implied  is_alert
//   rules.tsv                  head  rule_priority  intent  conditions
//   answers.tsv                query_intent  slot  answer_intent
//
//   conditions  "-", or up to four separated by ";":
//                 NEG_SET · NEG_CLEAR
//                 SLOT PRESENT · SLOT ABSENT
//                 SLOT EQ concept
//                 SLOT IN_CATEGORY category[,category…]   (category bits 0 … 15)
//
// Checks — any failure is a compile error naming the row:
//
//   §12.1 1  no two rules in a bucket share a rule_priority
//   §12.1 2  every intent is reachable by at least one rule (or answer)
//   §12.1 3  every rule's conditions reference slots its intent declares
//   §12.1 4  every ACTION / EVENT / STATE concept has at least one rule
//   plus     names resolve; rule_priority 0 … 255; at most four conditions;
//            EQ names a concept of that slot; a head_implied = 0 intent
//            expects its head's slot (§5.5 "head sent in its slot"); a
//            head_implied = 1 intent does not require its head's slot (it would
//            never be sent); an answer intent expects its answer slot.
//
// §12.1 item 5 (coverage report: corpus clauses reaching INTENT_NONE) needs the
// corpus and the encoder; it is produced by conformance.c07.

#include <string>
#include <utility>
#include <vector>

#include "common/types.h"
#include "lang/pack.h"

namespace itantra {
namespace rulec {

struct CompileResult {
    bool            ok = false;
    std::string     error;          // first failure, "file:line: why"
    std::vector<u8> rules_bin;      // a complete sender/rules.bin container
    u32             rule_count   = 0u;
    u32             answer_count = 0u;
};

CompileResult compile(const std::string& common_dir, const std::string& rules_tsv, const std::string& answers_tsv);

// Template authoring check — read-back safety R2 (tier1/readback.h), Phase 8.
// Run by itantra-packc over every language compiled together. For every intent:
//
//   - each language's template references only slots the intent REQUIRES (an
//     optional slot may be absent, and a template placeholder for it could not
//     render then), and
//   - every language that has a template for the intent references the SAME
//     slots,
//
// so a value the sender's read-back saw rendered is rendered in every listener's
// language. False with the first violation in `error`.
bool check_templates(const CommonPack& common, const std::vector<std::pair<std::string, const LanguagePack*>>& packs,
                     std::string& error);

}  // namespace rulec
}  // namespace itantra
