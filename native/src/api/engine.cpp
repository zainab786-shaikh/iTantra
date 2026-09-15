#include "api/engine.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "crypto/aead.h"
#include "lang/extract.h"
#include "lang/languages.h"
#include "tier2/commit.h"
#include "tier2/encode.h"

namespace itantra {

namespace {

// The files `names` under `directory` ("common", "lang/en", …), with the
// directory stripped, as the loaders expect them.
bool take_directory(const PackFiles& files, const std::string& directory, const std::vector<std::string>& names,
                    PackFiles& out, std::string& error) {
    out.clear();
    for (const std::string& name : names) {
        const std::string path = directory + "/" + name;
        bool found = false;
        for (const PackFile& f : files) {
            if (f.first == path) {
                out.emplace_back(name, f.second);
                found = true;
                break;
            }
        }
        if (!found) {
            error = "missing pack file " + path;
            return false;
        }
    }
    return true;
}

}  // namespace

const char* engine_status_name(EngineStatus status) noexcept {
    switch (status) {
        case EngineStatus::Ok:               return "Ok";
        case EngineStatus::NotLoaded:        return "NotLoaded";
        case EngineStatus::NoSession:        return "NoSession";
        case EngineStatus::UnknownLanguage:  return "UnknownLanguage";
        case EngineStatus::InvalidArgument:  return "InvalidArgument";
        case EngineStatus::CounterExhausted: return "CounterExhausted";
    }
    return "?";
}

Engine::~Engine() {
    end_session();
}

bool Engine::load(const PackFiles& files, std::string& error) {
    if (loaded_) {
        error = "engine already loaded";
        return false;
    }
    CommonPack common;
    PackFiles  part;
    if (!take_directory(files, "common", CommonPack::file_names(), part, error) || !common.load(std::move(part), error)) {
        return false;
    }
    Tier2Tables tier2;
    if (!take_directory(files, "tier2", Tier2Tables::file_names(), part, error) || !tier2.load(part, error)) {
        return false;
    }
    RuleTable rules;
    if (!take_directory(files, "sender", RuleTable::file_names(), part, error) || !rules.load(part, common, error)) {
        return false;
    }

    // Languages are whatever lang/<code>/ directories were supplied: adding one
    // is data, never code (language §4.4, L9).
    std::map<std::string, LanguagePack> packs;
    const std::string prefix = "lang/";
    for (const PackFile& f : files) {
        if (f.first.compare(0u, prefix.size(), prefix) != 0) continue;
        const std::size_t slash = f.first.find('/', prefix.size());
        if (slash == std::string::npos) continue;
        const std::string code = f.first.substr(prefix.size(), slash - prefix.size());
        if (packs.count(code) != 0u) continue;
        if (!is_supported_language(code)) {
            error = "unsupported language pack " + code;
            return false;
        }
        LanguagePack pack;
        if (!take_directory(files, prefix + code, LanguagePack::file_names(), part, error) ||
            !pack.load(std::move(part), common, error)) {
            error = code + ": " + error;
            return false;
        }
        if (pack.language() != code) {
            error = "pack under lang/" + code + " declares language " + pack.language();
            return false;
        }
        packs.emplace(code, std::move(pack));
    }
    if (packs.empty()) {
        error = "no language pack";
        return false;
    }

    // LanguagePack validated against the local `common`; the member copy holds
    // identical data (CommonPack keeps no pointers into its files).
    common_ = std::move(common);
    tier2_  = std::move(tier2);
    rules_  = std::move(rules);
    packs_  = std::move(packs);

    std::vector<const PackFile*> ordered;
    for (const PackFile& f : files) ordered.push_back(&f);
    std::sort(ordered.begin(), ordered.end(), [](const PackFile* a, const PackFile* b) { return a->first < b->first; });
    std::vector<u8> all;
    for (const PackFile* f : ordered) {
        all.insert(all.end(), f->first.begin(), f->first.end());
        all.push_back(0u);
        all.insert(all.end(), f->second.begin(), f->second.end());
    }
    pack_digest_ = crc32_iso_hdlc(all.data(), all.size());
    loaded_      = true;
    return true;
}

std::vector<std::string> Engine::languages() const {
    std::vector<std::string> out;
    for (const auto& p : packs_) out.push_back(p.first);
    return out;
}

bool Engine::has_language(const std::string& code) const {
    return packs_.count(code) != 0u;
}

void Engine::begin_loopback_session(const u8* psk, const u8* initiator_nonce, const u8* responder_nonce) noexcept {
    std::scoped_lock lock(send_mutex_, receive_mutex_);
    derive_session_keys(psk, initiator_nonce, responder_nonce, keys_);
    send_direction_ = Direction::InitiatorToResponder;
    init_context(send_context_);
    send_counter_ = 0u;
    begin_receiver_session(receiver_, keys_, send_direction_);
    session_ = true;
}

void Engine::begin_session(const u8* psk, const u8* initiator_nonce, const u8* responder_nonce,
                           bool initiator) noexcept {
    std::scoped_lock lock(send_mutex_, receive_mutex_);
    derive_session_keys(psk, initiator_nonce, responder_nonce, keys_);
    send_direction_ = initiator ? Direction::InitiatorToResponder : Direction::ResponderToInitiator;
    init_context(send_context_);
    send_counter_ = 0u;
    begin_receiver_session(receiver_, keys_,
                           initiator ? Direction::ResponderToInitiator : Direction::InitiatorToResponder);
    session_ = true;
}

u32 Engine::session_id() const {
    std::lock_guard<std::mutex> lock(send_mutex_);
    return keys_.session_id;
}

void Engine::end_session() noexcept {
    std::scoped_lock lock(send_mutex_, receive_mutex_);
    wipe_session_keys(keys_);
    wipe_session_keys(receiver_.keys);
    session_ = false;
}

UtteranceSend Engine::send_utterance(const SendRequest& request) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    UtteranceSend out;
    if (!loaded_) {
        out.status = EngineStatus::NotLoaded;
        return out;
    }
    if (!session_) {
        out.status = EngineStatus::NoSession;
        return out;
    }
    if (request.text == nullptr && request.length != 0u) {
        out.status = EngineStatus::InvalidArgument;
        return out;
    }
    const auto pack_it = packs_.find(request.sender_language);
    u8 sender_id   = kLangIdUnassigned;
    u8 listener_id = kLangIdUnassigned;
    if (pack_it == packs_.end() || !lang_id_of(request.sender_language, sender_id) ||
        !lang_id_of(request.listener_language, listener_id)) {
        out.status = EngineStatus::UnknownLanguage;
        return out;
    }
    const LanguagePack& pack = pack_it->second;

