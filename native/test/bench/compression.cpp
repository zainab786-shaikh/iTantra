// Characterisation — implementation plan Phase 13, validation contract §6.
// Recorded, not graded: nothing here can fail except a broken setup.
//
//   M-01 … M-08  compression, per clause of every corpus utterance, aggregated per
//                language, per listener relation (same / cross language) and per tier
//   M-23 / M-24  [H] engine encode / decode wall time per clause (host only — device
//                figures come from the Android CharacterizationTest)
//   M-31         Tier 1 vs Tier 2 margin at complete-packet level, where both exist
//   M-33         AEAD tag + outer frame as a fraction of the complete packet
//   CONV         a 200-message conversation (the Phase 12 pair script): sizes of
//                inheriting, refresh-point and Tier 2 messages
//   REC          loss / reordering sweep through the same engine: messages refused,
//                wrong deliveries, messages until the contexts are identical again
//
// Everything runs through api/engine.h in single-device loopback — the code the
// phones run — with a fresh session per corpus utterance (isolated, initial
// context) and one long session for the conversation and recovery runs.
//
//   compression_bench --packs <compiled pack root> --fixtures <fixture source root>
//                     [--report <tsv>] [--json <json>] [--timing-rounds 200]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "api/engine.h"
#include "lang/languages.h"
#include "lang_fixture.h"

using namespace itantra;

namespace {

// PacketCodec.HEADER_BYTES — the Kotlin outer frame (packet §1.1), 8 bytes.
constexpr u32 kOuterFrameBytes = 8u;

struct Options {
    std::string packs, fixtures, report, json;
    u32         timing_rounds = 200u;
} opt;

struct Utterance {
    std::string id, lang, text;
    i64         confidence = 1000;   // STT_DECODER_CONFIDENCE, what the app passes for the real decoder
    bool        critical   = false;
};

bool add_directory(PackFiles& all, const std::string& dir, const std::vector<std::string>& names, std::string& error) {
    PackFiles part;
    if (!read_pack_directory(opt.packs + "/" + dir, names, part, error)) return false;
    for (PackFile& f : part) all.emplace_back(dir + "/" + f.first, std::move(f.second));
    return true;
}

bool load_engine(Engine& engine, std::string& error) {
    PackFiles all;
    if (!add_directory(all, "common", CommonPack::file_names(), error) ||
        !add_directory(all, "tier2", Tier2Tables::file_names(), error) ||
        !add_directory(all, "sender", RuleTable::file_names(), error)) {
        return false;
    }
    std::size_t count = 0u;
    const SupportedLanguage* languages = supported_languages(count);
    for (std::size_t i = 0u; i < count; ++i) {
        std::string ignored;
        add_directory(all, std::string("lang/") + languages[i].code, LanguagePack::file_names(), ignored);
    }
    return engine.load(all, error);
}

void begin(Engine& engine) {
    u8 psk[kPskBytes], a[kHelloNonceBytes], b[kHelloNonceBytes];
    for (u32 i = 0u; i < kPskBytes; ++i) psk[i] = static_cast<u8>(0x5Au ^ i);
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        a[i] = static_cast<u8>(i + 1u);
        b[i] = static_cast<u8>(0xF0u - i);
    }
    engine.begin_loopback_session(psk, a, b);
}

std::vector<Utterance> corpus() {
    std::vector<Utterance> out;
    for (const auto& r : langfx::read_tsv(opt.fixtures + "/corpus/tier1.tsv")) {
        if (r.size() != 8u) continue;
        Utterance u;
        u.id         = r[0];
        u.lang       = r[1];
        u.confidence = std::stoll(r[2]);
        u.critical   = r[3] == "1";
        u.text       = r[6];
        out.push_back(u);
    }
    for (const auto& r : langfx::read_tsv(opt.fixtures + "/corpus/utterances.tsv")) {
        if (r.size() != 6u) continue;
        Utterance u;
        u.id   = r[0];
        u.lang = r[1];
        u.text = r[4];
        out.push_back(u);
    }
    return out;
}

UtteranceSend send(Engine& engine, const std::string& text, const std::string& lang, const std::string& listener,
                   i64 confidence, bool critical) {
    SendRequest request;
    request.text              = reinterpret_cast<const u8*>(text.data());
    request.length            = text.size();
    request.sender_language   = lang;
    request.listener_language = listener;
    request.stt_confidence    = confidence;
    request.manual_critical   = critical;
    return engine.send_utterance(request);
}

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

