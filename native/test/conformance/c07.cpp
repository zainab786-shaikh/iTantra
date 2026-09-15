// Conformance — C-07 (validation-benchmark-contract §5.2), with host
// preconditions for C-08, C-14, C-25 and C-26, and the tier §12.1 item 5
// coverage report. Implementation plan Phase 8.
//
//   C-07 [H]  Tier 1: encode → decode → render → compare against the original.
//             Pass: read-back similarity >= the configured threshold for that
//             priority. The receiver side decodes the payload with its own
//             pre-message context, resolves, renders with the templates, and
//             the rendering is compared with what was said.
//   C-08 [P]  cross-language Tier 1 — the pair run is Phase 12. Host
//             precondition here: a decoded frame renders in each listener's
//             language, exactly as expected (corpus/renderings.tsv).
//   C-14 [P]  forced hash mismatch on Tier 1 — host precondition: the payload
//             decodes; only INHERIT / REF slots are unresolved; nothing renders.
//   C-25 [H]  low-confidence STT never uses Tier 1 (tier selection is Phase 9;
//             this checks the Tier 1 gate it relies on).
//   C-26 [H]  NORMAL / CRITICAL survive the round trip; CRITICAL uses the
//             higher read-back bar. (The Phase 11 run is [D].)
//   §12.1 5   coverage report: every corpus clause reaching INTENT_NONE, with
//             its reason, written to --report.
//
//   c07_test --packs <dir> --fixtures <dir> --report <file>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "context/context.h"
#include "lang/normalize.h"
#include "packet/assemble.h"
#include "tier1/decode.h"
#include "tier1/encode.h"
#include "tier2_fixture.h"
#include "itest.h"

using namespace itantra;

namespace {

struct Row {
    std::string id, lang, context, awaiting, text, expect;
    i64         confidence = 0;
    bool        critical   = false;
};

struct Fx {
    t2fx::Fixture              base;
    RuleTable                  rules;
    std::vector<Row>           rows;
    std::map<u16, std::string> intent_names;
    std::string                report_path;
    std::string                error;

