#pragma once

// Shared helpers for the Phase 9 tier selection tests: the Tier 1 fixture (rules,
// corpus/tier1.tsv), selection requests, the utterance corpus as clauses with
// pre-message contexts, and the exact coder bit count of a symbol sequence.

#include <cstddef>
#include <string>
#include <vector>

#include "coder/coder.h"
#include "context/context.h"
#include "select/select.h"
#include "tier1/frame.h"
#include "tier1/rules.h"
#include "tier2/encode.h"
#include "tier2_fixture.h"

namespace selfx {

using namespace itantra;

struct Row {
    std::string id, lang, context, awaiting, text, expect;
    i64         confidence = 0;
    bool        critical   = false;
};

struct Fixture {
    t2fx::Fixture    base;
    RuleTable        rules;
    std::vector<Row> rows;
    std::string      error;

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
        return !rows.empty();
    }

    const LanguagePack& pack(const std::string& code) { return base.lang.pack(code); }

    SelectTables tables(const std::string& lang) {
        SelectTables t;
        t.tier1.common   = &base.lang.common;
        t.tier1.pack     = &pack(lang);
        t.tier1.rules    = &rules;
        t.tier1.subwords = &base.tables;
        t.tier2          = &base.tables;
        return t;
    }

    // "SLOT=concept;SLOT#number" committed once, as corpus/tier1.tsv writes it.
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
            p.slots[slot].value = item[at] == '=' ? base.lang.concepts.at(item.substr(at + 1u))
                                                  : static_cast<u16>(std::stoul(item.substr(at + 1u)));
        }
        return commit(ctx, p) == CommitResult::Ok;
    }
};

// One clause to select for, with everything a request points at.
struct Sample {
    std::string             id, lang, text;
    UtteranceExtraction     x;
    std::vector<SourceSpan> spans;
    std::size_t             k = 0u;
    Context                 ctx{};
    AdjacencyFsm            fsm;
    bool                    has_fsm    = false;
    i64                     confidence = 900;
    bool                    critical   = false;
};

inline SelectRequest request_for(const Sample& s, bool boost, const std::string& listener, u8 seq = 17u) {
    SelectRequest r;
    r.clause            = &s.x.clauses.at(s.k);
    r.input             = reinterpret_cast<const u8*>(s.text.data());
    r.input_length      = s.text.size();
    r.tier2_text        = s.spans.at(s.k);
    r.stt_confidence    = s.confidence;
    r.manual_critical   = s.critical;
    r.seq               = seq;
    r.context           = &s.ctx;
    r.adjacency         = s.has_fsm ? &s.fsm : nullptr;
    r.sender_language   = t2fx::test_language_id(s.lang);
    r.listener_language = t2fx::test_language_id(listener);
    r.boost_tier2       = boost;
    return r;
}

inline Sample single(Fixture& f, const std::string& id, const std::string& lang, const std::string& text,
                     const std::string& context = "-", i64 confidence = 900, bool critical = false) {
    Sample s;
    s.id         = id;
    s.lang       = lang;
    s.text       = text;
    s.confidence = confidence;
    s.critical   = critical;
    f.build_context(context, s.ctx);
    s.x     = f.base.lang.extract(lang, s.text);
    s.spans = tier2_clause_inputs(s.x, s.text.size());
    return s;
}

// Every clause of corpus/tier1.tsv (with its context, pending query, confidence
// and override) and of corpus/utterances.tsv (clause k's context is what both
// phones hold after same-language Tier 2 updates from clauses 0 … k−1).
inline std::vector<Sample> all_samples(Fixture& f) {
    std::vector<Sample> out;
    out.reserve(f.rows.size() + 4u * f.base.corpus.size());
    for (const Row& row : f.rows) {
        Sample s = single(f, "tier1:" + row.id, row.lang, row.text, row.context, row.confidence, row.critical);
        if (row.awaiting != "-") {
            s.fsm.on_query(f.base.lang.intents.at(row.awaiting), 0u, 1000u);
            s.has_fsm = true;
        }
        if (s.x.clauses.size() == 1u) out.push_back(std::move(s));
    }
    for (const t2fx::Utterance& u : f.base.corpus) {
        const UtteranceExtraction x = f.base.lang.extract(u.lang, u.text);
        Context ctx;
        init_context(ctx);
        for (std::size_t k = 0u; k < x.clauses.size(); ++k) {
            Sample s;
            s.id    = u.id + "." + std::to_string(k + 1u);
            s.lang  = u.lang;
            s.text  = u.text;
            s.x     = x;
            s.spans = tier2_clause_inputs(x, u.text.size());
            s.k     = k;
            s.ctx   = ctx;
            UtteranceExtraction one;
            one.clauses.push_back(x.clauses[k]);
            t2fx::commit_extraction(ctx, one, static_cast<u8>(k + 1u));
            out.push_back(std::move(s));
        }
    }
    return out;
}

// Coder bits the symbols commit under `model` — the compressed payload ALONE,
// without metadata, flush or padding (kCoderFlushBits).
inline u32 coder_bits(const PayloadModel& model, const Symbol* symbols, u32 count) {
    std::vector<u8> buffer(kMaxPayloadBytes);
    BitWriter       writer(buffer.data(), kMaxPayloadBytes);
    ArithmeticEncoder encoder(writer);
    for (u32 i = 0u; i < count; ++i) encoder.encode(model.model_at(i, symbols), symbols[i]);
    return encoder.committed_bits();
}

inline u32 tier1_coder_bits(Fixture& f, const Tier1Encoding& e) {
    const Tier1Model model(f.base.lang.common, f.base.tables.ngram());
    return coder_bits(model, e.symbols.data(), static_cast<u32>(e.symbols.size()));
}

// Tier 2 exactly as select_tier() encodes it for `r` at `priority`.
struct Tier2Run {
    Tier2Status   status = Tier2Status::InvalidArgument;
    NativePayload payload{};
    u32           coder_bits = 0u;
};

inline Tier2Run tier2_for(Fixture& f, const SelectRequest& r, Priority priority) {
    Tier2Run run;
    Tier2Message m;
    m.seq           = r.seq;
    m.priority      = priority;
    m.language      = r.sender_language;
    m.boost_context = tier2_boosted(r) ? r.context : nullptr;
    const u8*         text = r.input + r.tier2_text.begin;
    const std::size_t len  = r.tier2_text.end - r.tier2_text.begin;
    run.status = tier2_encode(f.base.tables, text, len, m, run.payload);
    if (run.status == Tier2Status::Ok) {
        Tier2Clause clause(f.base.tables);
        if (clause.prepare(text, len, m) == Tier2Status::Ok) {
            const AssemblyInput& in = clause.assembly_input();
            run.coder_bits = coder_bits(*in.model, in.symbols, in.symbol_count);
        }
    }
    return run;
}

}  // namespace selfx