struct Stats {
    std::vector<double> v;
    void   add(double x) { v.push_back(x); }
    double sum() const { double s = 0; for (double x : v) s += x; return s; }
    double mean() const { return v.empty() ? 0.0 : sum() / static_cast<double>(v.size()); }
    double pct(double p) const {
        if (v.empty()) return 0.0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(p * static_cast<double>(s.size() - 1u) + 0.5)];
    }
    double min() const { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }
    double max() const { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }
};

// One aggregate bucket: M-01 … M-08 figures.
struct Bucket {
    u32 clauses = 0u, tier1 = 0u, tier2 = 0u;
    u64 m01 = 0u, m02 = 0u, m03 = 0u, m04 = 0u;
    Stats ratio, symbols;
    void add(u32 original, u32 plain, u32 sealed, bool t1, u32 symbol_count) {
        ++clauses;
        (t1 ? tier1 : tier2) += 1u;
        m01 += original;
        m02 += plain;
        m03 += sealed;
        m04 += sealed + kOuterFrameBytes;
        ratio.add(static_cast<double>(original) / static_cast<double>(sealed + kOuterFrameBytes));
        symbols.add(symbol_count);
    }
};

std::string json;   // contract §7 characterisation records, written at the end

void record(const char* id, const char* env, double value, const char* unit, const std::string& breakdown) {
    std::printf("  %-6s %-3s %-40s %12.3f %s\n", id, env, breakdown.c_str(), value, unit);
    char line[512];
    std::snprintf(line, sizeof line, "%s\n  {\"id\":\"%s\",\"env\":\"%s\",\"value\":%.4f,\"unit\":\"%s\",\"breakdown\":\"%s\"}",
                  json.empty() ? "" : ",", id, env, value, unit, breakdown.c_str());
    json += line;
}

void report_bucket(const std::string& key, const Bucket& b) {
    if (b.clauses == 0u) return;
    const double n = b.clauses;
    record("M-01", "H", static_cast<double>(b.m01) / n, "bytes/clause", key);
    record("M-02", "H", static_cast<double>(b.m02) / n, "bytes/clause", key);
    record("M-03", "H", static_cast<double>(b.m03) / n, "bytes/clause", key);
    record("M-04", "H", static_cast<double>(b.m04) / n, "bytes/clause", key);
    record("M-05", "H", static_cast<double>(b.m01) / static_cast<double>(b.m04), "ratio (sum M-01 / sum M-04)", key);
    record("M-05m", "H", b.ratio.pct(0.5), "ratio (median per clause)", key);
    record("M-06", "H", 100.0 * (1.0 - static_cast<double>(b.m04) / static_cast<double>(b.m01)), "% saved", key);
    record("M-07", "H", 100.0 * b.tier1 / n, "% Tier 1", key + " n=" + std::to_string(b.clauses));
    record("M-08", "H", 100.0 * b.tier2 / n, "% Tier 2", key);
    record("SYM", "H", b.symbols.mean(), "symbols/clause", key);
}

// The Phase 12 pair script (PairSyncTest.SCRIPT).
const std::vector<std::pair<std::string, i64>>& script() {
    static const std::vector<std::pair<std::string, i64>> s = {
        {"Send an ambulance to the hospital", 1000}, {"Send medicine to the hospital", 1000},
        {"Send water to the hospital", 1000},        {"Fire at the north gate", 1000},
        {"Send an ambulance to the hospital", 1000}, {"all is quiet here", kSttConfidenceUnavailable},
        {"Send medicine to the hospital", 1000},     {"Police move away", 1000},
        {"The weather is good today", kSttConfidenceUnavailable}, {"Send water to the hospital", 1000},
    };
    return s;
}

// ---------------------------------------------------------------------------

