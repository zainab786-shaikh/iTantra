#pragma once

// Shared helpers for the Phase 10 receiver tests: session keys, sealing, a
// sending phone (its own context and counter; Tier 1, Tier 2 or Phase 9
// selection) and a receiving phone around receive_native_payload().

#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "packet/assemble.h"
#include "packet/seq.h"
#include "receiver/pipeline.h"
#include "select/select.h"
#include "tier1/encode.h"
#include "tier1/frame.h"
#include "tier2/commit.h"
#include "tier2/encode.h"
#include "select_fixture.h"

namespace rxfx {

using namespace itantra;

constexpr Direction kSenderDirection = Direction::InitiatorToResponder;

inline SessionKeys session_keys() {
    u8 psk[kSessionKeyBytes], ni[kHelloNonceBytes], nr[kHelloNonceBytes];
    for (u32 i = 0u; i < kSessionKeyBytes; ++i) psk[i] = static_cast<u8>(i * 11u + 3u);
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        ni[i] = static_cast<u8>(i + 90u);
        nr[i] = static_cast<u8>(250u - i);
    }
    SessionKeys keys;
    derive_session_keys(psk, ni, nr, keys);
    return keys;
}

inline std::vector<u8> seal(const SessionKeys& keys, SeqCounter counter, const std::vector<u8>& plain) {
    u8 nonce[kAeadNonceBytes];
    if (!derive_nonce(keys.session_id, kSenderDirection, counter, nonce)) return {};
    std::vector<u8> out(plain.size() + kAeadTagBytes);
    u32 n = 0u;
    if (aead_seal(keys.key, nonce, nullptr, 0u, plain.data(), static_cast<u32>(plain.size()), out.data(),
                  static_cast<u32>(out.size()), n) != AeadStatus::Ok) {
        return {};
    }
    out.resize(n);
    return out;
}

inline Tier1Encoding tier1_for(selfx::Fixture& f, const selfx::Sample& s, u8 seq) {
    Tier1Request q;
    q.clause          = &s.x.clauses.at(s.k);
    q.input           = reinterpret_cast<const u8*>(s.text.data());
    q.input_length    = s.text.size();
    q.stt_confidence  = s.confidence;
    q.manual_critical = s.critical;
    q.seq             = seq;
    q.context         = &s.ctx;
    return tier1_encode(f.tables(s.lang).tier1, q);
}

inline std::vector<u8> tier2_plain(selfx::Fixture& f, const selfx::Sample& s, u8 seq, bool boost, Priority priority) {
    Tier2Message m;
    m.seq           = seq;
    m.priority      = priority;
    m.language      = t2fx::test_language_id(s.lang);
    m.boost_context = boost ? &s.ctx : nullptr;
    const SourceSpan span = s.spans.at(s.k);
    NativePayload out;
    if (tier2_encode(f.base.tables, reinterpret_cast<const u8*>(s.text.data()) + span.begin, span.end - span.begin, m,
                     out) != Tier2Status::Ok) {
        return {};
    }
    return std::vector<u8>(out.bytes, out.bytes + out.len);
}

// A Tier 1 plaintext for any frame, with the hash of `hash_context` when given.
inline std::vector<u8> tier1_plaintext(selfx::Fixture& f, const Tier1Frame& frame, u8 seq, Priority priority,
                                       bool negation, const Context* hash_context) {
    const Tier1Model model(f.base.lang.common, f.base.tables.ngram());
    std::vector<Symbol> symbols;
    if (frame_to_symbols(f.base.lang.common, f.base.tables.vocabulary(), frame, symbols) != FrameFault::None) return {};
    AssemblyInput in;
    in.tier         = Tier::Tier1;
    in.symbols      = symbols.data();
    in.symbol_count = static_cast<u16>(symbols.size());
    in.model        = &model;
    in.seq          = seq;
    in.priority     = priority;
    in.negation     = negation;
    in.hash_present = hash_context != nullptr;
    in.context_hash = hash_context != nullptr ? wire_context_hash(context_hash(*hash_context)) : u16{0u};
    NativePayload p;
    if (assemble(in, p) != AsmResult::Ok) return {};
    return std::vector<u8>(p.bytes, p.bytes + p.len);
}

inline bool same_context(const Context& a, const Context& b) {
    for (u32 s = 0u; s < kSlotCount; ++s) {
        const Slot& x = a.slots[s];
        const Slot& y = b.slots[s];
        if (x.current != y.current || x.recent[0] != y.recent[0] || x.recent[1] != y.recent[1] || x.ver != y.ver ||
            x.age != y.age) {
            return false;
        }
    }
    return a.context_id == b.context_id && a.hash == b.hash && a.seq == b.seq;
}

