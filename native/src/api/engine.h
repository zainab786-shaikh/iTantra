#pragma once

// Phone engine — the coarse native API behind the JNI boundary (implementation
// plan Phase 11; handoff "JNI BOUNDARY"; packet §1.1–1.4; receiver §1.1).
//
// One phone is both a sender and a receiver, so this layer holds both halves:
//
//   Kotlin                                  this file
//   ------                                  ---------
//   utterance text + language + confidence  → send_utterance()
//                                             extract (language §8) → per clause:
//                                             select_tier (tier §8) → aead_seal
//                                             (packet §6) → commit (context §10)
//                                           ← one sealed native payload per clause
//
//   native payload (outer frame unwrapped)  → receive()
//                                             receive_native_payload (receiver §2)
//                                           ← the receiver §7 output interface
//
// Ownership (handoff): native owns the context, the models, the coder, the
// counter and the keys; Kotlin owns audio, transport and UI. Nothing here
// touches a clock, a file, a socket or a random source — packs and key material
// arrive as bytes from the caller — so the same translation unit runs in the
// Android library and in the host tests (android-native/.../sources.cmake).
//
// COARSE (handoff): one call per utterance on send, one per payload on receive.
// An utterance is what the endpointer delivers; the language layer, which is
// native and sender-only, cuts it into clauses (context §7), so Kotlin cannot
// call per clause without first calling to segment. send_utterance() therefore
// returns one result per clause — never finer than a clause, never per token or
// per symbol. Each clause is selected against the context the previous clause
// committed (tier §4.1 "context commits in clause order").
//
// DECIDED for Phase 11 (reported, not in any spec):
//   Session      begin_loopback_session(): keys from the pinned KDF (crypto/kdf.h)
//                over caller-supplied PSK and HELLO nonces; this phone sends as
//                initiator → responder and its receiver authenticates that same
//                direction — the single-device loopback of the Phase 11 exit
//                criterion. PSK provisioning and the HELLO exchange (context §18.1,
//                packet §8.3) are Phase 12; a second phone cannot authenticate
//                these packets until then.
//   Tier 2 boost Never boosted (kBoostTier2). The spec leaves "when to send boosted"
//                open (tier §8 implementation resolutions). Until the sync and
//                unboosted-resend requests can be carried (Phase 12), a boosted
//                message a drifted receiver refuses (receiver §6.2) has no way to
//                be recovered; the unboosted form always decodes (tier §6.5).
//   Adjacency    No adjacency FSM (SelectRequest::adjacency = null). Arming it
//                needs the intent of a received QUERY, which the receiver §7
//                output interface does not carry.
//   Refused      A clause with no payload (ClauseTooLong, NoEncoding) consumes no
//                clause   counter and commits nothing; later clauses of the same
//                utterance are still selected and sent. The caller must tell the
//                operator (tier §8 "the sender pipeline must tell the operator").
//   Commit       The sender commits BEFORE the packet is released and with the same
//                functions the receiver uses (frame_commit_payload via select's
//                CommitPayload; tier2_text_commit). A commit that fails releases
//                nothing and consumes no counter.
//
// STT confidence: kSttConfidenceUnavailable is below every pack threshold, so a
// recogniser that reports no confidence can never select Tier 1 (tier §5.8,
// contract C-25). The confidence scale itself is still open (language spec).

#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/types.h"
#include "context/context.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "lang/pack.h"
#include "packet/metadata.h"
#include "packet/seq.h"
#include "receiver/pipeline.h"
#include "select/select.h"
#include "tier1/rules.h"
#include "tier2/tables.h"