int compression(Engine& engine, const std::vector<Utterance>& utterances, std::ofstream& tsv) {
    const std::vector<std::string> languages = engine.languages();
    std::map<std::string, Bucket> buckets;
    Stats margin, margin_pct, overhead, t1_packet, t2_packet;
    if (tsv) {
        tsv << "id\tlang\tlistener\tclause\ttier\tpriority\tM01_orig\tM02_plain\tM03_sealed\tM04_packet\tratio\t"
               "symbols\ttier1_packet\ttier2_packet\toutcome\ttext\n";
    }
    for (const Utterance& u : utterances) {
        if (!engine.has_language(u.lang)) continue;
        for (const std::string& listener : languages) {
            begin(engine);
            const UtteranceSend s = send(engine, u.text, u.lang, listener, u.confidence, u.critical);
            if (s.status != EngineStatus::Ok) return 1;
            const std::string relation = listener == u.lang ? "same" : "cross";
            for (std::size_t k = 0u; k < s.clauses.size(); ++k) {
                const ClauseSend& c = s.clauses[k];
                const u32 original = c.text.end - c.text.begin;
                const std::string clause_text = u.text.substr(c.text.begin, original);
                if (!c.sent) {
                    if (tsv) tsv << u.id << '\t' << u.lang << '\t' << listener << '\t' << k << "\t-\t-\t" << original
                                 << "\t\t\t\t\t\t" << c.tier1_packet_bytes << '\t' << c.tier2_packet_bytes << '\t'
                                 << "refused(" << select_outcome_name(c.outcome) << ")\t" << clause_text << '\n';
                    continue;
                }
                ReceiveResult r;
                engine.receive(listener, c.sealed.data(), static_cast<u32>(c.sealed.size()), r);
                const bool t1 = r.metadata.tier == Tier::Tier1;
                const u32 sealed = static_cast<u32>(c.sealed.size());
                for (const std::string& key : {std::string("all"), relation + " " + u.lang,
                                               relation + " " + u.lang + " " + (t1 ? "tier1" : "tier2"),
                                               std::string(t1 ? "tier1" : "tier2")}) {
                    buckets[key].add(original, c.plaintext_bytes, sealed, t1, r.metadata.symbol_count);
                }
                overhead.add(100.0 * static_cast<double>(sealed - c.plaintext_bytes + kOuterFrameBytes) /
                             static_cast<double>(sealed + kOuterFrameBytes));
                if (relation == "same" && c.tier1_packet_bytes > 0u && c.tier2_packet_bytes > 0u) {
                    margin.add(static_cast<double>(c.tier2_packet_bytes) - static_cast<double>(c.tier1_packet_bytes));
                    margin_pct.add(100.0 * (1.0 - static_cast<double>(c.tier1_packet_bytes) / c.tier2_packet_bytes));
                    t1_packet.add(c.tier1_packet_bytes);
                    t2_packet.add(c.tier2_packet_bytes);
                }
                if (tsv) {
                    char ratio[32];
                    std::snprintf(ratio, sizeof ratio, "%.2f", static_cast<double>(original) / (sealed + kOuterFrameBytes));
                    tsv << u.id << '\t' << u.lang << '\t' << listener << '\t' << k << '\t' << (t1 ? 1 : 2) << '\t'
                        << (c.priority == Priority::Critical ? "CRITICAL" : "NORMAL") << '\t' << original << '\t'
                        << c.plaintext_bytes << '\t' << sealed << '\t' << sealed + kOuterFrameBytes << '\t' << ratio
                        << '\t' << r.metadata.symbol_count << '\t' << c.tier1_packet_bytes << '\t'
                        << c.tier2_packet_bytes << '\t' << receive_outcome_name(r.outcome) << '\t' << clause_text << '\n';
                }
            }
        }
    }
    std::printf("\nM-01…M-08 (isolated sessions, every corpus clause × every listener language)\n");
    for (const auto& b : buckets) report_bucket(b.first, b.second);
    std::printf("\nM-31 Tier 1 vs Tier 2, complete native packet (payload + tag), same-language clauses where both exist\n");
    record("M-31", "H", static_cast<double>(margin.v.size()), "clauses with both tiers", "same-language");
    record("M-31", "H", t1_packet.mean(), "bytes mean", "tier1 packet");
    record("M-31", "H", t2_packet.mean(), "bytes mean", "tier2 packet");
    record("M-31", "H", margin.mean(), "bytes mean (T2 - T1)", "margin");
    record("M-31", "H", margin.pct(0.5), "bytes median", "margin");
    record("M-31", "H", margin.min(), "bytes min", "margin");
    record("M-31", "H", margin.max(), "bytes max", "margin");
    record("M-31", "H", margin_pct.pct(0.5), "% smaller (median)", "tier1 vs tier2");
    std::printf("\nM-33 AEAD tag + outer frame share of the complete packet\n");
    record("M-33", "H", overhead.mean(), "% mean", "tag 4 B + frame 8 B");
    record("M-33", "H", overhead.pct(0.5), "% median", "tag 4 B + frame 8 B");
    return 0;
}

