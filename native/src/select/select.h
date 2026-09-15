#pragma once

// Tier selection — tier §8, §11, §13.1; packet §5, §7.5, §11; contract C-17 … C-19.
// SENDER-ONLY. One clause in; the one native payload to send out, or the reason
// there is none. Two independent questions, in order (tier §8):
//
//   1  Is Tier 1 safe?     tier1_encode() runs every §8.1 gate. On top of it
//                          the selector re-reads the Tier 1 payload's metadata
//                          (negation copies, negation, priority, seq, count,
//                          hash). Anything but a verified Ok → Tier 2.
//   2  Is Tier 1 smaller?  Asked only when it is safe. Both are encoded and
//                          their COMPLETE native packets compared (C-19):
//
//          native packet = plaintext native payload        (assemble(), packet §2)
//                          = metadata + coder bits + 2 flush bits + zero pad
//                        + kAeadTagBytes (4)               (packet §6.4)
//
//                          Tier 1 is selected only if its packet is STRICTLY
//                          smaller; a tie sends Tier 2.
//
// DECIDED in Phase 9 (recorded in the tier and packet specs' implementation
// resolutions):
//
//   Size       Metadata is not the same size on both tiers (Tier 1 19 bits,
//              Tier 2 21; +12 with a hash, which each tier sets for its own
//              reasons; +11 past 30 symbols), and byte padding is not the same,
//              so comparing coder bits alone ("payload size") can pick the
//              larger packet. The constant 4-byte tag and the Kotlin outer frame
//              cannot change the order; the tag is counted so the reported
//              figure is the real packet. kAeadOverheadBytes (0 under the debug
//              AEAD bypass) is never used here.
//   Tie        Tier 1 must be smaller (§8.2 "Is Tier 1 smaller?"). Equal packets
//              send Tier 2: lossless, and a richer context (§8.3).
//   Priority   Tier 1 safe → the Tier 1 priority (is_alert OR manual override),
//              whichever tier is sent: a verified alert is never lowered by
//              choosing the smaller encoding (packet §11.1). Tier 1 not safe →
//              the manual override only, "the only path available to Tier 2
//              messages, which carry no intent" (packet §11.1).
//   Too long   A clause of more than kMaxSymbolCount (2078) Tier 2 tokens has no
//              Tier 2 payload (packet §5 ASM_TOO_LONG). Nothing is split here:
//              one clause is one message (packet §7.5). If Tier 1 is safe it is
//              sent; otherwise the result is ClauseTooLong with no payload, no
//              commit and no counter consumed, and the sender pipeline must
//              tell the operator rather than send part of the clause. Tier §6.7's
//              "always succeeds" is the coder's totality — every token has a
//              finite code — within packet §5's size bound. A clause of at most
//              2078 bytes always has a Tier 2 payload.
//   Context-free (Phase 10 fix). A fully explicit request — policy.allow_inheritance
//              = false, the context spec §16.1 periodic refresh and the §18.3
//              context-free fallback — is sent with NO inheritance AND NO Tier 2
//              boost, whatever boost_tier2 says (tier §6.5: only the unboosted
//              form is self-contained). A boosted Tier 2 refresh would need the
//              very context agreement it exists to restore. Tier 1 carries no
//              hash then either. The reset flag (context §13.4) stays deferred.
//   Context    Tier 1 → commit the frame's payload (tier §11.2). Tier 2 → both
//              phones update from the text when they share a language, and both
//              skip it otherwise (tier §11.3). The selector commits nothing.
//
// UNCHANGED: cross-language selection is §8.2's safe-AND-smaller rule; the
// §8.4 preference for Tier 1 is deferred. No encoder, format, table or golden
// vector changes.

#include <cstddef>
#include <vector>

#include "context/context.h"
#include "crypto/aead.h"
#include "lang/extract.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "tier1/encode.h"
#include "tier2/encode.h"
#include "tier2/tables.h"

