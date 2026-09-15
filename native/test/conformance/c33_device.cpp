// Conformance — C-33 on a device. Implementation plan Phase 11.
//
//   C-33 [D]  run a session of 100,000 messages; log every derived nonce
//             → zero repeats (packet §6.5: nonce reuse breaks AEAD)
//
// The session is the Phase 11 phone engine (api/engine.h), the code
// libitantra-native.so runs behind the JNI, in single-device loopback: every
// message is extracted, selected, sealed and committed by the sending half, then
// authenticated, decoded and committed by the same phone's receiving half.
// Utterances cycle through the fixture corpus (corpus/tier1.tsv and
// corpus/utterances.tsv) in every fixture language, and the listener language
// rotates, so both tiers, multi-clause utterances, cross-language Tier 2 and
// Tier 1 refusals all occur in the run.
//
// For every message:
//   - the nonce is logged exactly as the sender derived it; at the end the log
//     is sorted and must contain no repeat
//   - counters are exactly 1, 2, …, N (packet §3.6, crypto/nonce.h), and every
//     nonce is the one session_id ‖ BE64((direction << 63) | counter) with
//     direction 0 (initiator → responder)
//   - the receiver accepts it (Delivered, or RenderFailed, which is committed),
//     and the two halves' contexts are identical afterwards (context §10.2)
//
// native/test/device/CMakeLists.txt builds this with the NDK for devices; the
// host runs it too (CTest conformance.c33).
//
//   c33_device --packs <compiled pack root> --fixtures <fixture source root>
//              [--messages 100000] [--nonce-log <file>]

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "api/engine.h"
#include "itest.h"
#include "lang/languages.h"
#include "lang_fixture.h"

using namespace itantra;

namespace {

constexpr u32 kC33Messages = 100000u;

struct Utterance {
    std::string lang;
    std::string text;
    i64         confidence = 900;
    bool        critical   = false;
};

struct Options {
    std::string packs;
    std::string fixtures;
    std::string nonce_log;
    u32         messages = kC33Messages;
};

Options& options() {
    static Options o;
    return o;
}

bool add_directory(PackFiles& all, const std::string& root, const std::string& dir,
                   const std::vector<std::string>& names, std::string& error) {
    PackFiles part;
    if (!read_pack_directory(root + "/" + dir, names, part, error)) return false;
    for (PackFile& f : part) all.emplace_back(dir + "/" + f.first, std::move(f.second));
    return true;
}

bool load_engine(Engine& engine, std::string& error) {
    const std::string& root = options().packs;
    PackFiles all;
    if (!add_directory(all, root, "common", CommonPack::file_names(), error) ||
        !add_directory(all, root, "tier2", Tier2Tables::file_names(), error) ||
        !add_directory(all, root, "sender", RuleTable::file_names(), error)) {
        return false;
    }
    std::size_t count = 0u;
    const SupportedLanguage* languages = supported_languages(count);
    for (std::size_t i = 0u; i < count; ++i) {
        std::string ignored;
        add_directory(all, root, std::string("lang/") + languages[i].code, LanguagePack::file_names(), ignored);
    }
    return engine.load(all, error);
}

std::vector<Utterance> corpus() {
    std::vector<Utterance> out;
    for (const auto& r : langfx::read_tsv(options().fixtures + "/corpus/tier1.tsv")) {
        if (r.size() != 8u) continue;
        Utterance u;
        u.lang       = r[1];
        u.confidence = std::stoll(r[2]);
        u.critical   = r[3] == "1";
        u.text       = r[6];
        out.push_back(u);
    }
    for (const auto& r : langfx::read_tsv(options().fixtures + "/corpus/utterances.tsv")) {
        if (r.size() != 6u) continue;
        Utterance u;
        u.lang = r[1];
        u.text = r[4];
        out.push_back(u);
    }
    return out;
}

bool same_context(const Context& a, const Context& b) {
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

using Nonce = std::array<u8, kAeadNonceBytes>;

}  // namespace

ITEST(C33_a_100000_message_session_derives_no_nonce_twice) {
    Engine engine;
    std::string error;
    if (!load_engine(engine, error)) {
        std::printf("  engine not loaded from %s: %s\n", options().packs.c_str(), error.c_str());
        ITEST_TRUE(false);
        return;
    }
    const std::vector<Utterance> utterances = corpus();
    const std::vector<std::string> languages = engine.languages();
    if (utterances.empty() || languages.empty()) {
        std::printf("  no corpus under %s\n", options().fixtures.c_str());
        ITEST_TRUE(false);
        return;
    }

    u8 psk[kPskBytes], initiator[kHelloNonceBytes], responder[kHelloNonceBytes];
    for (u32 i = 0u; i < kPskBytes; ++i) psk[i] = static_cast<u8>(0xC3u ^ (i * 7u));
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        initiator[i] = static_cast<u8>(i * 5u + 1u);
        responder[i] = static_cast<u8>(0xFEu - i);
    }
    engine.begin_loopback_session(psk, initiator, responder);

    std::ofstream log;
    if (!options().nonce_log.empty()) {
        log.open(options().nonce_log, std::ios::binary | std::ios::trunc);
        if (!log) {
            std::printf("  cannot open nonce log %s\n", options().nonce_log.c_str());
            ITEST_TRUE(false);
            return;
        }
    }

    std::vector<Nonce> nonces;
    nonces.reserve(options().messages);
    const auto started = std::chrono::steady_clock::now();