int timing(Engine& engine, const std::vector<Utterance>& utterances) {
    // M-23 / M-24 [H]: one long same-language session, the corpus replayed; wall time
    // of send_utterance per clause and of receive per payload.
    Stats encode, decode;
    begin(engine);
    for (u32 round = 0u; round < opt.timing_rounds; ++round) {
        for (const Utterance& u : utterances) {
            if (!engine.has_language(u.lang)) continue;
            auto t = std::chrono::steady_clock::now();
            const UtteranceSend s = send(engine, u.text, u.lang, u.lang, u.confidence, u.critical);
            const double ms = ms_since(t);
            u32 sent = 0u;
            for (const ClauseSend& c : s.clauses) sent += c.sent ? 1u : 0u;
            if (sent == 0u) continue;
            encode.add(ms / sent);
            for (const ClauseSend& c : s.clauses) {
                if (!c.sent) continue;
                ReceiveResult r;
                t = std::chrono::steady_clock::now();
                engine.receive(u.lang, c.sealed.data(), static_cast<u32>(c.sealed.size()), r);
                decode.add(ms_since(t));
            }
        }
    }
    std::printf("\nM-23 / M-24 [H] engine time per clause (%u rounds, host build — not a device figure)\n", opt.timing_rounds);
    record("M-23", "H", encode.pct(0.5) * 1000.0, "us median", "encode (extract+select+seal+commit)");
    record("M-23", "H", encode.pct(0.95) * 1000.0, "us p95", "encode");
    record("M-24", "H", decode.pct(0.5) * 1000.0, "us median", "decode (auth+decode+render+commit)");
    record("M-24", "H", decode.pct(0.95) * 1000.0, "us p95", "decode");
    return 0;
}

// The pair script as one conversation, sender en → listener `listener`.
int conversation(Engine& engine, const std::string& listener) {
    begin(engine);
    Stats inheriting, explicit_t1, refresh, tier2;
    const auto& s = script();
    for (u32 i = 0u; i < 200u; ++i) {
        const UtteranceSend u = send(engine, s[i % s.size()].first, "en", listener, s[i % s.size()].second, false);
        const ClauseSend& c = u.clauses.at(0);
        ReceiveResult r;
        engine.receive(listener, c.sealed.data(), static_cast<u32>(c.sealed.size()), r);
        const double packet = static_cast<double>(c.sealed.size() + kOuterFrameBytes);
        if (r.metadata.tier == Tier::Tier2) tier2.add(packet);
        else if (is_context_refresh(c.counter)) refresh.add(packet);
        else if (r.metadata.hash_present) inheriting.add(packet);
        else explicit_t1.add(packet);
    }
    const std::string key = "en->" + listener;
    std::printf("\nCONV 200-message pair script, %s (complete packet M-04)\n", key.c_str());
    record("CONV", "H", inheriting.mean(), "bytes mean", key + " tier1 inheriting n=" + std::to_string(inheriting.v.size()));
    record("CONV", "H", explicit_t1.mean(), "bytes mean", key + " tier1 explicit n=" + std::to_string(explicit_t1.v.size()));
    record("CONV", "H", refresh.mean(), "bytes mean", key + " refresh point n=" + std::to_string(refresh.v.size()));
    record("CONV", "H", tier2.mean(), "bytes mean", key + " tier2 n=" + std::to_string(tier2.v.size()));
    record("CONV", "H", (inheriting.sum() + explicit_t1.sum() + refresh.sum() + tier2.sum()) / 200.0, "bytes mean", key + " all");
    return 0;
}

