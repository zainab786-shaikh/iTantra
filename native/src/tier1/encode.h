#pragma once

// Tier 1 encode — tier §5, §8.1; language §11.1, §11.2; packet §11.
// SENDER-ONLY. One clause in, a Tier 1 payload out — or the reason Tier 1 is
// not safe for it (every reason other than Ok means INTENT_NONE / Tier 2,
// §8.1; tier selection itself is Phase 9).
//
//   STT confidence < the sender pack's threshold      → LowConfidence  (§5.8, C-25)
//   more than one negation word                        → NegationAmbiguous
//   adjacency: a bare value answering the pending query → answer intent (§5.2)
//   otherwise head selection (§5.3) → rule table (§5.4)
//   negated clause whose rule has no NEG_SET           → NegationWithoutRule
//   slot resolution (§5.5–5.7)
//   priority = intents.bin is_alert OR manual override  (language §11.2; never lowered)
//   read-back with the sender's templates (§5.8)
//   frame → symbols → assemble() under the static Tier 1 model (§5.9)
//   self-check: the payload decodes to the same frame, negation and priority
//                                                       → else SelfCheckFailed
//                                                         (covers §8.1 "negation
//                                                         copies disagree")
//
// DECIDED in Phase 8 (tier spec implementation resolutions):
//   Negation   the packet's negation bit is the clause's negation flag. A negated
//              clause is permitted in Tier 1 only through a rule with a NEG_SET
//              condition — the negated meaning then lives in the INTENT (§5.4
//              "a different intent, not the same intent with a bit set"). A
//              negated clause matched by a rule that does not test NEG_SET would
//              render as its positive sentence: refused. Two or more negation
//              words are refused rather than interpreted.
//   hash       hash_present = 1 exactly when the frame has an Inherit or Ref
//              slot, with context_hash = wire_context_hash(context_hash) of the
//              sender's pre-message context (context §17.1).
//   language   Tier 1 metadata carries no language field (packet §3.2).
//
// Nothing here changes the context or the adjacency state: the result carries
// the commit payload (context §10), and the caller commits after choosing Tier 1.

#include <cstddef>
#include <string>
#include <vector>

#include "context/context.h"
#include "lang/extract.h"
#include "lang/pack.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "tier1/adjacency.h"
#include "tier1/frame.h"
#include "tier1/readback.h"
#include "tier1/rules.h"
#include "tier1/slots.h"
#include "tier2/tables.h"

namespace itantra {

struct Tier1Tables {
    const CommonPack*   common   = nullptr;
    const LanguagePack* pack     = nullptr;   // the SENDER's language
    const RuleTable*    rules    = nullptr;
    const Tier2Tables*  subwords = nullptr;   // literal subword coder
};

struct Tier1Request {
    const ClauseExtraction* clause          = nullptr;
    const u8*               input           = nullptr;   // the original utterance the clause indexes
    std::size_t             input_length    = 0u;
    i64                     stt_confidence  = 0;
    bool                    manual_critical = false;     // "Send as Critical"
    u8                      seq             = 0u;
    const Context*          context         = nullptr;   // the sender's PRE-message context
    const AdjacencyFsm*     adjacency       = nullptr;   // optional
    SenderPolicy            policy;
};

enum class Tier1Outcome : u8 {
    Ok,
    InvalidArgument,
    LowConfidence,
    NegationAmbiguous,
    NoHead,
    TwoTopClass,
    AmbiguousHead,
    NoRule,
    AmbiguousRuleSlot,
    NegationWithoutRule,
    AmbiguousSlot,
    UnrepresentableValue,
    ValueKindCollision,
    RequiredSlotMissing,
    LiteralNotPlaceable,
    LiteralTooLong,
    ReadbackUnrenderable,
    ReadbackUnrenderedSlot,    // R2: a transmitted slot the sender's template does not render
    ReadbackUnexplainedWord,   // R1: a spoken word the rendering does not account for
    ReadbackBelowBar,
    TooLong,
    SelfCheckFailed,
};

const char* tier1_outcome_name(Tier1Outcome outcome) noexcept;

struct Tier1Encoding {
    Tier1Outcome        outcome  = Tier1Outcome::InvalidArgument;
    u16                 head     = 0u;               // 0 for an adjacency answer
    bool                answered = false;            // intent came from the adjacency check
    bool                negated  = false;
    Priority            priority = Priority::Normal;
    Tier1Frame          frame;
    ReadbackResult      readback;
    std::vector<Symbol> symbols;
    std::vector<u8>     payload;                     // plaintext native payload (packet §2)
    u16                 metadata_bits = 0u;
    CommitPayload       commit{};                    // what the sender commits if Tier 1 is sent
};

Tier1Encoding tier1_encode(const Tier1Tables& tables, const Tier1Request& request);

}  // namespace itantra