namespace itantra {

// The bytes a native payload occupies on the wire: the plaintext payload
// assemble() produced (metadata, coder bits, flush and padding already inside)
// plus the authentication tag (packet §6.4). Tier-independent by construction.
constexpr u32 native_packet_bytes(u32 plaintext_payload_bytes) noexcept {
    return plaintext_payload_bytes + kAeadTagBytes;
}

// §8.2 on complete packets. Strict: a tie is not "smaller".
constexpr bool tier1_packet_is_smaller(u32 tier1_packet_bytes, u32 tier2_packet_bytes) noexcept {
    return tier1_packet_bytes < tier2_packet_bytes;
}

struct SelectTables {
    Tier1Tables        tier1;               // tier1.pack is the SENDER's language
    const Tier2Tables* tier2 = nullptr;
};

struct SelectRequest {
    const ClauseExtraction* clause         = nullptr;
    const u8*               input          = nullptr;   // the original utterance
    std::size_t             input_length   = 0u;
    SourceSpan              tier2_text{0u, 0u};         // this clause's Tier 2 bytes (tier2_clause_inputs)
    i64                     stt_confidence = 0;
    bool                    manual_critical = false;    // "Send as Critical"
    u8                      seq            = 0u;        // low 8 bits of this message's counter
    const Context*          context        = nullptr;   // the sender's PRE-message context
    const AdjacencyFsm*     adjacency      = nullptr;   // optional
    SenderPolicy            policy;
    LangId                  sender_language   = 0u;     // must name tier1.pack's language
    LangId                  listener_language = 0u;     // from HELLO
    bool                    boost_tier2       = true;   // false: the unboosted recovery form (tier §6.5);
                                                        // ignored (unboosted) when !policy.allow_inheritance
};

// Whether Tier 2 is boosted for `r`: never for a context-free request.
inline bool tier2_boosted(const SelectRequest& r) noexcept {
    return r.boost_tier2 && r.policy.allow_inheritance;
}

enum class Tier1Verdict : u8 {
    Safe,          // Ok, and the payload's metadata verified
    Unsafe,        // a safety gate tripped — see SafetyTrigger
    Unavailable,   // TooLong: more than kMaxSymbolCount symbols
    Invalid,       // InvalidArgument, or a payload that fails verification
};

// tier §8.1, row for row, plus the sender refusals outside that table.
enum class SafetyTrigger : u8 {
    None,
    LowSttConfidence,         // LowConfidence
    NoHead,                   // NoHead, AmbiguousHead
    TwoTopClass,              // TwoTopClass
    NoRule,                   // NoRule, AmbiguousRuleSlot, NegationWithoutRule
    RequiredSlotMissing,      // RequiredSlotMissing, LiteralNotPlaceable
    ReadbackLostMeaning,      // ReadbackUnrenderable, ReadbackUnrenderedSlot,
                              // ReadbackUnexplainedWord, ReadbackBelowBar
    NegationCopiesDisagree,   // SelfCheckFailed, or the metadata copies differ
    SenderRefusal,            // NegationAmbiguous, AmbiguousSlot, UnrepresentableValue,
                              // ValueKindCollision, LiteralTooLong
};

enum class SelectOutcome : u8 {
    Tier1,             // payload is Tier 1
    Tier2,             // payload is Tier 2
    ClauseTooLong,     // Tier 1 not safe and Tier 2 ASM_TOO_LONG: nothing to send
    NoEncoding,        // Tier 1 not safe and Tier 2 failed otherwise (tables broken)
    InvalidArgument,   // the request itself is unusable
};

enum class ContextUpdate : u8 {
    None,        // nothing is sent
    FromFrame,   // commit `commit` (Tier 1)
    FromText,    // same-language Tier 2: both phones commit what the text yields
    Skip,        // cross-language Tier 2: neither phone updates (tier §11.3)
};

struct TierSelection {
    SelectOutcome   outcome = SelectOutcome::InvalidArgument;
    Tier1Verdict    tier1_verdict = Tier1Verdict::Invalid;
    SafetyTrigger   trigger       = SafetyTrigger::None;
    Tier1Encoding   tier1;                                // as tier1_encode() returned it
    Tier2Status     tier2_status = Tier2Status::InvalidArgument;
    bool            tier2_verified = false;
    u32             tier1_packet_bytes = 0u;              // 0 when Tier 1 is not safe
    u32             tier2_packet_bytes = 0u;              // 0 when there is no Tier 2 payload
    Priority        priority = Priority::Normal;
    std::vector<u8> payload;                              // the selected plaintext native payload
    u16             metadata_bits = 0u;
    ContextUpdate   context_update = ContextUpdate::None;
    CommitPayload   commit{};                             // FromFrame only
};

const char* select_outcome_name(SelectOutcome outcome) noexcept;
const char* safety_trigger_name(SafetyTrigger trigger) noexcept;

// Question 1 on an existing Tier 1 encoding.
Tier1Verdict assess_tier1(const SelectTables& tables, const SelectRequest& request, const Tier1Encoding& tier1,
                          SafetyTrigger& trigger) noexcept;

// The priority the selected message carries, whichever tier (see Priority above).
Priority selection_priority(const SelectRequest& request, const Tier1Encoding& tier1, Tier1Verdict verdict) noexcept;

// Question 2 and the result, from encodings already made. `tier2` must have been
// encoded from request.tier2_text with selection_priority(); a Tier 2 payload that
// does not verify is treated as unavailable. select_tier() is this plus the two
// encodes.
TierSelection choose_tier(const SelectTables& tables, const SelectRequest& request, const Tier1Encoding& tier1,
                          Tier2Status tier2_status, const NativePayload& tier2);

TierSelection select_tier(const SelectTables& tables, const SelectRequest& request);

}  // namespace itantra
