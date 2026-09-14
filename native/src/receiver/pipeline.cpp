#include "receiver/pipeline.h"

#include <algorithm>
#include <array>

#include "common/bitio.h"
#include "crypto/aead.h"
#include "lang/extract.h"
#include "lang/languages.h"
#include "packet/assemble.h"
#include "packet/parse.h"
#include "tier1/decode.h"
#include "tier1/frame.h"
#include "tier2/commit.h"
#include "tier2/decode.h"

namespace itantra {

namespace {

void enter(ReceiveResult& r, ReceiveStage stage) noexcept {
    if (r.trace_length < kReceiveStageCount) {
        r.trace[r.trace_length] = stage;
        r.trace_length          = static_cast<u8>(r.trace_length + 1u);
    }
}

u64 distance(u64 a, u64 b) noexcept {
    return a >= b ? a - b : b - a;
}

// The wire seq of a plaintext, read with the frozen field decoders (packet/
// metadata.h): 2 tier bits, symbol_count (5 bits, or 16 with the escape), then
// seq. The tier's VALUE never moves seq, so a plaintext with tier 00 / 11 still
// has a readable seq. False when the plaintext ends before seq does: at least
// 2 bytes in the short form (seq = bits 7 … 14), 4 with the escape (bits 18 … 25).
bool wire_seq_of(const u8* bytes, u32 length, u8& seq) noexcept {
    BitReader in(bytes, length);
    (void)in.read(kTierBits);
    (void)get_symbol_count(in);
    seq = get_seq(in);
    return !in.overran();
}

// ② — see "counter recovery" in pipeline.h.
bool recover_counter(const ReceiverSession& s, const u8* sealed, u32 length, NativePayload& plain, SeqCounter& counter,
                     u32& tried) noexcept {
    const SeqCounter largest  = s.replay.largest_accepted();
    const SeqCounter expected = largest < kMaxCounter ? largest + 1u : kMaxCounter;
    std::array<SeqCounter, 256> candidates{};
    for (u32 w = 0u; w < 256u; ++w) candidates[w] = seq_reconstruct(expected, static_cast<u8>(w));
    std::sort(candidates.begin(), candidates.end(), [expected](SeqCounter a, SeqCounter b) {
        const u64 da = distance(a, expected);
        const u64 db = distance(b, expected);
        return da != db ? da < db : a > b;
    });
    for (const SeqCounter candidate : candidates) {
        if (candidate == 0u || candidate > kMaxCounter) continue;
        ++tried;
        if (open_payload(sealed, length, s.keys, s.sender_direction, candidate, plain) != ParseStatus::Ok) continue;
        // SECURITY-CRITICAL — mandatory for EVERY accepted candidate; never skip,
        // never make it depend on whether the metadata parses. With up to 256
        // candidates, the 4-byte tag alone would let a forgery through with
        // probability 256 · 2^−32 = 2^−24. Requiring the decrypted seq to equal
        // the candidate's low 8 bits adds 8 independent bits per candidate and
        // restores 2^−32 for the whole search — the bound of one tag checked at
        // a known counter. A plaintext too short to contain seq (e.g. an empty
        // one from a bare 4-byte packet) or with a different seq is not this
        // counter: keep searching, accept nothing on the tag alone.
        u8 wire_seq = 0u;
        if (!wire_seq_of(plain.bytes, plain.len, wire_seq) || wire_seq != seq_to_wire(candidate)) {
            secure_wipe(plain.bytes, plain.len);
            plain.len = 0u;
            continue;
        }
        counter = candidate;
        return true;
    }
    return false;
}

void integrity_fail(ReceiveResult& r) {
    r.request_repeat  = true;
    r.emit            = true;
    r.output.status   = OutputStatus::IntegrityFail;
    r.output.text.clear();
    r.output.unresolved.clear();
    if (r.metadata_valid) {
        r.output.priority = r.metadata.priority;
        r.output.mode     = r.metadata.tier == Tier::Tier1 ? OutputMode::Tier1 : OutputMode::Tier2;
        r.output.language = r.metadata.tier == Tier::Tier2 ? r.metadata.language : LangId{0u};
    }
    enter(r, ReceiveStage::Emit);
}

struct Wipe {
    NativePayload& p;
    ~Wipe() { secure_wipe(p.bytes, p.len); }
};

}  // namespace

const char* receive_outcome_name(ReceiveOutcome outcome) noexcept {
    switch (outcome) {
        case ReceiveOutcome::Delivered:            return "Delivered";
        case ReceiveOutcome::Tier1Unresolved:      return "Tier1Unresolved";
        case ReceiveOutcome::Tier2ContextMismatch: return "Tier2ContextMismatch";
        case ReceiveOutcome::RenderFailed:         return "RenderFailed";
        case ReceiveOutcome::AuthenticationFailed: return "AuthenticationFailed";
        case ReceiveOutcome::NegationRejected:     return "NegationRejected";
        case ReceiveOutcome::Replayed:             return "Replayed";
        case ReceiveOutcome::Malformed:            return "Malformed";
        case ReceiveOutcome::InvalidArgument:      return "InvalidArgument";
    }
    return "?";
}

void begin_receiver_session(ReceiverSession& session, const SessionKeys& keys, Direction sender_direction) noexcept {
    session.keys             = keys;
    session.sender_direction = sender_direction;
    session.replay           = ReplayWindow{};
    init_context(session.context);
    session.context_suspect = false;
}

ReceiveResult receive_native_payload(const ReceiverTables& t, ReceiverSession& s, const u8* payload, u32 length) {
    ReceiveResult r;
    u8 own = kLangIdUnassigned;
    if (t.common == nullptr || t.pack == nullptr || t.tier2 == nullptr || !t.tier2->loaded() ||
        (payload == nullptr && length != 0u) || !lang_id_of(t.pack->language(), own)) {
        return r;
    }

    // ① FEC — no-op.
    enter(r, ReceiveStage::Fec);
    const u8* bytes = payload;
    const u32 n     = fec_decode(payload, length, bytes);

    // ② decrypt + authenticate.
    enter(r, ReceiveStage::Decrypt);
    NativePayload plain;
    plain.len           = 0u;
    plain.metadata_bits = 0u;
    const Wipe wipe{plain};
    SeqCounter counter = 0u;
    if (!recover_counter(s, bytes, n, plain, counter, r.candidates_tried)) {
        r.outcome = ReceiveOutcome::AuthenticationFailed;
        return r;
    }
    r.counter = counter;

    // ③ parse metadata.
    enter(r, ReceiveStage::Parse);
    u32 offset = 0u;
    const ParseStatus parsed = parse_metadata(plain.bytes, plain.len, r.metadata, offset);
    if (parsed != ParseStatus::Ok && parsed != ParseStatus::NegationMismatch) {
        r.outcome = ReceiveOutcome::Malformed;
        integrity_fail(r);
        return r;
    }
    r.metadata_valid = true;

    // ④ negation copies.
    enter(r, ReceiveStage::Negation);
    if (parsed == ParseStatus::NegationMismatch) {
        r.outcome = ReceiveOutcome::NegationRejected;
        integrity_fail(r);
        return r;
    }

    // ⑤ seq gap — evaluated here, acted on only if the packet is not a replay.
    enter(r, ReceiveStage::SeqGap);
    const u32 last_seq = seq_to_wire(s.replay.largest_accepted());
    r.gap     = static_cast<u8>((u32{r.metadata.seq} + 256u - ((last_seq + 1u) & 0xFFu)) & 0xFFu);
    r.seq_gap = r.gap != 0u;

    // ⑥ replay.
    enter(r, ReceiveStage::Replay);
    if (s.replay.check(counter) != ReplayVerdict::Fresh || !s.replay.accept(counter)) {
        r.outcome = ReceiveOutcome::Replayed;
        return r;
    }
    if (r.seq_gap) {
        r.request_sync    = true;
        s.context_suspect = true;
    }

    // ⑦ context hash, against the PRE-message context, before anything is decoded.
    enter(r, ReceiveStage::Hash);
    r.context_matched = !r.metadata.hash_present ||
                        r.metadata.context_hash == wire_context_hash(context_hash(s.context));
    if (r.metadata.hash_present && r.context_matched) s.context_suspect = false;

    // ⑧ model from tier.
    enter(r, ReceiveStage::Model);
    r.output.priority = r.metadata.priority;
    const bool tier1  = r.metadata.tier == Tier::Tier1;
    r.output.mode     = tier1 ? OutputMode::Tier1 : OutputMode::Tier2;
    r.output.language = tier1 ? LangId{own} : r.metadata.language;
    if (!tier1 && !r.context_matched) {
        r.outcome                  = ReceiveOutcome::Tier2ContextMismatch;
        r.output.status            = OutputStatus::ContextMismatch;
        r.request_sync             = true;
        r.request_unboosted_resend = true;
        s.context_suspect          = true;
        r.emit                     = true;
        enter(r, ReceiveStage::Emit);
        return r;
    }

    if (tier1) {
        // ⑨ decode — static model, whatever the context state.
        enter(r, ReceiveStage::Decode);
        Tier1Decoded decoded;
        if (tier1_decode(*t.common, *t.tier2, plain.bytes, plain.len, decoded) != Tier1DecodeStatus::Ok) {
            r.outcome = ReceiveOutcome::Malformed;
            integrity_fail(r);
            return r;
        }
        // ⑩ reconstruct — Inherit / Ref resolve only against a context ⑦ trusted.
        enter(r, ReceiveStage::Reconstruct);
        const Tier1Received received = tier1_resolve(*t.common, decoded, &s.context);
        if (received.context_matched != r.context_matched) {
            r.outcome = ReceiveOutcome::Malformed;
            integrity_fail(r);
            return r;
        }
        for (u32 slot = 0u; slot < kConceptSlotCount; ++slot) {
            if ((received.resolved.unresolved_slots & (1u << slot)) != 0u) r.output.unresolved.push_back(static_cast<u8>(slot));
        }
        bool rendered = false;
        if (r.output.unresolved.empty()) {
            rendered = tier1_render(*t.pack, decoded, received, r.output.text) == Tier1RenderStatus::Ok;
            if (!rendered) r.output.text.clear();
        }
        // ⑪ commit — only with a matching (or absent) hash (§8.1, R8).
        enter(r, ReceiveStage::Commit);
        if (r.context_matched) {
            r.context_committed = commit(s.context, frame_commit_payload(decoded.frame, r.metadata.seq)) == CommitResult::Ok;
            if (r.context_committed) r.context_update = ReceiverContextUpdate::FromFrame;
        } else {
            r.request_sync    = true;
            s.context_suspect = true;
        }
        if (!r.output.unresolved.empty()) {
            r.outcome       = ReceiveOutcome::Tier1Unresolved;
            r.output.status = OutputStatus::ContextMismatch;
        } else if (!rendered) {
            // Intact, decoded and committed: not an integrity problem, and a
            // repeat would render no better (receiver spec resolutions).
            r.outcome       = ReceiveOutcome::RenderFailed;
            r.output.status = OutputStatus::RenderFail;
        } else {
            r.outcome       = ReceiveOutcome::Delivered;
            r.output.status = OutputStatus::Ok;
        }
    } else {
        // ⑨ decode — boost only when hash_present (and ⑦ matched).
        enter(r, ReceiveStage::Decode);
        Tier2Decoded decoded;
        if (tier2_decode(*t.tier2, plain.bytes, plain.len, &s.context, decoded) != Tier2Status::Ok) {
            r.outcome = ReceiveOutcome::Malformed;
            integrity_fail(r);
            return r;
        }
        // ⑩ reconstruct — the sender's exact text, in the sender's language.
        enter(r, ReceiveStage::Reconstruct);
        r.output.text = decoded.text;
        // ⑪ commit — the same extractor over the same text, same language only.
        enter(r, ReceiveStage::Commit);
        if (r.metadata.language == own) {
            const CommitPayload update =
                tier2_text_commit(*t.pack, *t.common, reinterpret_cast<const u8*>(r.output.text.data()),
                                  r.output.text.size(), r.metadata.seq);
            r.context_committed = commit(s.context, update) == CommitResult::Ok;
            if (r.context_committed) r.context_update = ReceiverContextUpdate::FromText;
        } else {
            r.context_update = ReceiverContextUpdate::SkippedCrossLanguage;
        }
        r.outcome       = ReceiveOutcome::Delivered;
        r.output.status = OutputStatus::Ok;
    }

    // ⑫ emit.
    enter(r, ReceiveStage::Emit);
    r.emit = true;
    return r;
}

}  // namespace itantra
