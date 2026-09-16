#pragma once

// Receiver pipeline — receiver-pipeline-spec.md §2–§9, §11. RECEIVER-ONLY: no head
// selection, rule table, read-back, staleness, pronoun resolution, tier selection
// or clause segmentation (§10). Includes nothing sender-only (unit.receiver
// scans this directory).
//
//   native payload (outer frame already unwrapped by Kotlin, §1.1)
//   ①  FEC decode              no-op in the current phase: no algorithm (§3①)
//   ②  decrypt + authenticate  fail → discard, no output; the next good packet's
//                              seq reveals the gap (§9)
//   ③  parse metadata          unparseable after authentication → integrity_fail
//   ④  negation copies agree?  no → reject, integrity_fail, request repeat (C-30)
//   ⑤  seq gap?                gap = (seq − (last_seq + 1)) mod 256; gap > 0 →
//                              context suspect + sync request, packet continues
//   ⑥  replay check            seen (or behind the window) → discard silently: no
//                              output, no repair request — not even ⑤'s
//   ⑦  hash_present?           compare with the receiver's PRE-message context
//   ⑧  model from tier         Tier 2 boosted + mismatch → not decoded,
//                              context_mismatch, request sync + unboosted resend (§6.2)
//   ⑨  decode symbol_count
//   ⑩  reconstruct             Tier 1: resolve (Inherit / Ref only if ⑦ matched)
//                                      → render in the RECEIVER's language; any
//                                      unresolved slot → no text (§6.1, C-31)
//                              Tier 2: the sender's exact text, sender's LangId
//   ⑪  commit                  only on success (§8.1): Tier 1 with a matching (or
//                              absent) hash → frame_commit_payload; Tier 2 → the
//                              text commit (tier2/commit.h) iff the sender's LangId
//                              is the receiver's, else both phones skip (§8.3)
//   ⑫  emit                    receiver/output.h
//
// DECIDED in Phase 10 (packet spec implementation resolutions) — counter recovery.
// seq travels inside the ciphertext (§6.2), but the nonce needs the wide counter
// before decryption (§3.6, §6.5). Nothing on the wire changes. The receiver
// authenticates against the candidate counters of the frozen reconstruction
// rule: seq_reconstruct(expected, w) for every w in 0 … 255, where expected is
// one more than the largest counter accepted — exactly the counters
// packet/seq.h recovers, [expected − 127, expected + 128]. Candidates are tried
// nearest the expectation first (expected, +1, −1, …). The first one whose tag
// verifies AND whose decrypted seq equals its low 8 bits is the counter. The
// seq check is SECURITY-CRITICAL and applies to every accepted candidate,
// whatever the rest of the metadata looks like: seq is located with the frozen
// field decoders (bits 7 … 14, or 18 … 25 when symbol_count uses its escape), and
// a plaintext too short to contain it is never accepted. A wrong candidate
// passes only if a 32-bit tag and an 8-bit seq both match by chance (2^−40), so
// over all 256 candidates a forgery succeeds with 2^−32 — the same bound as one
// 4-byte tag checked at a known counter (packet §6.4). On the tag alone it would
// be 2^−24.
// Cost: one AEAD open in order, 256 at most for a packet that fails. A counter
// outside the window aliases and fails authentication (§3.6 accepts this).

#include <cstddef>

#include "context/context.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "crypto/replay.h"
#include "lang/pack.h"
#include "packet/metadata.h"
#include "packet/seq.h"
#include "receiver/output.h"
#include "tier2/tables.h"

namespace itantra {

enum class ReceiveStage : u8 {
    Fec,
    Decrypt,
    Parse,
    Negation,
    SeqGap,
    Replay,
    Hash,
    Model,
    Decode,
    Reconstruct,
    Commit,
    Emit,
};
constexpr u32 kReceiveStageCount = 12u;

enum class ReceiveOutcome : u8 {
    Delivered,              // decoded; every slot resolved; committed as §8.1 requires
    Tier1Unresolved,        // decoded; Inherit / Ref slots unresolved; not committed
    Tier2ContextMismatch,   // boosted, hash mismatch: not decoded, not committed
    RenderFailed,           // decoded and committed; the receiver's pack cannot render it
                            // (status render_fail) — e.g. a number in a slot whose
                            // template asks a named form, reachable cross-language
    AuthenticationFailed,   // no candidate counter authenticates: discarded, no output
    NegationRejected,       // negation copies disagree: rejected
    Replayed,               // already seen, or behind the replay window: discarded silently
    Malformed,              // authenticated, but not a valid payload (a sender fault)
    InvalidArgument,        // tables not loaded, or a null payload with a length
};

const char* receive_outcome_name(ReceiveOutcome outcome) noexcept;

enum class ReceiverContextUpdate : u8 {
    None,
    FromFrame,              // Tier 1 commit
    FromText,               // same-language Tier 2 commit
    SkippedCrossLanguage,   // Tier 2 from a sender in another language (§8.3)
};

struct ReceiverTables {
    const CommonPack*   common = nullptr;
    const LanguagePack* pack   = nullptr;   // the RECEIVER's language
    const Tier2Tables*  tier2  = nullptr;
};

// One direction of one session, receiver side. Local state, never on the wire.
struct ReceiverSession {
    SessionKeys  keys{};
    Direction    sender_direction = Direction::InitiatorToResponder;
    ReplayWindow replay;
    Context      context{};
    bool         context_suspect = false;   // a gap or a mismatch since the last matching hash
    SeqCounter   refresh_counter = 0u;      // counter of the last refresh applied (context §16.1); 0 = none
};

void begin_receiver_session(ReceiverSession& session, const SessionKeys& keys, Direction sender_direction) noexcept;

struct ReceiveResult {
    ReceiveOutcome outcome = ReceiveOutcome::InvalidArgument;
    bool           emit    = false;          // hand `output` to the output layer
    ReceiverOutput output;

    bool request_repeat           = false;
    bool request_sync             = false;
    bool request_unboosted_resend = false;

    SeqCounter counter        = 0u;          // the recovered wide counter (after ②)
    u32        candidates_tried = 0u;
    Metadata   metadata;
    bool       metadata_valid = false;
    bool       seq_gap        = false;
    u8         gap            = 0u;
    bool       context_matched   = false;     // ⑦: no hash sent, or it matched
    bool           context_reset     = false;   // this packet was a refresh point: context reset first
    bool           context_committed = false;
    ReceiverContextUpdate context_update    = ReceiverContextUpdate::None;

    ReceiveStage trace[kReceiveStageCount] = {};
    u8           trace_length = 0u;          // the stages entered, in order
};

// ① stage: the identity. No forward error correction exists in this phase.
inline u32 fec_decode(const u8* payload, u32 length, const u8*& out) noexcept {
    out = payload;
    return length;
}

ReceiveResult receive_native_payload(const ReceiverTables& tables, ReceiverSession& session, const u8* payload,
                                     u32 length);

}  // namespace itantra
