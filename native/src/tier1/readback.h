#pragma once

// Read-back check — tier §5.8, T9; language §11.1, §11.2. SENDER-ONLY.
//
//   build the frame → render it with the SENDER's own templates, inherited
//   values resolved → compare against what was actually said
//   Tier 1 is permitted only if EVERY check below holds; otherwise Tier 2
//
// "Comparison: word-level edit distance over the normalised token sequence,
// integer arithmetic."
//
// DECIDED in Phase 8 (tier spec implementation resolutions):
//
//   R2 rendered slots  Every non-Absent slot of the frame is a placeholder of
//                      the sender's template for the intent. A value that would
//                      be transmitted but never rendered — heard by nobody —
//                      refuses Tier 1. The pack compiler requires every
//                      language's template for an intent to use the same slots,
//                      all of them required (rulec::check_templates), so what the
//                      sender verified is rendered in every listener's language.
//   Normalised tokens  both sides through language §6.1 steps 1–3, 5, 6 with
//                      the sender pack's rules: the spoken clause is its
//                      match_text; the rendering goes through normalize_surface.
//                      Tokens are split on U+0020.
//   R1 coverage        Every spoken token appears among the rendered tokens,
//                      counted as a multiset (a word said twice must be rendered
//                      twice). The tokens of the unmatched words the transmitted
//                      literal was taken from are exempt. An unrecognised word is
//                      never treated as filler: an unlisted negation, a dropped
//                      quantity or any other unexplained spoken word refuses
//                      Tier 1, whatever the similarity.
//   Distance           Levenshtein over tokens: insertion, deletion and
//                      substitution each cost 1; tokens compare by bytes.
//   Similarity         per-mille: (L − d) · 1000 / L, floor, L = the longer
//                      token count; two empty sequences are 1000.
//   Bar                an additional guard: the sender pack's meta.json
//                      readback_critical for a CRITICAL message, readback_normal
//                      otherwise; similarity >= bar. CRITICAL = is_alert OR the
//                      manual override (language §11.2).

#include <string>

#include "common/types.h"
#include "context/context.h"
#include "lang/extract.h"
#include "lang/normalize.h"
#include "lang/pack.h"
#include "lang/render.h"
#include "packet/metadata.h"
#include "tier1/frame.h"

namespace itantra {

// Per-mille word similarity of two normalised strings.
u32 word_similarity_permille(const std::string& a, const std::string& b);

struct ReadbackResult {
    u8           unrendered_slots = 0u;                      // R2: transmitted slots without a placeholder
    bool         rendered         = false;
    RenderStatus render_status    = RenderStatus::NoTemplate;
    bool         covered          = false;                   // R1: every spoken token rendered
    std::string  unexplained;                                // R1: the first spoken token not rendered
    u32          similarity       = 0u;                      // per-mille
    u32          bar              = 0u;                      // per-mille
    bool         passed           = false;                   // all checks
    std::string  text;                                       // the sender-language rendering, as rendered
};

// `literal_span` is the original-byte range the frame's literal was taken from
// (tier1/slots.h literal_source_span), or null when the frame has no literal.
ReadbackResult readback_check(const LanguagePack& sender_pack, const CommonPack& common, const Tier1Frame& frame,
                              const Context& sender_context, const ClauseExtraction& clause, Priority priority,
                              const SourceSpan* literal_span);

}  // namespace itantra