// Loss / reordering sweep. Loopback: a "lost" payload is simply never handed to the
// receiving half; a reordered one is held and delivered after its successor.
int recovery(Engine& engine) {
    const auto& s = script();
    // Reference texts per counter from a loss-free run.
    std::vector<std::string> reference(201u);
    begin(engine);
    for (u32 i = 0u; i < 200u; ++i) {
        const UtteranceSend u = send(engine, s[i % s.size()].first, "en", "en", s[i % s.size()].second, false);
        ReceiveResult r;
        engine.receive("en", u.clauses[0].sealed.data(), static_cast<u32>(u.clauses[0].sealed.size()), r);
        reference[i + 1u] = r.output.text;
    }
    std::printf("\nREC loss / reorder sweep, 200-message pair script en->en, refresh interval %llu, 50 seeds each\n",
                static_cast<unsigned long long>(kContextRefreshInterval));
    for (const auto& mode : std::vector<std::pair<const char*, u32>>{
             {"loss", 1u}, {"loss", 5u}, {"loss", 10u}, {"loss", 20u}, {"reorder", 5u}, {"reorder", 10u}}) {
        Stats refused, diverged_run, recover_len, delivered;
        u32 wrong = 0u, final_equal = 0u, events = 0u;
        for (u32 seed = 1u; seed <= 50u; ++seed) {
            u64 state = 0x9E3779B97F4A7C15ull * seed;
            const auto roll = [&]() {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<u32>((state >> 33) % 100u);
            };
            begin(engine);
            u32 refused_n = 0u, delivered_n = 0u, diverged_n = 0u;
            std::vector<u8> held;
            bool diverged = false;
            u32 diverged_since = 0u;
            const auto deliver = [&](const std::vector<u8>& p) {
                ReceiveResult r;
                engine.receive("en", p.data(), static_cast<u32>(p.size()), r);
                if (r.outcome == ReceiveOutcome::Delivered) {
                    ++delivered_n;
                    if (r.output.text != reference.at(r.counter)) ++wrong;
                } else if (r.emit) {
                    ++refused_n;
                }
            };
            for (u32 i = 0u; i < 200u; ++i) {
                const UtteranceSend u = send(engine, s[i % s.size()].first, "en", "en", s[i % s.size()].second, false);
                const std::vector<u8>& p = u.clauses.at(0).sealed;
                const bool fault = i < 199u && roll() < mode.second;
                if (std::strcmp(mode.first, "loss") == 0) {
                    if (fault) ++events; else deliver(p);
                } else if (!held.empty()) {
                    deliver(p);
                    deliver(held);
                    held.clear();
                } else if (fault) {
                    ++events;
                    held = p;
                } else {
                    deliver(p);
                }
                const bool same = engine.sender_context().hash == engine.receiver_context().hash;
                if (!same) {
                    ++diverged_n;
                    if (!diverged) diverged_since = i;
                    diverged = true;
                } else if (diverged) {
                    recover_len.add(static_cast<double>(i - diverged_since));
                    diverged = false;
                }
            }
            if (!held.empty()) deliver(held);
            if (engine.sender_context().hash == engine.receiver_context().hash) ++final_equal;
            refused.add(refused_n);
            delivered.add(delivered_n);
            diverged_run.add(diverged_n);
        }
        const std::string key = std::string(mode.first) + " " + std::to_string(mode.second) + "%";
        record("REC", "H", static_cast<double>(events) / 50.0, "fault events/run", key);
        record("REC", "H", delivered.mean(), "delivered/run (of 200)", key);
        record("REC", "H", refused.mean(), "safely refused/run", key);
        record("REC", "H", diverged_run.mean(), "messages with contexts differing/run", key);
        record("REC", "H", recover_len.mean(), "messages to reconverge (mean)", key);
        record("REC", "H", recover_len.max(), "messages to reconverge (max)", key);
        record("REC", "H", wrong, "wrong deliveries (all runs)", key);
        record("REC", "H", final_equal, "runs ending identical (of 50)", key);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--packs") opt.packs = v;
        else if (k == "--fixtures") opt.fixtures = v;
        else if (k == "--report") opt.report = v;
        else if (k == "--json") opt.json = v;
        else if (k == "--timing-rounds") opt.timing_rounds = static_cast<u32>(std::stoul(v));
    }
    Engine engine;
    std::string error;
    if (!load_engine(engine, error)) {
        std::printf("engine not loaded: %s\n", error.c_str());
        return 1;
    }
    const std::vector<Utterance> utterances = corpus();
    std::printf("compression_bench: %zu corpus utterances, languages:", utterances.size());
    for (const auto& l : engine.languages()) std::printf(" %s", l.c_str());
    std::printf("\n");
    std::ofstream tsv;
    if (!opt.report.empty()) tsv.open(opt.report, std::ios::trunc);
    int rc = compression(engine, utterances, tsv);
    for (const auto& l : engine.languages()) rc |= conversation(engine, l);
    rc |= timing(engine, utterances);
    rc |= recovery(engine);
    if (!opt.json.empty()) std::ofstream(opt.json, std::ios::trunc) << "{\"characterisation\":[" << json << "\n]}\n";
    return rc;
}