    u32 sent = 0u, tier1 = 0u, tier2 = 0u, refused = 0u, delivered = 0u, render_failed = 0u, rejected = 0u;
    u32 bad_counter = 0u, bad_nonce = 0u, diverged = 0u, compared = 0u, engine_errors = 0u;
    SeqCounter expected_counter = 1u;
    std::size_t turn = 0u;
    while (sent < options().messages) {
        const Utterance&   u        = utterances[turn % utterances.size()];
        const std::string& listener = languages[(turn / utterances.size() + turn) % languages.size()];
        ++turn;
        if (!engine.has_language(u.lang)) continue;

        SendRequest request;
        request.text              = reinterpret_cast<const u8*>(u.text.data());
        request.length            = u.text.size();
        request.sender_language   = u.lang;
        request.listener_language = listener;
        request.stt_confidence    = u.confidence;
        request.manual_critical   = u.critical;
        const UtteranceSend result = engine.send_utterance(request);
        if (result.status != EngineStatus::Ok) {
            ++engine_errors;
            if (engine_errors < 5u) std::printf("  send: %s\n", engine_status_name(result.status));
            break;
        }

        // send_utterance() commits every clause of the utterance before it returns,
        // so the two halves can be compared only once all of them are received.
        bool received_all = true;
        for (const ClauseSend& c : result.clauses) {
            if (sent >= options().messages) {
                received_all = !c.sent;
                if (c.sent) break;
                continue;
            }
            if (!c.sent) {
                ++refused;
                continue;
            }
            ++sent;
            (c.outcome == SelectOutcome::Tier1 ? tier1 : tier2) += 1u;

            Nonce n;
            std::memcpy(n.data(), c.nonce, kAeadNonceBytes);
            nonces.push_back(n);
            if (log.is_open()) {
                static const char kHex[] = "0123456789abcdef";
                std::string line = std::to_string(c.counter) + " ";
                for (u8 b : n) {
                    line.push_back(kHex[b >> 4]);
                    line.push_back(kHex[b & 0x0Fu]);
                }
                line.push_back('\n');
                log << line;
            }

            if (c.counter != expected_counter) ++bad_counter;
            expected_counter = c.counter + 1u;
            bool structure = std::memcmp(n.data(), nonces.front().data(), 4u) == 0;
            for (u32 k = 0u; k < 8u; ++k) {
                structure = structure && n[4u + k] == static_cast<u8>(c.counter >> (56u - 8u * k));
            }
            if (!structure) ++bad_nonce;

            ReceiveResult r;
            if (engine.receive(listener, c.sealed.data(), static_cast<u32>(c.sealed.size()), r) != EngineStatus::Ok) {
                ++engine_errors;
                continue;
            }
            if (r.outcome == ReceiveOutcome::Delivered) {
                ++delivered;
            } else if (r.outcome == ReceiveOutcome::RenderFailed) {
                ++render_failed;
            } else {
                ++rejected;
                if (rejected < 5u) std::printf("  message %u rejected: %s\n", sent, receive_outcome_name(r.outcome));
            }
            if (r.counter != c.counter) ++bad_counter;
        }
        if (received_all) {
            ++compared;
            if (!same_context(engine.sender_context(), engine.receiver_context())) ++diverged;
        }
    }
    if (log.is_open()) log.close();

    std::sort(nonces.begin(), nonces.end());
    u32 repeats = 0u;
    for (std::size_t i = 1u; i < nonces.size(); ++i) repeats += nonces[i] == nonces[i - 1u] ? 1u : 0u;
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

    ITEST_EQ(sent, options().messages);
    ITEST_EQ(nonces.size(), options().messages);
    ITEST_EQ(repeats, 0u);
    ITEST_EQ(bad_counter, 0u);
    ITEST_EQ(bad_nonce, 0u);
    ITEST_EQ(rejected, 0u);
    ITEST_EQ(diverged, 0u);
    ITEST_EQ(engine_errors, 0u);
    ITEST_TRUE(tier1 > 0u && tier2 > 0u && compared > 0u);

    std::printf("  C-33: %u messages logged, %zu nonces, %u repeats; counters 1..%llu\n", sent, nonces.size(), repeats,
                static_cast<unsigned long long>(engine.sender_counter()));
    std::printf("  sent Tier 1 %u, Tier 2 %u; clauses not sent %u; received Delivered %u, RenderFailed %u, "
                "rejected %u; contexts compared after %u utterances, diverged %u; %lld ms\n",
                tier1, tier2, refused, delivered, render_failed, rejected, compared, diverged,
                static_cast<long long>(elapsed));
}

int main(int argc, char** argv) {
    for (int a = 1; a + 1 < argc; ++a) {
        if (std::strcmp(argv[a], "--packs") == 0) options().packs = argv[a + 1];
        if (std::strcmp(argv[a], "--fixtures") == 0) options().fixtures = argv[a + 1];
        if (std::strcmp(argv[a], "--nonce-log") == 0) options().nonce_log = argv[a + 1];
        if (std::strcmp(argv[a], "--messages") == 0) options().messages = static_cast<u32>(std::stoul(argv[a + 1]));
    }
    if (options().packs.empty() || options().fixtures.empty()) {
        std::printf("usage: c33_device --packs <dir> --fixtures <dir> [--messages N] [--nonce-log <file>]\n");
        return 2;
    }
    if (options().messages != kC33Messages) {
        std::printf("NOTE: --messages %u; C-33 itself is %u messages\n", options().messages, kC33Messages);
    }
    const int result = ::itest::run_all("conformance.c33");
    std::printf("C-33 on this device: %s\n", result == 0 ? "PASS" : "FAIL");
    return result;
}
