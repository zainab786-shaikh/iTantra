#pragma once

// Tier 2 context commit — tier §11.2, §11.3; receiver §8.3; context §10, §20 D2/D3.
// SHARED: the sender commits what it sent, the receiver what it decoded, through
// this ONE function over the SAME text with the SAME extractor, so both phones
// build the identical payload for commit() (context/context.h).
//
// Only for same-language sessions. When the phones' languages differ, BOTH skip
// the update (tier §11.3); that decision is the caller's.
//
// DECIDED in Phase 10 (tier spec implementation resolutions): one message is one
// commit, whatever number of clauses the extractor finds in the text. For each
// slot ACTOR … STATE:
//   exactly one distinct non-zero Value across all clauses, and no clause
//   Ambiguous there                                   → Write(value)
//   anything else (none, Ambiguous, two values, 0)    → Absent
// LAST_REF is never written. Two values for one slot are not guessed between
// (context §8.2, "multiple inheritable values per slot" rejected).

#include <cstddef>

#include "context/context.h"
#include "lang/extract.h"
#include "lang/pack.h"

namespace itantra {

CommitPayload tier2_text_commit(const UtteranceExtraction& extraction, u8 seq) noexcept;

// Extracts `text` with `pack` (the language both phones share), then the above.
CommitPayload tier2_text_commit(const LanguagePack& pack, const CommonPack& common, const u8* text, std::size_t length,
                                u8 seq);

}  // namespace itantra
