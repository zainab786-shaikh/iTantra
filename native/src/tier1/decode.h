#pragma once

// Tier 1 decode — receiver §3 ④ ⑦ ⑧ ⑨, §4, §6.1, §7; tier §5.9, §10.
// SHARED: the receiver's path, and the sender's self-check. Includes nothing
// sender-only (no rule table, head, read-back or staleness).
//
//   parse_metadata        a negation disagreement is NegationMismatch: rejected,
//                         never decoded with a guessed negation (receiver §3④, C-30)
//   tier must be 1
//   decode symbol_count   STATIC tables (tier1/frame.h Tier1Model): the payload
//                         decodes whatever the context state (§5.9)
//   symbols → frame       exactly one frame; hash_present must equal "the frame
//                         uses Inherit or Ref" (context §17.1), else Malformed
//
// tier1_resolve() then verifies the pre-message context hash (receiver §3⑦) and
// resolves slots (§4). On a mismatch the payload is still usable: explicit
// slots resolve and only Inherit / Ref slots are unresolved (§6.1). The receiver
// pipeline decides what to do next (request sync, do not commit — Phase 10).
//
// tier1_render() renders in the LISTENER's language (language §10.1) and refuses
// while any slot is unresolved: nothing may present an unresolved slot as a
// value (receiver §7.1, C-31).

#include <string>

#include "context/context.h"
#include "lang/pack.h"
#include "packet/metadata.h"
#include "tier1/frame.h"
#include "tier2/tables.h"

namespace itantra {

enum class Tier1DecodeStatus : u8 {
    Ok,
    InvalidArgument,    // tables not loaded
    Malformed,          // metadata truncated or tier 00 / 11, or not exactly one frame
    NotTier1,
    NegationMismatch,   // negation copies disagree: reject, request a repeat
    DecodeFailure,      // the model broke its contract (unreachable with loaded tables)
};

struct Tier1Decoded {
    Metadata   metadata;   // priority, negation, seq, hash_present, context_hash
    Tier1Frame frame;
};

// `bytes` is the PLAINTEXT native payload (after open_payload()).
Tier1DecodeStatus tier1_decode(const CommonPack& common, const Tier2Tables& subwords, const u8* bytes, u32 length,
                               Tier1Decoded& out);

struct Tier1Received {
    bool          context_matched = false;   // no hash sent, or the wire hash matched
    ResolvedFrame resolved;
};

// `receiver_context` is the receiver's PRE-message context (null: none).
Tier1Received tier1_resolve(const CommonPack& common, const Tier1Decoded& decoded, const Context* receiver_context);

enum class Tier1RenderStatus : u8 { Ok, Unresolved, RenderFailed };

Tier1RenderStatus tier1_render(const LanguagePack& listener_pack, const Tier1Decoded& decoded,
                               const Tier1Received& received, std::string& text);

}  // namespace itantra