// The sending phone.
struct Phone {
    selfx::Fixture& f;
    std::string     lang;
    SessionKeys     keys;
    Context         ctx{};
    SeqCounter      counter = 0u;
    SeqCounter      last    = 0u;   // counter of the latest packet

    Phone(selfx::Fixture& fx, std::string code, const SessionKeys& k) : f(fx), lang(std::move(code)), keys(k) {
        init_context(ctx);
    }

    selfx::Sample sample(const std::string& text, bool critical = false) {
        selfx::Sample s = selfx::single(f, "tx", lang, text, "-", 900, critical);
        s.ctx = ctx;
        return s;
    }

    u8 next(SeqCounter skip) {
        counter += 1u + skip;
        last = counter;
        if (is_context_refresh(counter)) init_context(ctx);   // periodic refresh, as the engine sends it
        return seq_to_wire(counter);
    }

    // Tier 1 exactly as tier1_encode() makes it; committed. Empty if refused.
    std::vector<u8> tier1(const std::string& text, bool critical = false, SeqCounter skip = 0u) {
        const u8 seq = next(skip);
        const selfx::Sample s = sample(text, critical);
        const Tier1Encoding e = tier1_for(f, s, seq);
        if (e.outcome != Tier1Outcome::Ok) return {};
        commit(ctx, e.commit);
        return seal(keys, counter, e.payload);
    }

    // Tier 2; committed from the text when the listener shares the language.
    std::vector<u8> tier2(const std::string& text, const std::string& listener, bool boost, bool critical = false,
                          bool commit_after = true, SeqCounter skip = 0u) {
        const u8 seq = next(skip);
        const selfx::Sample s = sample(text, critical);
        const std::vector<u8> plain = tier2_plain(f, s, seq, boost, critical ? Priority::Critical : Priority::Normal);
        if (plain.empty()) return {};
        if (commit_after && listener == lang) {
            const SourceSpan span = s.spans.at(s.k);
            commit(ctx, tier2_text_commit(f.pack(lang), f.base.lang.common,
                                          reinterpret_cast<const u8*>(s.text.data()) + span.begin, span.end - span.begin,
                                          seq));
        }
        return seal(keys, counter, plain);
    }

    // Phase 9 selection for clause k of `text`; commits as the selection says.
    // context_free: a periodic full-explicit / context-free refresh
    // (policy.allow_inheritance = false; Tier 2 is then never boosted).
    std::vector<u8> selected(const std::string& text, std::size_t k, const std::string& listener, TierSelection& out,
                             bool context_free = false) {
        const u8 seq = next(0u);
        selfx::Sample s = selfx::single(f, "tx", lang, text);
        s.k   = k;
        s.ctx = ctx;
        SelectRequest request = selfx::request_for(s, true, listener, seq);
        request.policy.allow_inheritance = !context_free && !is_context_refresh(counter);
        out = select_tier(f.tables(lang), request);
        if (out.payload.empty()) return {};
        if (out.context_update == ContextUpdate::FromFrame) {
            commit(ctx, out.commit);
        } else if (out.context_update == ContextUpdate::FromText) {
            const SourceSpan span = s.spans.at(k);
            commit(ctx, tier2_text_commit(f.pack(lang), f.base.lang.common,
                                          reinterpret_cast<const u8*>(s.text.data()) + span.begin, span.end - span.begin,
                                          seq));
        }
        return seal(keys, counter, out.payload);
    }
};

// The receiving phone.
struct Receiver {
    selfx::Fixture& f;
    std::string     lang;
    ReceiverSession session;

    Receiver(selfx::Fixture& fx, std::string code, const SessionKeys& keys) : f(fx), lang(std::move(code)) {
        begin_receiver_session(session, keys, kSenderDirection);
    }

    ReceiverTables tables() {
        ReceiverTables t;
        t.common = &f.base.lang.common;
        t.pack   = &f.pack(lang);
        t.tier2  = &f.base.tables;
        return t;
    }

    ReceiveResult receive(const std::vector<u8>& packet) {
        return receive_native_payload(tables(), session, packet.empty() ? nullptr : packet.data(),
                                      static_cast<u32>(packet.size()));
    }
};

inline bool trace_is(const ReceiveResult& r, std::initializer_list<ReceiveStage> stages) {
    if (r.trace_length != stages.size()) return false;
    u32 i = 0u;
    for (ReceiveStage s : stages) {
        if (r.trace[i++] != s) return false;
    }
    return true;
}

inline int stage_index(const ReceiveResult& r, ReceiveStage stage) {
    for (u32 i = 0u; i < r.trace_length; ++i) {
        if (r.trace[i] == stage) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace rxfx