    UtteranceExtraction extraction;
    extract_utterance(pack, common_, request.text, request.length, extraction);
    const std::vector<SourceSpan> spans = tier2_clause_inputs(extraction, request.length);

    SelectTables tables;
    tables.tier1.common   = &common_;
    tables.tier1.pack     = &pack;
    tables.tier1.rules    = &rules_;
    tables.tier1.subwords = &tier2_;
    tables.tier2          = &tier2_;

    out.status = EngineStatus::Ok;
    for (std::size_t k = 0u; k < extraction.clauses.size() && k < spans.size(); ++k) {
        if (send_counter_ >= kMaxCounter) {
            out.status = EngineStatus::CounterExhausted;
            break;
        }
        const SeqCounter counter = send_counter_ + 1u;

        SelectRequest r;
        r.clause            = &extraction.clauses[k];
        r.input             = request.text;
        r.input_length      = request.length;
        r.tier2_text        = spans[k];
        r.stt_confidence    = request.stt_confidence;
        r.manual_critical   = request.manual_critical;
        r.seq               = seq_to_wire(counter);
        r.context           = &send_context_;
        r.adjacency         = nullptr;
        r.sender_language   = sender_id;
        r.listener_language = listener_id;
        r.boost_tier2       = kBoostTier2;

        TierSelection s = select_tier(tables, r);

        ClauseSend c;
        c.outcome            = s.outcome;
        c.tier1_verdict      = s.tier1_verdict;
        c.trigger            = s.trigger;
        c.priority           = s.priority;
        c.plaintext_bytes    = static_cast<u32>(s.payload.size());
        c.tier1_packet_bytes = s.tier1_packet_bytes;
        c.tier2_packet_bytes = s.tier2_packet_bytes;
        c.context_update     = s.context_update;
        c.text               = spans[k];

        if (s.payload.empty()) {   // ClauseTooLong, NoEncoding, InvalidArgument: nothing to send
            out.clauses.push_back(std::move(c));
            continue;
        }

        // Compress, then encrypt (packet §6.1). The selector verified the
        // payload's seq equals r.seq, the counter's low 8 bits (packet §3.6).
        std::vector<u8> sealed(s.payload.size() + kAeadOverheadBytes);
        u32 sealed_length = 0u;
        const bool sealed_ok =
            derive_nonce(keys_.session_id, send_direction_, counter, c.nonce) &&
            aead_seal(keys_.key, c.nonce, nullptr, 0u, s.payload.data(), static_cast<u32>(s.payload.size()),
                      sealed.data(), static_cast<u32>(sealed.size()), sealed_length) == AeadStatus::Ok;
        secure_wipe(s.payload.data(), static_cast<u32>(s.payload.size()));
        if (!sealed_ok) {
            std::memset(c.nonce, 0, sizeof c.nonce);
            c.outcome = SelectOutcome::InvalidArgument;
            out.clauses.push_back(std::move(c));
            continue;
        }
        sealed.resize(sealed_length);

        // Commit what was sent, with the receiver's own functions (context §10,
        // tier §11.2, §11.3), before the packet leaves this layer.
        bool          do_commit = false;
        CommitPayload payload{};
        if (s.context_update == ContextUpdate::FromFrame) {
            payload   = s.commit;
            do_commit = true;
        } else if (s.context_update == ContextUpdate::FromText) {
            payload = tier2_text_commit(pack, common_, request.text + spans[k].begin, spans[k].end - spans[k].begin,
                                        r.seq);
            do_commit = true;
        }
        if (do_commit && commit(send_context_, payload) != CommitResult::Ok) {
            secure_wipe(sealed.data(), static_cast<u32>(sealed.size()));
            std::memset(c.nonce, 0, sizeof c.nonce);
            c.commit_refused = true;
            out.clauses.push_back(std::move(c));
            continue;
        }

        send_counter_ = counter;
        c.counter     = counter;
        c.sent        = true;
        c.sealed      = std::move(sealed);
        out.clauses.push_back(std::move(c));
    }
    return out;
}

EngineStatus Engine::receive(const std::string& receiver_language, const u8* payload, u32 length, ReceiveResult& out) {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    out = ReceiveResult{};
    if (!loaded_) return EngineStatus::NotLoaded;
    if (!session_) return EngineStatus::NoSession;
    if (payload == nullptr && length != 0u) return EngineStatus::InvalidArgument;
    const auto pack_it = packs_.find(receiver_language);
    if (pack_it == packs_.end()) return EngineStatus::UnknownLanguage;

    ReceiverTables tables;
    tables.common = &common_;
    tables.pack   = &pack_it->second;
    tables.tier2  = &tier2_;
    out = receive_native_payload(tables, receiver_, payload, length);
    return EngineStatus::Ok;
}

Context Engine::sender_context() const {
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_context_;
}

Context Engine::receiver_context() const {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    return receiver_.context;
}

SeqCounter Engine::sender_counter() const {
    std::lock_guard<std::mutex> lock(send_mutex_);
    return send_counter_;
}

}  // namespace itantra