    bool load(const std::string& packs, const std::string& src) {
        if (!base.load(packs, src)) {
            error = base.error;
            return false;
        }
        PackFiles files;
        if (!read_pack_directory(packs + "/sender", RuleTable::file_names(), files, error) ||
            !rules.load(files, base.lang.common, error)) {
            return false;
        }
        for (const auto& r : langfx::read_tsv(src + "/corpus/tier1.tsv")) {
            if (r.size() != 8u) continue;
            Row row;
            row.id         = r[0];
            row.lang       = r[1];
            row.confidence = std::stoll(r[2]);
            row.critical   = r[3] == "1";
            row.context    = r[4];
            row.awaiting   = r[5];
            row.text       = r[6];
            row.expect     = r[7];
            rows.push_back(row);
        }
        for (const auto& kv : base.lang.intents) intent_names[kv.second] = kv.first;
        return !rows.empty();
    }
};

Fx& fx() {
    static Fx f;
    return f;
}

const CommonPack&   common() { return fx().base.lang.common; }
const LanguagePack& pack(const std::string& code) { return fx().base.lang.pack(code); }

bool build_context(const std::string& spec, Context& ctx) {
    init_context(ctx);
    if (spec == "-") return true;
    CommitPayload p{};
    p.seq = 1u;
    for (const std::string& item : langfx::split(spec, ";")) {
        const std::size_t at = item.find_first_of("=#");
        u8 slot = 0u;
        if (at == std::string::npos || !slot_from_name(item.substr(0u, at), slot)) return false;
        p.slots[slot].op    = SlotOp::Write;
        p.slots[slot].value = item[at] == '=' ? fx().base.lang.concepts.at(item.substr(at + 1u))
                                              : static_cast<u16>(std::stoul(item.substr(at + 1u)));
    }
    return commit(ctx, p) == CommitResult::Ok;
}

Tier1Encoding encode(const std::string& lang, const std::string& text, const ClauseExtraction& clause,
                     const Context& ctx, i64 confidence, bool critical, const AdjacencyFsm* fsm) {
    Tier1Tables tables;
    tables.common   = &common();
    tables.pack     = &pack(lang);
    tables.rules    = &fx().rules;
    tables.subwords = &fx().base.tables;
    Tier1Request r;
    r.clause          = &clause;
    r.input           = reinterpret_cast<const u8*>(text.data());
    r.input_length    = text.size();
    r.stt_confidence  = confidence;
    r.manual_critical = critical;
    r.seq             = 17u;
    r.context         = &ctx;
    r.adjacency       = fsm;
    return tier1_encode(tables, r);
}

// The C-07 check, receiver side.
struct C07 {
    bool        decoded    = false;
    bool        same_frame = false;
    bool        rendered   = false;
    Priority    priority   = Priority::Normal;
    u32         similarity = 0u;
    u32         bar        = 0u;
    std::string text;
};

C07 check_c07(const std::string& lang, const Tier1Encoding& e, const ClauseExtraction& clause,
              const Context& receiver_context) {
    C07 r;
    Tier1Decoded d;
    r.decoded = tier1_decode(common(), fx().base.tables, e.payload.data(), static_cast<u32>(e.payload.size()), d) ==
                Tier1DecodeStatus::Ok;
    if (!r.decoded) return r;
    r.same_frame = d.frame == e.frame;
    r.priority   = d.metadata.priority;
    const Tier1Received received = tier1_resolve(common(), d, &receiver_context);
    r.rendered = tier1_render(pack(lang), d, received, r.text) == Tier1RenderStatus::Ok;
    if (!r.rendered) return r;
    const std::string rendered =
        normalize_surface(reinterpret_cast<const u8*>(r.text.data()), r.text.size(), pack(lang).rules());
    r.similarity = word_similarity_permille(clause.match_text, rendered);
    r.bar = d.metadata.priority == Priority::Critical ? pack(lang).readback_critical() : pack(lang).readback_normal();
    return r;
}

bool passes(const C07& c) {
    return c.decoded && c.same_frame && c.rendered && c.similarity >= c.bar;
}

struct RowRun {
    const Row*          row = nullptr;
    UtteranceExtraction x;
    Context             context;
    Tier1Encoding       e;
};

RowRun run_row(const Row& row, i64 confidence) {
    RowRun run;
    run.row = &row;
    build_context(row.context, run.context);
    AdjacencyFsm fsm;
    if (row.awaiting != "-") fsm.on_query(fx().base.lang.intents.at(row.awaiting), 0u, 1000u);
    run.x = fx().base.lang.extract(row.lang, row.text);
    if (run.x.clauses.size() == 1u) {
        run.e = encode(row.lang, row.text, run.x.clauses[0], run.context, confidence, row.critical, &fsm);
    }
    return run;
}

std::vector<RowRun>& runs() {
    static std::vector<RowRun> r;
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// C-07
// ---------------------------------------------------------------------------

ITEST(c07_tier1_corpus_meets_every_expectation_and_the_readback_bar) {
    u32 tier1 = 0u;
    u32 tier2 = 0u;
    for (const Row& row : fx().rows) {
        runs().push_back(run_row(row, row.confidence));
        const RowRun& run = runs().back();
        ITEST_EQ(run.x.clauses.size(), 1u);
        if (run.x.clauses.size() != 1u) continue;
        const Tier1Encoding& e = run.e;
        const std::vector<std::string> expect = langfx::split(row.expect, " ");
        bool ok = false;
        if (expect.at(0) == "tier1") {
            const C07 c = check_c07(row.lang, e, run.x.clauses[0], run.context);
            ok = e.outcome == Tier1Outcome::Ok && e.frame.intent == fx().base.lang.intents.at(expect.at(1)) &&
                 (e.priority == Priority::Critical) == (expect.at(2) == "CRITICAL") && passes(c) && c.priority == e.priority;
            std::printf("  %-4s %s tier1 %-22s %-8s read-back %4u/%u  \"%s\"\n", row.id.c_str(), row.lang.c_str(),
                        fx().intent_names[e.frame.intent].c_str(), e.priority == Priority::Critical ? "CRITICAL" : "NORMAL",
                        c.similarity, c.bar, c.text.c_str());
            ++tier1;
        } else {
            ok = e.outcome != Tier1Outcome::Ok && expect.at(1) == tier1_outcome_name(e.outcome);
            std::printf("  %-4s %s tier2 %s\n", row.id.c_str(), row.lang.c_str(), tier1_outcome_name(e.outcome));
            ++tier2;
        }
        ITEST_TRUE(ok);
        if (!ok) {
            std::printf("  MISMATCH %s: expected \"%s\", got %s intent %s (read-back %u/%u \"%s\")\n", row.id.c_str(),
                        row.expect.c_str(), tier1_outcome_name(e.outcome), fx().intent_names[e.frame.intent].c_str(),
                        e.readback.similarity, e.readback.bar, e.readback.text.c_str());
        }
    }
    std::printf("  C-07: %zu rows — %u Tier 1 (all above their bar), %u refused as expected\n", fx().rows.size(), tier1, tier2);
    ITEST_TRUE(tier1 >= 20u && tier2 >= 10u);
}

ITEST(c07_every_corpus_clause_that_frames_meets_the_bar_and_coverage_report) {
    struct Miss {
        std::string id, lang, clause, reason;
    };
    std::vector<Miss> misses;
    std::map<std::string, u32> reasons;
    u32 clauses = 0u;
    u32 framed  = 0u;
    u32 bugs    = 0u;
    const auto record = [&](const std::string& id, const std::string& lang, const ClauseExtraction& clause,
                            const Tier1Encoding& e, const Context& ctx) {
        ++clauses;
        if (e.outcome == Tier1Outcome::Ok) {
            ++framed;
            const bool ok = passes(check_c07(lang, e, clause, ctx));
            ITEST_TRUE(ok);
            if (!ok) std::printf("  C-07 FAILED on %s \"%s\"\n", id.c_str(), clause.clause_text.c_str());
            return;
        }
        if (e.outcome == Tier1Outcome::InvalidArgument || e.outcome == Tier1Outcome::SelfCheckFailed) ++bugs;
        misses.push_back(Miss{id, lang, clause.clause_text, tier1_outcome_name(e.outcome)});
        ++reasons[tier1_outcome_name(e.outcome)];
    };

    for (const t2fx::Utterance& u : fx().base.corpus) {
        const UtteranceExtraction x = fx().base.lang.extract(u.lang, u.text);
        for (std::size_t k = 0u; k < x.clauses.size(); ++k) {
            const Context ctx = [] {
                Context c;
                init_context(c);
                return c;
            }();
            record(u.id + (x.clauses.size() > 1u ? "." + std::to_string(k + 1u) : std::string()), u.lang, x.clauses[k],
                   encode(u.lang, u.text, x.clauses[k], ctx, 900, false, nullptr), ctx);
        }
    }
    for (const RowRun& run : runs()) {
        if (run.x.clauses.size() == 1u) record("tier1:" + run.row->id, run.row->lang, run.x.clauses[0], run.e, run.context);
    }
    ITEST_EQ(bugs, 0u);

    // tier §12.1 item 5
    std::filesystem::path path(fx().report_path);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream report(fx().report_path, std::ios::binary | std::ios::trunc);
    ITEST_TRUE(static_cast<bool>(report));
    report << "# Tier 1 coverage report (tier 12.1 item 5), synthetic fixture corpora\n";
    report << "# clauses " << clauses << ", Tier 1 safe " << framed << ", INTENT_NONE " << misses.size() << "\n";
    report << "# id\tlang\treason\tclause\n";
    for (const Miss& m : misses) report << m.id << '\t' << m.lang << '\t' << m.reason << '\t' << m.clause << '\n';
    report.close();
    ITEST_TRUE(std::filesystem::exists(path) && std::filesystem::file_size(path) > 0u);

    std::printf("  coverage: %u clauses, %u Tier 1 (all pass C-07), %zu INTENT_NONE → %s\n", clauses, framed, misses.size(),
                fx().report_path.c_str());
    for (const auto& kv : reasons) std::printf("    %-22s %u\n", kv.first.c_str(), kv.second);
}

// ---------------------------------------------------------------------------
// C-08 host precondition — cross-language Tier 1
// ---------------------------------------------------------------------------

ITEST(c08_precondition_tier1_frames_render_in_the_listeners_language) {
    const Tier1Model model(common(), fx().base.tables.ngram());
    u32 exact = 0u;
    for (const auto& r : langfx::read_tsv(fx().base.lang.src_dir + "/corpus/renderings.tsv")) {
        if (r.size() != 4u) continue;
        Tier1Frame f;
        f.intent = fx().base.lang.intents.at(r[0]);
        for (const std::string& item : langfx::split(r[1], "; ")) {
            const std::size_t at = item.find_first_of("=#~");
            u8 slot = 0u;
            ITEST_TRUE(at != std::string::npos && slot_from_name(item.substr(0u, at), slot));
            FrameSlot& fs = f.slots[slot];
            if (item[at] == '~') {
                fs.mode    = SlotMode::Literal;
                fs.literal = item.substr(at + 1u);
            } else {
                fs.mode  = SlotMode::Id;
                fs.value = item[at] == '=' ? fx().base.lang.concepts.at(item.substr(at + 1u))
                                           : static_cast<u16>(std::stoul(item.substr(at + 1u)));
            }
        }
        std::vector<Symbol> symbols;
        ITEST_TRUE(frame_to_symbols(common(), fx().base.tables.vocabulary(), f, symbols) == FrameFault::None);
        AssemblyInput in;
        in.tier         = Tier::Tier1;
        in.symbols      = symbols.data();
        in.symbol_count = static_cast<u16>(symbols.size());
        in.model        = &model;
        NativePayload payload;
        ITEST_TRUE(assemble(in, payload) == AsmResult::Ok);
        Tier1Decoded d;
        ITEST_TRUE(tier1_decode(common(), fx().base.tables, payload.bytes, payload.len, d) == Tier1DecodeStatus::Ok);
        std::string text;
        ITEST_TRUE(tier1_render(pack(r[2]), d, tier1_resolve(common(), d, nullptr), text) == Tier1RenderStatus::Ok);
        ITEST_TRUE(text == r[3]);
        if (text != r[3]) std::printf("  %s [%s] in %s: \"%s\" != \"%s\"\n", r[0].c_str(), r[1].c_str(), r[2].c_str(), text.c_str(), r[3].c_str());
        if (text == r[3]) ++exact;
    }
    ITEST_TRUE(exact >= 20u);

    // Every Tier 1 message of the corpus, heard in every listener's language.
    u32 cross = 0u;
    for (const RowRun& run : runs()) {
        if (run.e.outcome != Tier1Outcome::Ok) continue;
        Tier1Decoded d;
        ITEST_TRUE(tier1_decode(common(), fx().base.tables, run.e.payload.data(), static_cast<u32>(run.e.payload.size()), d) ==
                   Tier1DecodeStatus::Ok);
        const Tier1Received received = tier1_resolve(common(), d, &run.context);
        std::string line = "  " + run.row->id + " " + run.row->lang + " \"" + run.row->text + "\" →";
        for (const std::string& listener : langfx::fixture_languages()) {
            std::string text;
            ITEST_TRUE(tier1_render(pack(listener), d, received, text) == Tier1RenderStatus::Ok);
            if (listener != run.row->lang) ++cross;
            line += " " + listener + ": \"" + text + "\"";
        }
        std::printf("%s\n", line.c_str());
    }
    std::printf("  C-08 precondition: %u frames rendered exactly as expected; %u cross-language renderings of corpus messages\n",
                exact, cross);
}

// ---------------------------------------------------------------------------
// C-14, C-25, C-26 host preconditions
// ---------------------------------------------------------------------------

ITEST(c14_precondition_hash_mismatch_decodes_with_only_inherited_slots_unresolved) {
    u32 checked = 0u;
    for (const RowRun& run : runs()) {
        if (run.e.outcome != Tier1Outcome::Ok || !frame_uses_context(run.e.frame)) continue;
        Tier1Decoded d;
        ITEST_TRUE(tier1_decode(common(), fx().base.tables, run.e.payload.data(), static_cast<u32>(run.e.payload.size()), d) ==
                   Tier1DecodeStatus::Ok);
        Context stale;
        init_context(stale);
        const Tier1Received r = tier1_resolve(common(), d, &stale);
        u8 inherited = 0u;
        for (u32 s = 0u; s < kConceptSlotCount; ++s) {
            const SlotMode m = d.frame.slots[s].mode;
            if (m == SlotMode::Inherit || m == SlotMode::Ref) inherited = static_cast<u8>(inherited | (1u << s));
            if (m == SlotMode::Id || m == SlotMode::Literal) ITEST_TRUE(r.resolved.slots[s].kind != RenderKind::Absent);
        }
        ITEST_TRUE(!r.context_matched && inherited != 0u && r.resolved.unresolved_slots == inherited);
        std::string text;
        ITEST_TRUE(tier1_render(pack(run.row->lang), d, r, text) == Tier1RenderStatus::Unresolved && text.empty());
        ++checked;
    }
    ITEST_TRUE(checked >= 3u);
    std::printf("  C-14 precondition: %u inheriting messages decoded under a mismatched context, only inherited slots unresolved\n",
                checked);
}

ITEST(c25_precondition_low_confidence_never_uses_tier1) {
    u32 refused = 0u;
    for (const Row& row : fx().rows) {
        const RowRun low = run_row(row, pack(row.lang).stt_confidence_threshold() - 1);
        ITEST_TRUE(low.e.outcome == Tier1Outcome::LowConfidence);
        ++refused;
    }
    std::printf("  C-25 precondition: all %u rows below their language's threshold are refused Tier 1\n", refused);
}

ITEST(c26_precondition_priority_round_trips_and_critical_uses_the_higher_bar) {
    u32 critical = 0u;
    u32 normal   = 0u;
    for (const RowRun& run : runs()) {
        if (run.e.outcome != Tier1Outcome::Ok) continue;
        Tier1Decoded d;
        ITEST_TRUE(tier1_decode(common(), fx().base.tables, run.e.payload.data(), static_cast<u32>(run.e.payload.size()), d) ==
                   Tier1DecodeStatus::Ok);
        ITEST_TRUE(d.metadata.priority == run.e.priority);
        const IntentInfo* intent = common().intent(run.e.frame.intent);
        ITEST_TRUE(intent != nullptr && (run.e.priority == Priority::Critical) == (intent->is_alert || run.row->critical));
        ITEST_EQ(run.e.readback.bar, run.e.priority == Priority::Critical ? pack(run.row->lang).readback_critical()
                                                                          : pack(run.row->lang).readback_normal());
        (run.e.priority == Priority::Critical ? critical : normal) += 1u;
    }
    // e16 / e17: the same clause passes the NORMAL bar and fails the CRITICAL bar.
    const RowRun* e16 = nullptr;
    const RowRun* e17 = nullptr;
    for (const RowRun& run : runs()) {
        if (run.row->id == "e16") e16 = &run;
        if (run.row->id == "e17") e17 = &run;
    }
    ITEST_TRUE(e16 != nullptr && e17 != nullptr);
    if (e16 != nullptr && e17 != nullptr) {
        ITEST_TRUE(e16->e.outcome == Tier1Outcome::Ok && e17->e.outcome == Tier1Outcome::ReadbackBelowBar);
        ITEST_TRUE(e16->e.readback.similarity == e17->e.readback.similarity);
        ITEST_TRUE(e16->e.readback.similarity >= pack("en").readback_normal() &&
                   e17->e.readback.similarity < pack("en").readback_critical());
        std::printf("  C-26 precondition: %u CRITICAL, %u NORMAL round trips; \"%s\" scores %u: passes NORMAL %u, fails CRITICAL %u\n",
                    critical, normal, e16->row->text.c_str(), e16->e.readback.similarity, pack("en").readback_normal(),
                    pack("en").readback_critical());
    }
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--packs") == 0) packs = argv[a + 1];
        if (std::strcmp(argv[a], "--fixtures") == 0) fixtures = argv[a + 1];
        if (std::strcmp(argv[a], "--report") == 0) fx().report_path = argv[a + 1];
    }
    if (!fx().load(packs, fixtures) || fx().report_path.empty()) {
        std::printf("fixture not loaded: %s\n", fx().report_path.empty() ? "no --report" : fx().error.c_str());
        return 1;
    }
    return ::itest::run_all("conformance.c07");
}