namespace itantra {

constexpr i64  kSttConfidenceUnavailable = std::numeric_limits<i64>::min();
constexpr bool kBoostTier2               = false;

enum class EngineStatus : u8 {
    Ok,
    NotLoaded,          // load() has not succeeded
    NoSession,          // no session keys: begin_loopback_session() not called
    UnknownLanguage,    // not a supported language code, or no pack for it here
    InvalidArgument,    // null bytes with a length
    CounterExhausted,   // the send counter reached kMaxCounter: the session must re-key
};

const char* engine_status_name(EngineStatus status) noexcept;

// One clause of one utterance, as the sender pipeline handled it.
struct ClauseSend {
    SelectOutcome   outcome        = SelectOutcome::InvalidArgument;
    Tier1Verdict    tier1_verdict  = Tier1Verdict::Invalid;
    SafetyTrigger   trigger        = SafetyTrigger::None;
    Priority        priority       = Priority::Normal;
    bool            sent           = false;   // `sealed` holds a payload to hand to the transport
    bool            commit_refused = false;   // selected, but commit() refused it: nothing released
    std::vector<u8> sealed;                   // ciphertext ‖ tag (packet §6.2, §6.4)
    u32             plaintext_bytes    = 0u;  // the golden-vector boundary (packet §6.10.1)
    u32             tier1_packet_bytes = 0u;  // select/select.h, 0 when Tier 1 is not safe
    u32             tier2_packet_bytes = 0u;
    SeqCounter      counter            = 0u;  // this message's wide counter; 0 when not sent
    u8              nonce[kAeadNonceBytes] = {};   // derived, never transmitted (packet §6.5)
    ContextUpdate   context_update = ContextUpdate::None;
    SourceSpan      text{0u, 0u};             // this clause's Tier 2 bytes in the utterance
};

struct UtteranceSend {
    EngineStatus            status = EngineStatus::InvalidArgument;
    std::vector<ClauseSend> clauses;          // text order; empty if no clause was found
};

struct SendRequest {
    const u8*   text   = nullptr;             // the utterance, UTF-8, exactly as recognised
    std::size_t length = 0u;
    std::string sender_language;              // ISO 639-1; this phone's setting, needs a pack
    std::string listener_language;            // ISO 639-1; the peer's setting (HELLO, context §18.1)
    i64         stt_confidence  = kSttConfidenceUnavailable;
    bool        manual_critical = false;      // "Send as Critical" (packet §11.1)
};

class Engine {
public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    // Every table, by path relative to the pack root:
    //   common/<CommonPack::file_names()>       tier2/<Tier2Tables::file_names()>
    //   sender/<RuleTable::file_names()>        lang/<code>/<LanguagePack::file_names()>
    // At least one language. Replaces nothing on failure; call once.
    bool load(const PackFiles& files, std::string& error);

    bool                     loaded() const noexcept { return loaded_; }
    std::vector<std::string> languages() const;
    bool                     has_language(const std::string& code) const;

    // psk: kPskBytes; nonces: kHelloNonceBytes each. Starts both halves from the
    // context spec §5.3 initial state; the send counter restarts at 0 (the first
    // message carries 1, crypto/nonce.h).
    void begin_loopback_session(const u8* psk, const u8* initiator_nonce, const u8* responder_nonce) noexcept;
    void end_session() noexcept;

    UtteranceSend send_utterance(const SendRequest& request);

    // `receiver_language` is this phone's setting: Tier 1 renders in it, and the
    // Tier 2 context rule compares against it (receiver §7.2, §8.3).
    EngineStatus receive(const std::string& receiver_language, const u8* payload, u32 length, ReceiveResult& out);

    // Observability for tests: both halves' contexts, and the send counter.
    Context    sender_context() const;
    Context    receiver_context() const;
    SeqCounter sender_counter() const;

private:
    CommonPack                          common_;
    Tier2Tables                         tier2_;
    RuleTable                           rules_;
    std::map<std::string, LanguagePack> packs_;
    bool                                loaded_ = false;

    mutable std::mutex send_mutex_;
    mutable std::mutex receive_mutex_;

    bool            session_        = false;
    SessionKeys     keys_{};
    Direction       send_direction_ = Direction::InitiatorToResponder;
    Context         send_context_{};
    SeqCounter      send_counter_   = 0u;
    ReceiverSession receiver_;
};

}  // namespace itantra
