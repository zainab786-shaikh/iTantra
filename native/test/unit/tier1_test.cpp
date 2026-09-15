// Unit tests — Tier 1 (native/src/tier1). Implementation plan Phase 8.
//
//   unit   head precedence ACTION > EVENT > STATE > ENTITY > MODIFIER
//   unit   ties break by slot enum order
//   unit   no head → INTENT_NONE; two top-class → INTENT_NONE
//   unit   rule_priority sorted, first match wins
//   unit   NEG_SET selects a DIFFERENT intent, not a flag on the same one
//   unit   head_implied true → head not transmitted
//   unit   TIME never inherits
//   unit   literals encode via the Tier 2 coder, any script
//   unit   literals never written to context
//   unit   adjacency FSM times out
//   unit   STATIC model — no context boost applied
//   build  rule compiler: duplicate rule_priority in a bucket = build error;
//          every intent reachable; every ACTION/EVENT/STATE has a rule
//          (also run as CTest rulec.rejects.* against the itantra-rulec tool)
//
// Plus: frames round trip through symbols and payloads, number coding and value
// kind collisions, staleness, graceful hash mismatch, Ref, negation copy
// disagreement, hash_present consistency, the LangId mapping, negation
// extraction, read-back bars and similarity, low confidence, priority, and that
// the shared Tier 1 files include nothing sender-only.
//
//   tier1_test --packs <dir> --fixtures <dir> --rulec-fixtures <dir> --shared-sources <files...>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "context/context.h"
#include "lang/languages.h"
#include "packet/assemble.h"
#include "packet/parse.h"
#include "tier1/adjacency.h"
#include "tier1/decode.h"
#include "tier1/encode.h"
#include "tier1/frame.h"
#include "tier1/head.h"
#include "tier1/readback.h"
#include "tier1/rulec/rulec.h"
#include "tier1/rules.h"
#include "tier1/slots.h"
#include "tier2/boost.h"
#include "tier2_fixture.h"
#include "itest.h"

using namespace itantra;
using langfx::cps;

namespace {

struct Fx {
    t2fx::Fixture                      base;
    RuleTable                          rules;
    std::string                        packs_dir;
    std::string                        rulec_dir;
    std::vector<std::string>           shared_sources;
    std::map<std::string, std::string> tier1_text;   // corpus/tier1.tsv id → text
    std::string                        error;

    bool load(const std::string& packs, const std::string& src) {
        packs_dir = packs;
        if (!base.load(packs, src)) {
            error = base.error;
            return false;
        }
        PackFiles files;
        if (!read_pack_directory(packs + "/sender", RuleTable::file_names(), files, error) ||
            !rules.load(files, base.lang.common, error)) {
            return false;
        }
        for (const auto& row : langfx::read_tsv(src + "/corpus/tier1.tsv")) {
            if (row.size() == 8u) tier1_text[row[0]] = row[6];
        }
        return !tier1_text.empty();
    }
};

Fx& fx() {
    static Fx f;
    return f;
}

const CommonPack&   common() { return fx().base.lang.common; }
const LanguagePack& pack(const std::string& code) { return fx().base.lang.pack(code); }
const Tier2Tables&  subwords() { return fx().base.tables; }
u16                 cid(const char* name) { return fx().base.lang.concepts.at(name); }
u16                 iid(const char* name) { return fx().base.lang.intents.at(name); }
const std::string&  corpus_text(const char* id) { return fx().tier1_text.at(id); }

Context empty_context() {
    Context ctx;
    init_context(ctx);
    return ctx;
}

Context context_with(std::initializer_list<std::pair<SlotId, u16>> writes) {
    Context ctx = empty_context();
    CommitPayload p{};
    p.seq = 1u;
    for (const auto& w : writes) {
        p.slots[w.first].op    = SlotOp::Write;
        p.slots[w.first].value = w.second;
    }
    commit(ctx, p);
    return ctx;
}

void age_context(Context& ctx, u32 messages) {
    for (u32 i = 0u; i < messages; ++i) {
        CommitPayload p{};
        p.seq = static_cast<u8>(2u + i);
        commit(ctx, p);
    }
}

struct Encoded {
    UtteranceExtraction x;
    Tier1Encoding       e;
};

Encoded encode(const std::string& code, const std::string& text, const Context& ctx, i64 confidence = 900,
               bool critical = false, const AdjacencyFsm* fsm = nullptr, SenderPolicy policy = SenderPolicy{}) {
    Encoded out;
    out.x = fx().base.lang.extract(code, text);
    Tier1Tables tables;
    tables.common   = &common();
    tables.pack     = &pack(code);
    tables.rules    = &fx().rules;
    tables.subwords = &subwords();
    Tier1Request r;
    r.clause          = out.x.clauses.size() == 1u ? &out.x.clauses[0] : nullptr;
    r.input           = reinterpret_cast<const u8*>(text.data());
    r.input_length    = text.size();
    r.stt_confidence  = confidence;
    r.manual_critical = critical;
    r.seq             = 9u;
    r.context         = &ctx;
    r.adjacency       = fsm;
    r.policy          = policy;
    out.e = tier1_encode(tables, r);
    return out;
}

ConceptMatch match(u32 group, const char* name) {
    const ConceptInfo* info = common().concept_info(cid(name));
    return ConceptMatch{group, info->id, info->slot, info->concept_class, info->categories, FormClass::Base,
                        Origin::Native, 0u, 0u, SourceSpan{0u, 0u}};
}

ClauseExtraction synthetic(std::initializer_list<ConceptMatch> matches) {
    ClauseExtraction c;
    c.concepts.assign(matches.begin(), matches.end());
    assign_slots(c, kConceptSlotMask);
    return c;
}

std::vector<u8> assemble_frame(const Tier1Frame& frame, bool negation, bool hash_present, u16 wire_hash,
                               const CommonPack* over = nullptr) {
    const CommonPack& c = over != nullptr ? *over : common();
    std::vector<Symbol> symbols;
    if (frame_to_symbols(c, subwords().vocabulary(), frame, symbols) != FrameFault::None) return {};
    const Tier1Model model(c, subwords().ngram());
    AssemblyInput in;
    in.tier         = Tier::Tier1;
    in.symbols      = symbols.data();
    in.symbol_count = static_cast<u16>(symbols.size());
    in.model        = &model;
    in.seq          = 5u;
    in.negation     = negation;
    in.hash_present = hash_present;
    in.context_hash = wire_hash;
    const auto p = std::make_unique<NativePayload>();
    if (assemble(in, *p) != AsmResult::Ok) return {};
    return std::vector<u8>(p->bytes, p->bytes + p->len);
}

Tier1DecodeStatus decode(const std::vector<u8>& bytes, Tier1Decoded& out, const CommonPack* over = nullptr) {
    return tier1_decode(over != nullptr ? *over : common(), subwords(), bytes.data(), static_cast<u32>(bytes.size()), out);
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

CommonPack make_common(const std::vector<IntentInfo>& intents) {
    PackFiles files;
    files.emplace_back("concepts.bin", serialize_concepts(common().concepts()));
    files.emplace_back("intents.bin", serialize_intents(intents));
    files.emplace_back("schema_version", std::vector<u8>{'1'});
    CommonPack c;
    std::string error;
    c.load(std::move(files), error);
    return c;
}

u8 slot_mask(std::initializer_list<SlotId> slots) {
    u32 m = 0u;
    for (SlotId s : slots) m |= 1u << s;
    return static_cast<u8>(m);
}

u32 bit_at(const std::vector<u8>& bytes, u32 i) {
    return (bytes[i / 8u] >> (7u - i % 8u)) & 1u;
}

}  // namespace

// ---------------------------------------------------------------------------
// Head selection — tier §5.3
// ---------------------------------------------------------------------------

ITEST(head_precedence_is_action_event_state_entity_modifier) {
    HeadSelection h =
        select_head(synthetic({match(0u, "urgent"), match(1u, "police"), match(2u, "injured"), match(3u, "fire"), match(4u, "send")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("send"));
    h = select_head(synthetic({match(0u, "urgent"), match(1u, "police"), match(2u, "injured"), match(3u, "fire")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("fire"));
    h = select_head(synthetic({match(0u, "urgent"), match(1u, "police"), match(2u, "injured")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("injured"));
    h = select_head(synthetic({match(0u, "urgent"), match(1u, "police")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("police"));
    h = select_head(synthetic({match(0u, "urgent")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_class == ConceptClass::Modifier);
}

ITEST(entity_and_modifier_ties_break_by_slot_enum_order) {
    HeadSelection h = select_head(synthetic({match(0u, "north_gate"), match(1u, "police")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("police"));      // ACTOR before LOCATION
    h = select_head(synthetic({match(0u, "hospital"), match(1u, "ambulance")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("ambulance"));   // OBJECT before LOCATION
    h = select_head(synthetic({match(0u, "north_gate"), match(1u, "hospital")}));
    ITEST_TRUE(h.status == HeadStatus::TwoTopClass);                                 // same slot: no tie-break
    h = select_head(synthetic({match(0u, "urgent"), match(1u, "critical")}));
    ITEST_TRUE(h.status == HeadStatus::TwoTopClass);
    h = select_head(synthetic({match(0u, "police"), match(1u, "police")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("police"));      // one concept, twice
}

ITEST(no_head_and_two_top_class_concepts_are_intent_none) {
    ITEST_TRUE(select_head(ClauseExtraction{}).status == HeadStatus::NoHead);
    ITEST_TRUE(select_head(synthetic({match(0u, "send"), match(1u, "stop")})).status == HeadStatus::TwoTopClass);
    ITEST_TRUE(select_head(synthetic({match(0u, "fire"), match(1u, "flood")})).status == HeadStatus::TwoTopClass);
    ITEST_TRUE(select_head(synthetic({match(0u, "injured"), match(1u, "trapped")})).status == HeadStatus::TwoTopClass);
    HeadSelection h = select_head(synthetic({match(0u, "send"), match(1u, "fire")}));
    ITEST_TRUE(h.status == HeadStatus::Found && h.concept_id == cid("send"));
    ITEST_TRUE(select_head(synthetic({match(0u, "send"), match(0u, "food")})).status == HeadStatus::Ambiguous);

    const Context ctx = empty_context();
    ITEST_TRUE(encode("en", "okay", ctx).e.outcome == Tier1Outcome::NoHead);
    ITEST_TRUE(encode("en", "Fire flood at the bridge", ctx).e.outcome == Tier1Outcome::TwoTopClass);
}

// ---------------------------------------------------------------------------
// Rule table — tier §5.4, §12.1
// ---------------------------------------------------------------------------

ITEST(rule_buckets_are_sorted_by_rule_priority_and_the_first_match_wins) {
    u32 n = 0u;
    const Rule* bucket = fx().rules.bucket(cid("send"), n);
    ITEST_EQ(n, 5u);
    for (u32 i = 0u; bucket != nullptr && i + 1u < n; ++i) ITEST_TRUE(bucket[i].rule_priority > bucket[i + 1u].rule_priority);
    ITEST_EQ(bucket == nullptr ? 0u : bucket[0].rule_priority, 200u);

    // medicine is MEDICAL and SUPPLY: the higher-priority MEDICAL rule wins.
    const Context ctx = empty_context();
    ITEST_EQ(encode("en", "Send medicine to the hospital", ctx).e.frame.intent, iid("REQUEST_MEDICAL_AT"));
    ITEST_EQ(encode("en", "Send food to the market", ctx).e.frame.intent, iid("REQUEST_SUPPLY_AT"));
    ITEST_EQ(encode("en", "Send water", ctx).e.frame.intent, iid("REQUEST_GENERIC"));

    const UtteranceExtraction x = fx().base.lang.extract("en", "Send medicine to the hospital");
    const Rule* rule = nullptr;
    ITEST_TRUE(match_rule(fx().rules, common(), cid("send"), x.clauses.at(0), rule) == RuleMatch::Matched &&
               rule->intent == iid("REQUEST_MEDICAL_AT"));

    // A table compiled in any order is stored in evaluation order; a shared
    // rule_priority in one bucket is refused.
    const Rule low{cid("move"), 5u, iid("REQUEST_MOVE"), 0u, {}};
    const Rule high{cid("move"), 50u, iid("REQUEST_MOVE"), 1u, {Condition{SLOT_ACTOR, 0u, 0u}}};
    PackFiles files;
    files.emplace_back("rules.bin", serialize_rules({low, high}, {}));
    RuleTable table;
    std::string error;
    ITEST_TRUE(table.load(files, common(), error));
    u32 count = 0u;
    const Rule* moved = table.bucket(cid("move"), count);
    ITEST_TRUE(count == 2u && moved[0].rule_priority == 50u && moved[1].rule_priority == 5u);
    files[0].second = serialize_rules({low, low}, {});
    ITEST_TRUE(!table.load(files, common(), error) && contains(error, "share a rule_priority"));
}

ITEST(neg_set_selects_a_different_intent_not_a_flag_on_the_same_one) {
    const Context ctx = empty_context();
    const Encoded positive = encode("en", "Send water", ctx);
    const Encoded negative = encode("en", "Do not send water", ctx);
    ITEST_TRUE(positive.e.outcome == Tier1Outcome::Ok && negative.e.outcome == Tier1Outcome::Ok);
    ITEST_EQ(positive.e.frame.intent, iid("REQUEST_GENERIC"));
    ITEST_EQ(negative.e.frame.intent, iid("CANCEL_REQUEST"));

    Tier1Decoded d;
    ITEST_TRUE(decode(negative.e.payload, d) == Tier1DecodeStatus::Ok && d.metadata.negation);
    ITEST_TRUE(decode(positive.e.payload, d) == Tier1DecodeStatus::Ok && !d.metadata.negation);

    // A negated clause whose rule does not test NEG_SET would render as its
    // positive sentence: refused, in every language.
    ITEST_TRUE(encode("en", "Police do not move", ctx).e.outcome == Tier1Outcome::NegationWithoutRule);
    ITEST_TRUE(encode("hi", corpus_text("h10"), ctx).e.outcome == Tier1Outcome::NegationWithoutRule);
    ITEST_TRUE(encode("hi", corpus_text("h03"), ctx).e.frame.intent == iid("CANCEL_REQUEST"));
    ITEST_TRUE(encode("ta", corpus_text("t03"), ctx).e.frame.intent == iid("CANCEL_REQUEST"));
    ITEST_TRUE(encode("en", "Do not send no water", ctx).e.outcome == Tier1Outcome::NegationAmbiguous);
}

ITEST(head_implied_true_means_the_head_is_not_transmitted) {
    const Context ctx = empty_context();
    // REPORT_STATE: head_implied 0 — the head is sent in its slot, and the template renders it.
    const Encoded state = encode("en", "injured", ctx);
    ITEST_TRUE(state.e.outcome == Tier1Outcome::Ok && state.e.frame.intent == iid("REPORT_STATE"));
    ITEST_TRUE(state.e.frame.slots[SLOT_STATE].mode == SlotMode::Id && state.e.frame.slots[SLOT_STATE].value == cid("injured"));

    // REPORT_CASUALTY_COUNT: head_implied 1 — "injured" is implied by the intent and not transmitted.
    const Encoded count = encode("en", "3 injured", ctx);
    ITEST_TRUE(count.e.outcome == Tier1Outcome::Ok);
    ITEST_TRUE(count.e.frame.slots[SLOT_STATE].mode == SlotMode::Absent);
    ITEST_TRUE(count.e.frame.slots[SLOT_QUANTITY].mode == SlotMode::Id && count.e.frame.slots[SLOT_QUANTITY].value == 3u);

    // The same clause under a head_implied 0 variant of that intent carries the head: one more symbol.
    const HeadSelection head = select_head(count.x.clauses.at(0));
    const IntentInfo not_implied{iid("REPORT_CASUALTY_COUNT"), slot_mask({SLOT_QUANTITY, SLOT_LOCATION, SLOT_STATE}),
                                 slot_mask({SLOT_QUANTITY}), false, true};
    Tier1Frame frame;
    const std::string text = "3 injured";
    ITEST_TRUE(resolve_slots(common(), subwords().vocabulary(), not_implied, &head, count.x.clauses.at(0),
                             reinterpret_cast<const u8*>(text.data()), text.size(), ctx, SenderPolicy{}, frame) ==
               SlotOutcome::Ok);
    ITEST_TRUE(frame.slots[SLOT_STATE].mode == SlotMode::Id && frame.slots[SLOT_STATE].value == cid("injured"));

    std::vector<Symbol> with_head;
    std::vector<Symbol> without_head;
    ITEST_TRUE(frame_to_symbols(common(), subwords().vocabulary(), frame, with_head) == FrameFault::None);
    ITEST_TRUE(frame_to_symbols(common(), subwords().vocabulary(), count.e.frame, without_head) == FrameFault::None);
    ITEST_EQ(with_head.size(), without_head.size() + 1u);   // the head's value symbol

    const Encoded fire = encode("en", "Fire at the north gate", ctx);   // head_implied 1, head fire
    ITEST_TRUE(fire.e.outcome == Tier1Outcome::Ok);
    for (const FrameSlot& fs : fire.e.frame.slots) ITEST_TRUE(fs.value != cid("fire"));
}

// ---------------------------------------------------------------------------
// Slots — tier §5.6, context §4.3, §13
// ---------------------------------------------------------------------------

ITEST(time_never_inherits) {
    const u8 timed_slots = slot_mask({SLOT_TIME, SLOT_LOCATION});
    const IntentInfo timed{99u, timed_slots, timed_slots, true, false};
    const std::string said = "3 o'clock at the bridge";
    const UtteranceExtraction x = fx().base.lang.extract("en", said);
    ITEST_TRUE(x.clauses.at(0).slots[SLOT_TIME].state == SlotState::Value && x.clauses.at(0).slots[SLOT_TIME].value == 180u);

    const Context ctx = context_with({{SLOT_TIME, u16{180}}, {SLOT_LOCATION, cid("bridge")}});
    Tier1Frame f;
    ITEST_TRUE(resolve_slots(common(), subwords().vocabulary(), timed, nullptr, x.clauses.at(0),
                             reinterpret_cast<const u8*>(said.data()), said.size(), ctx, SenderPolicy{}, f) ==
               SlotOutcome::Ok);
    ITEST_TRUE(f.slots[SLOT_TIME].mode == SlotMode::Id && f.slots[SLOT_TIME].value == 180u);   // unchanged, fresh: still explicit
    ITEST_TRUE(f.slots[SLOT_LOCATION].mode == SlotMode::Inherit);                               // the same case on LOCATION inherits

    // (No unmatched words: they would become a literal for the missing slot.)
    const std::string no_time = "bridge";
    const UtteranceExtraction y = fx().base.lang.extract("en", no_time);
    ITEST_TRUE(resolve_slots(common(), subwords().vocabulary(), timed, nullptr, y.clauses.at(0),
                             reinterpret_cast<const u8*>(no_time.data()), no_time.size(), ctx, SenderPolicy{}, f) ==
               SlotOutcome::RequiredSlotMissing);

    // The frame format refuses it, the static model gives it p = 0, and a
    // decoder cannot produce it.
    const CommonPack timed_common = make_common({timed});
    Tier1Frame bad;
    bad.intent = 99u;
    bad.slots[SLOT_LOCATION].mode  = SlotMode::Id;
    bad.slots[SLOT_LOCATION].value = cid("bridge");
    std::vector<Symbol> symbols;
    for (const SlotMode m : {SlotMode::Inherit, SlotMode::Ref}) {
        bad.slots[SLOT_TIME].mode = m;
        ITEST_TRUE(frame_to_symbols(timed_common, subwords().vocabulary(), bad, symbols) == FrameFault::TimeInherited);
    }
    const Tier1Model model(timed_common, subwords().ngram());
    ITEST_TRUE(model.valid());
    const Symbol prefix[2] = {0u, static_cast<Symbol>(SlotMode::Id)};   // intent, then LOCATION's mode
    const Model& modes = model.model_at(2u, prefix);                    // TIME's mode
    ITEST_TRUE(modes.range_of(static_cast<u32>(SlotMode::Inherit)).high == modes.range_of(static_cast<u32>(SlotMode::Inherit)).low);
    ITEST_TRUE(modes.range_of(static_cast<u32>(SlotMode::Ref)).high == modes.range_of(static_cast<u32>(SlotMode::Ref)).low);
    ITEST_TRUE(modes.range_of(static_cast<u32>(SlotMode::Id)).high > modes.range_of(static_cast<u32>(SlotMode::Id)).low);
    const Symbol stream[3] = {0u, static_cast<Symbol>(SlotMode::Id), static_cast<Symbol>(SlotMode::Inherit)};
    Tier1Frame out;
    ITEST_TRUE(!symbols_to_frame(timed_common, subwords().vocabulary(), stream, 3u, out));

    Context committed = ctx;
    CommitPayload p{};
    p.slots[SLOT_TIME].op = SlotOp::Inherit;
    ITEST_TRUE(commit(committed, p) == CommitResult::TimeInherited);
}

ITEST(staleness_is_sender_side_and_eventually_forces_explicit_values) {
    SenderPolicy policy;
    policy.max_inherit_age = 3u;
    for (u32 age = 0u; age <= 5u; ++age) {
        Context ctx = context_with({{SLOT_LOCATION, cid("hospital")}});
        age_context(ctx, age);
        ITEST_EQ(ctx.slots[SLOT_LOCATION].age, age);
        const Encoded e = encode("en", "Send an ambulance to the hospital", ctx, 900, false, nullptr, policy);
        ITEST_TRUE(e.e.outcome == Tier1Outcome::Ok);
        ITEST_TRUE(e.e.frame.slots[SLOT_LOCATION].mode == (age <= 3u ? SlotMode::Inherit : SlotMode::Id));
    }

    // INHERIT does not reset age: inheriting every message goes stale.
    // Ages 0, 1, 2, 3 inherit; at age 4 the value is stale and sent explicitly,
    // which resets age (a write); round 5 inherits again.
    Context ctx = context_with({{SLOT_LOCATION, cid("hospital")}});
    const SlotMode expected[6] = {SlotMode::Inherit, SlotMode::Inherit, SlotMode::Inherit,
                                  SlotMode::Inherit, SlotMode::Id,      SlotMode::Inherit};
    for (u32 round = 0u; round < 6u; ++round) {
        const Encoded e = encode("en", "Send an ambulance to the hospital", ctx, 900, false, nullptr, policy);
        ITEST_TRUE(e.e.outcome == Tier1Outcome::Ok);
        ITEST_TRUE(e.e.frame.slots[SLOT_LOCATION].mode == expected[round]);
        ITEST_TRUE(commit(ctx, e.e.commit) == CommitResult::Ok);
    }

    // Inheritance off: a fully explicit message with no hash.
    policy.allow_inheritance = false;
    const Context fresh = context_with({{SLOT_LOCATION, cid("hospital")}});
    const Encoded e = encode("en", "Send an ambulance to the hospital", fresh, 900, false, nullptr, policy);
    ITEST_TRUE(e.e.outcome == Tier1Outcome::Ok && e.e.frame.slots[SLOT_LOCATION].mode == SlotMode::Id);
    Tier1Decoded d;
    ITEST_TRUE(decode(e.e.payload, d) == Tier1DecodeStatus::Ok && !d.metadata.hash_present);
}

ITEST(literals_encode_via_the_tier2_subword_coder_without_boost_or_history) {
    const SubwordVocabulary& vocab = subwords().vocabulary();
    const std::vector<std::string> literals = {
        "Ravi",
        cps({0x0930, 0x092E, 0x0947, 0x0936}),                                 // Devanagari
        cps({0x0BAE, 0x0BC1, 0x0BB0, 0x0BC1, 0x0B95, 0x0BA9, 0x0BCD}),         // Tamil
        cps({0x674E, 0x660E}),                                                 // CJK: byte fallback
        cps({0x1F691}),                                                        // emoji
        t2fx::bytes({0xFFu, 0xC0u, 0x80u}),                                    // invalid UTF-8
        "north gate",                                                          // a whole subword
    };
    const Context busy = context_with({{SLOT_LOCATION, cid("north_gate")}, {SLOT_OBJECT, cid("ambulance")}});
    ContextBoost boost;
    boost.build(subwords().boost(), busy);
    ITEST_TRUE(boost.size() != 0u);

    for (const std::string& literal : literals) {
        Tier1Frame f;
        f.intent = iid("REQUEST_MOVE");   // expects ACTOR, LOCATION
        f.slots[SLOT_ACTOR].mode    = SlotMode::Literal;
        f.slots[SLOT_ACTOR].literal = literal;
        std::vector<Symbol> symbols;
        ITEST_TRUE(frame_to_symbols(common(), vocab, f, symbols) == FrameFault::None);
        std::vector<Symbol> tokens;
        vocab.tokenize(reinterpret_cast<const u8*>(literal.data()), literal.size(), tokens);
        // [intent][ACTOR mode][LOCATION mode][length][tokens…]
        ITEST_EQ(symbols.size(), 4u + tokens.size());
        ITEST_EQ(symbols.size() > 3u ? symbols[3] : 0u, tokens.size());
        ITEST_TRUE(symbols.size() == 4u + tokens.size() && std::equal(tokens.begin(), tokens.end(), symbols.begin() + 4));

        // Every literal token is coded under the UNBOOSTED Tier 2 table, with a
        // history that starts at the literal — whatever the context holds.
        const Tier1Model tier1(common(), subwords().ngram());
        const Tier2Model plain(subwords().ngram(), nullptr);
        const Tier2Model boosted(subwords().ngram(), &boost);
        u32 differs_from_plain = 0u;
        u32 differs_from_boost = 0u;
        for (u32 k = 0u; k < tokens.size() && symbols.size() == 4u + tokens.size(); ++k) {
            const SymbolRange a = tier1.model_at(4u + k, symbols.data()).range_of(tokens[k]);
            const SymbolRange b = plain.model_at(k, tokens.data()).range_of(tokens[k]);
            const SymbolRange c = boosted.model_at(k, tokens.data()).range_of(tokens[k]);
            if (a.low != b.low || a.high != b.high || a.total != b.total) ++differs_from_plain;
            if (a.low != c.low || a.high != c.high || a.total != c.total) ++differs_from_boost;
        }
        ITEST_EQ(differs_from_plain, 0u);
        ITEST_TRUE(differs_from_boost != 0u);   // the boost would have changed it; it was not applied

        Tier1Decoded d;
        ITEST_TRUE(decode(assemble_frame(f, false, false, 0u), d) == Tier1DecodeStatus::Ok && d.frame == f);
    }
}

ITEST(literals_are_never_written_to_context) {
    Context ctx = context_with({{SLOT_ACTOR, cid("police")}});
    const Encoded e = encode("en", "Tell Ravi to move", ctx);
    ITEST_TRUE(e.e.outcome == Tier1Outcome::Ok);
    ITEST_TRUE(e.e.frame.slots[SLOT_ACTOR].mode == SlotMode::Literal && e.e.frame.slots[SLOT_ACTOR].literal == "Tell Ravi to");
    ITEST_TRUE(e.e.commit.slots[SLOT_ACTOR].op == SlotOp::Literal && e.e.commit.slots[SLOT_ACTOR].value == 0u);

    const Slot before = ctx.slots[SLOT_ACTOR];
    ITEST_TRUE(commit(ctx, e.e.commit) == CommitResult::Ok);
    ITEST_EQ(ctx.slots[SLOT_ACTOR].current, before.current);
    ITEST_EQ(ctx.slots[SLOT_ACTOR].ver, before.ver);
    ITEST_EQ(ctx.slots[SLOT_ACTOR].age, before.age + 1u);

    Tier1Decoded d;
    ITEST_TRUE(decode(e.e.payload, d) == Tier1DecodeStatus::Ok);
    ITEST_TRUE(d.frame.slots[SLOT_ACTOR].literal == "Tell Ravi to");
    ITEST_TRUE(frame_commit_payload(d.frame, 9u).slots[SLOT_ACTOR].op == SlotOp::Literal);
}

// ---------------------------------------------------------------------------
// Adjacency — tier §5.2
// ---------------------------------------------------------------------------

ITEST(adjacency_fsm_answers_bare_values_and_times_out) {
    const Context ctx = empty_context();
    const std::string three = cps({0x0BE9});   // Tamil digit three
    AdjacencyFsm fsm;
    ITEST_TRUE(fsm.state() == AdjacencyState::Neutral);
    ITEST_TRUE(encode("ta", three, ctx, 900, false, &fsm).e.outcome == Tier1Outcome::NoHead);

    fsm.on_query(iid("QUERY_CASUALTY_COUNT"), 100u, 50u);
    ITEST_TRUE(fsm.state() == AdjacencyState::AwaitingAnswer && fsm.pending_query() == iid("QUERY_CASUALTY_COUNT"));
    const Encoded answered = encode("ta", three, ctx, 900, false, &fsm);
    ITEST_TRUE(answered.e.outcome == Tier1Outcome::Ok && answered.e.answered);
    ITEST_EQ(answered.e.frame.intent, iid("ANSWER_COUNT"));
    ITEST_TRUE(answered.e.frame.slots[SLOT_QUANTITY].mode == SlotMode::Id && answered.e.frame.slots[SLOT_QUANTITY].value == 3u);

    fsm.tick(149u);
    ITEST_TRUE(fsm.state() == AdjacencyState::AwaitingAnswer);
    fsm.tick(150u);   // timeout
    ITEST_TRUE(fsm.state() == AdjacencyState::Neutral && fsm.pending_query() == 0u);
    ITEST_TRUE(encode("ta", three, ctx, 900, false, &fsm).e.outcome == Tier1Outcome::NoHead);

    fsm.on_query(iid("QUERY_CASUALTY_COUNT"), 10u, ~u64{0});   // deadline saturates
    fsm.tick(~u64{0} - 1u);
    ITEST_TRUE(fsm.state() == AdjacencyState::AwaitingAnswer);
    fsm.on_message_sent();   // any message sent
    ITEST_TRUE(fsm.state() == AdjacencyState::Neutral);

    // Only a query in the answer table has answers; a clause with a concept is not a bare value.
    fsm.on_query(iid("REQUEST_MOVE"), 0u, 100u);
    ITEST_TRUE(encode("ta", three, ctx, 900, false, &fsm).e.outcome == Tier1Outcome::NoHead);
    fsm.on_query(iid("QUERY_CASUALTY_COUNT"), 0u, 100u);
    ITEST_TRUE(encode("en", "3 injured", ctx, 900, false, &fsm).e.frame.intent == iid("REPORT_CASUALTY_COUNT"));
    ITEST_TRUE(fx().rules.is_query(iid("QUERY_CASUALTY_COUNT")) && !fx().rules.is_query(iid("REQUEST_MOVE")));
}

// ---------------------------------------------------------------------------
// Static model, frames, numbers — tier §5.9
// ---------------------------------------------------------------------------

ITEST(tier1_model_is_static_no_context_boost) {
    const Context empty = empty_context();
    const Context busy  = context_with({{SLOT_LOCATION, cid("north_gate")}, {SLOT_OBJECT, cid("water")}, {SLOT_ACTOR, cid("police")}});
    const Encoded a = encode("en", "Send an ambulance to the hospital", empty);
    const Encoded b = encode("en", "Send an ambulance to the hospital", busy);
    ITEST_TRUE(a.e.outcome == Tier1Outcome::Ok && b.e.outcome == Tier1Outcome::Ok);
    ITEST_TRUE(!frame_uses_context(a.e.frame) && a.e.payload == b.e.payload);

    // With inheritance, two contexts that agree on LOCATION but differ elsewhere:
    // only the 12-bit context hash differs; every coded bit is identical.
    const Context c1 = context_with({{SLOT_LOCATION, cid("hospital")}});
    const Context c2 = context_with({{SLOT_LOCATION, cid("hospital")}, {SLOT_ACTOR, cid("police")}});
    const Encoded i1 = encode("en", "Send an ambulance to the hospital", c1);
    const Encoded i2 = encode("en", "Send an ambulance to the hospital", c2);
    ITEST_TRUE(i1.e.outcome == Tier1Outcome::Ok && i2.e.outcome == Tier1Outcome::Ok);
    ITEST_TRUE(frame_uses_context(i1.e.frame) && i1.e.symbols == i2.e.symbols && i1.e.payload != i2.e.payload);
    ITEST_TRUE(i1.e.payload.size() == i2.e.payload.size() && i1.e.metadata_bits == i2.e.metadata_bits);
    u32 differing = 0u;
    for (u32 bit = i1.e.metadata_bits; bit < i1.e.payload.size() * 8u && i1.e.payload.size() == i2.e.payload.size(); ++bit) {
        if (bit_at(i1.e.payload, bit) != bit_at(i2.e.payload, bit)) ++differing;
    }
    ITEST_EQ(differing, 0u);
}

ITEST(frames_round_trip_through_symbols_and_payloads) {
    t2fx::XorShift32 rng{0x7E1FA11u};
    std::vector<std::vector<u16>> by_slot(kConceptSlotCount);
    for (const ConceptInfo& c : common().concepts()) {
        if (c.slot < kConceptSlotCount) by_slot[c.slot].push_back(c.id);
    }
    const std::vector<IntentInfo>& intents = common().intents();
    u32 modes[kSlotModeCount] = {};
    u32 failures = 0u;
    for (u32 round = 0u; round < 2000u; ++round) {
        const IntentInfo& intent = intents[rng.next() % intents.size()];
        Tier1Frame f;
        f.intent = intent.id;
        for (u32 s = 0u; s < kConceptSlotCount; ++s) {
            if (((intent.expected_slots >> s) & 1u) == 0u) continue;
            const bool required = ((intent.required_slots >> s) & 1u) != 0u;
            SlotMode m = SlotMode::Absent;
            for (;;) {
                m = static_cast<SlotMode>(rng.next() % kSlotModeCount);
                if (required && m == SlotMode::Absent) continue;
                if (s == SLOT_TIME && (m == SlotMode::Inherit || m == SlotMode::Ref)) continue;
                break;
            }
            FrameSlot& fs = f.slots[s];
            fs.mode = m;
            if (m == SlotMode::Id) {
                fs.value = (!by_slot[s].empty() && (rng.next() & 1u) != 0u)
                               ? by_slot[s][rng.next() % by_slot[s].size()]
                               : static_cast<u16>(1u + rng.next() % 65535u);
            } else if (m == SlotMode::Literal) {
                const u32 n = 1u + rng.next() % 24u;
                for (u32 k = 0u; k < n; ++k) fs.literal.push_back(static_cast<char>(static_cast<u8>(rng.next())));
            }
            ++modes[static_cast<u32>(m)];
        }
        std::vector<Symbol> symbols;
        Tier1Frame back;
        bool ok = frame_to_symbols(common(), subwords().vocabulary(), f, symbols) == FrameFault::None &&
                  symbols_to_frame(common(), subwords().vocabulary(), symbols.data(), static_cast<u32>(symbols.size()), back) &&
                  back == f;
        if (ok) {
            const bool hash = frame_uses_context(f);
            Tier1Decoded d;
            ok = decode(assemble_frame(f, (round & 1u) != 0u, hash, hash ? 0x5A5u : 0u), d) == Tier1DecodeStatus::Ok &&
                 d.frame == f && d.metadata.negation == ((round & 1u) != 0u);
        }
        if (!ok) ++failures;
    }
    ITEST_EQ(failures, 0u);
    std::printf("  frames: 2000 random frames, modes Absent %u Inherit %u Ref %u Id %u Literal %u, 0 failures\n",
                modes[0], modes[1], modes[2], modes[3], modes[4]);
}

ITEST(numbers_use_bit_length_classes_and_never_collide_with_concepts) {
    const SubwordVocabulary& vocab = subwords().vocabulary();
    for (const u32 v : {1u, 2u, 3u, 255u, 256u, 65535u}) {
        Tier1Frame f;
        f.intent = iid("ANSWER_COUNT");
        f.slots[SLOT_QUANTITY].mode  = SlotMode::Id;
        f.slots[SLOT_QUANTITY].value = static_cast<u16>(v);
        std::vector<Symbol> symbols;
        ITEST_TRUE(frame_to_symbols(common(), vocab, f, symbols) == FrameFault::None);
        u32 bits = 0u;
        for (u32 x = v; x != 0u; x >>= 1) ++bits;
        ITEST_EQ(symbols.size(), 4u + (bits - 1u));   // intent, mode, escape, class, low bits
        Tier1Decoded d;
        ITEST_TRUE(decode(assemble_frame(f, false, false, 0u), d) == Tier1DecodeStatus::Ok && d.frame == f);
    }

    // LOCATION escape + the number 10, which is the LOCATION concept north_gate.
    std::vector<u16> locations;
    for (const ConceptInfo& c : common().concepts()) {
        if (c.slot == SLOT_LOCATION) locations.push_back(c.id);
    }
    ITEST_EQ(cid("north_gate"), 10u);
    u32 fire_index = 0u;
    for (u32 i = 0u; i < common().intents().size(); ++i) {
        if (common().intents()[i].id == iid("REPORT_FIRE_AT")) fire_index = i;
    }
    const Symbol stream[] = {fire_index, static_cast<Symbol>(SlotMode::Id), static_cast<Symbol>(SlotMode::Absent),
                             static_cast<Symbol>(locations.size()), 3u, 0u, 1u, 0u};
    Tier1Frame out;
    ITEST_TRUE(!symbols_to_frame(common(), vocab, stream, 8u, out));

    // The sender refuses to send a number that reads as a concept.
    ClauseExtraction c = synthetic({match(0u, "fire")});
    c.values.push_back(TypedValue{SLOT_LOCATION, cid("north_gate"), SourceSpan{0u, 0u}});
    assign_slots(c, kConceptSlotMask);
    const HeadSelection head = select_head(c);
    Tier1Frame f;
    ITEST_TRUE(resolve_slots(common(), vocab, *common().intent(iid("REPORT_FIRE_AT")), &head, c, nullptr, 0u,
                             empty_context(), SenderPolicy{}, f) == SlotOutcome::ValueKindCollision);
}

// ---------------------------------------------------------------------------
// Receiver behaviour — receiver §3④ ⑦, §4, §6.1
// ---------------------------------------------------------------------------

ITEST(hash_mismatch_decodes_with_only_inherited_slots_unresolved) {
    const Context sender = context_with({{SLOT_LOCATION, cid("hospital")}});
    const Encoded e = encode("en", "Send an ambulance to the hospital", sender);
    ITEST_TRUE(e.e.outcome == Tier1Outcome::Ok && e.e.frame.slots[SLOT_LOCATION].mode == SlotMode::Inherit);
    Tier1Decoded d;
    ITEST_TRUE(decode(e.e.payload, d) == Tier1DecodeStatus::Ok && d.metadata.hash_present);

    std::string text;
    const Context same = sender;
    const Tier1Received good = tier1_resolve(common(), d, &same);
    ITEST_TRUE(good.context_matched && good.resolved.unresolved_slots == 0u);
    ITEST_TRUE(tier1_render(pack("en"), d, good, text) == Tier1RenderStatus::Ok && text == "Send an ambulance to the hospital");

    const Context other = empty_context();
    const Tier1Received bad = tier1_resolve(common(), d, &other);
    ITEST_TRUE(!bad.context_matched);
    ITEST_EQ(bad.resolved.unresolved_slots, 1u << SLOT_LOCATION);
    ITEST_TRUE(bad.resolved.slots[SLOT_OBJECT].kind == RenderKind::Concept && bad.resolved.slots[SLOT_OBJECT].concept_id == cid("ambulance"));
    ITEST_TRUE(bad.resolved.slots[SLOT_LOCATION].kind == RenderKind::Absent);
    ITEST_TRUE(tier1_render(pack("en"), d, bad, text) == Tier1RenderStatus::Unresolved && text.empty());
    ITEST_EQ(tier1_resolve(common(), d, nullptr).resolved.unresolved_slots, 1u << SLOT_LOCATION);

    // An explicit message decodes and renders whatever the receiver holds.
    const Encoded x = encode("en", "Send an ambulance to the hospital", empty_context());
    Tier1Decoded dx;
    ITEST_TRUE(decode(x.e.payload, dx) == Tier1DecodeStatus::Ok && !dx.metadata.hash_present);
    ITEST_TRUE(tier1_render(pack("en"), dx, tier1_resolve(common(), dx, &sender), text) == Tier1RenderStatus::Ok);

    // hash_present must say exactly whether the frame uses context.
    Tier1Decoded lying;
    ITEST_TRUE(decode(assemble_frame(x.e.frame, false, true, 0x123u), lying) == Tier1DecodeStatus::Malformed);
    ITEST_TRUE(decode(assemble_frame(e.e.frame, false, false, 0u), lying) == Tier1DecodeStatus::Malformed);
}

ITEST(ref_decodes_commits_and_resolves_like_inherit) {
    Tier1Frame f;
    f.intent = iid("REQUEST_MEDICAL_AT");
    f.slots[SLOT_OBJECT].mode    = SlotMode::Id;
    f.slots[SLOT_OBJECT].value   = cid("ambulance");
    f.slots[SLOT_LOCATION].mode  = SlotMode::Ref;
    Context receiver = context_with({{SLOT_LOCATION, cid("hospital")}});
    Tier1Decoded d;
    ITEST_TRUE(decode(assemble_frame(f, false, true, wire_context_hash(context_hash(receiver))), d) == Tier1DecodeStatus::Ok);
    ITEST_TRUE(d.frame == f);
    const Tier1Received r = tier1_resolve(common(), d, &receiver);
    ITEST_TRUE(r.context_matched && r.resolved.slots[SLOT_LOCATION].concept_id == cid("hospital"));
    const CommitPayload p = frame_commit_payload(d.frame, 3u);
    ITEST_TRUE(p.slots[SLOT_LOCATION].op == SlotOp::Ref && p.slots[SLOT_OBJECT].op == SlotOp::Write);
    const Slot before = receiver.slots[SLOT_LOCATION];
    ITEST_TRUE(commit(receiver, p) == CommitResult::Ok);
    ITEST_TRUE(receiver.slots[SLOT_LOCATION].current == before.current && receiver.slots[SLOT_LOCATION].age == before.age + 1u);

    // The Phase 8 sender never emits Ref.
    u32 refs = 0u;
    for (const auto& kv : fx().tier1_text) {
        for (const char* lang : {"hi", "ta", "en"}) {
            const Encoded e = encode(lang, kv.second, context_with({{SLOT_LOCATION, cid("hospital")}, {SLOT_OBJECT, cid("water")}}));
            for (const FrameSlot& fs : e.e.frame.slots) refs += fs.mode == SlotMode::Ref ? 1u : 0u;
        }
    }
    ITEST_EQ(refs, 0u);
}

ITEST(a_negation_copy_disagreement_is_rejected_never_decoded) {
    const Encoded neg = encode("en", "Do not send water", empty_context());
    ITEST_TRUE(neg.e.outcome == Tier1Outcome::Ok && neg.e.symbols.size() < 31u);
    for (const u8 mask : {u8{0x40}, u8{0x20}}) {   // metadata bits 17 and 18: the two negation copies
        std::vector<u8> bytes = neg.e.payload;
        bytes[2] = static_cast<u8>(bytes[2] ^ mask);
        Tier1Decoded d;
        ITEST_TRUE(decode(bytes, d) == Tier1DecodeStatus::NegationMismatch);
    }
}

// ---------------------------------------------------------------------------
// Language data and gates
// ---------------------------------------------------------------------------

ITEST(lang_id_mapping_is_frozen) {
    const char* const order[] = {"hi", "gu", "mr", "kn", "ml", "ta", "te", "or", "bn", "en"};
    for (u32 i = 0u; i < 10u; ++i) {
        u8 id = 0u;
        ITEST_TRUE(lang_id_of(order[i], id) && id == i + 1u);
        const SupportedLanguage* l = language_of_id(static_cast<u8>(i + 1u));
        ITEST_TRUE(l != nullptr && std::strcmp(l->code, order[i]) == 0);
    }
    ITEST_TRUE(language_of_id(kLangIdUnassigned) == nullptr);
    for (u32 id = 11u; id <= 255u; ++id) ITEST_TRUE(language_of_id(static_cast<u8>(id)) == nullptr);
    u8 unchanged = 7u;
    ITEST_TRUE(!lang_id_of("xx", unchanged) && unchanged == 7u);
    ITEST_EQ(kLangIdMax, kMaxLanguage);
}

ITEST(negation_words_are_extracted_per_clause_and_are_not_unmatched_text) {
    UtteranceExtraction x = fx().base.lang.extract("en", "Do not send water");
    ITEST_TRUE(x.clauses.size() == 1u && x.clauses[0].negations.size() == 1u && x.clauses[0].unmatched.empty());
    ITEST_EQ(x.clauses[0].concepts.size(), 2u);
    x = fx().base.lang.extract("en", "Send water");
    ITEST_TRUE(!x.clauses.at(0).negated());
    x = fx().base.lang.extract("en", "don't send water");
    ITEST_TRUE(x.clauses.at(0).negations.size() == 1u && x.clauses.at(0).unmatched.empty());
    for (const char* id : {"h03", "t03", "h10"}) {
        const std::string lang = id[0] == 'h' ? "hi" : "ta";
        x = fx().base.lang.extract(lang, corpus_text(id));
        ITEST_TRUE(x.clauses.size() == 1u && x.clauses[0].negations.size() == 1u && x.clauses[0].unmatched.empty());
    }
}

ITEST(readback_bars_are_per_language_and_critical_is_higher) {
    ITEST_TRUE(pack("en").readback_normal() == 600u && pack("en").readback_critical() == 800u);
    ITEST_TRUE(pack("hi").readback_normal() == 600u && pack("hi").readback_critical() == 800u);
    ITEST_TRUE(pack("ta").readback_normal() == 550u && pack("ta").readback_critical() == 750u);

    PackFiles files;
    std::string error;
    ITEST_TRUE(read_pack_directory(fx().packs_dir + "/lang/en", LanguagePack::file_names(), files, error));
    const auto loads = [&files](const std::string& json) {
        PackFiles f = files;
        for (PackFile& file : f) {
            if (file.first != "meta.json") continue;
            file.second.clear();
            for (char ch : json) file.second.push_back(static_cast<u8>(ch));
        }
        LanguagePack p;
        std::string why;
        return p.load(std::move(f), common(), why);
    };
    const std::string head =
        "{\"language\":\"en\",\"pack_version\":1,\"script\":\"Latn\",\"tts_voice\":\"x\",\"stt_confidence_threshold\":700";
    ITEST_TRUE(loads(head + ",\"readback_normal\":600,\"readback_critical\":800}"));
    ITEST_TRUE(!loads(head + ",\"readback_normal\":600,\"readback_critical\":600}"));
    ITEST_TRUE(!loads(head + ",\"readback_normal\":600,\"readback_critical\":1001}"));
    ITEST_TRUE(!loads(head + "}"));
}

ITEST(word_similarity_is_integer_per_mille_over_tokens) {
    ITEST_EQ(word_similarity_permille("a b c", "a b c"), 1000u);
    ITEST_EQ(word_similarity_permille("", ""), 1000u);
    ITEST_EQ(word_similarity_permille("a", ""), 0u);
    ITEST_EQ(word_similarity_permille("a b", "b a"), 0u);
    ITEST_EQ(word_similarity_permille("  a   b ", "a b"), 1000u);
    ITEST_EQ(word_similarity_permille("send an ambulance to the hospital", "send an ambulance at the hospital"), 833u);
    ITEST_EQ(word_similarity_permille("send blankets now to the relief camp", "send blankets at the relief camp"), 714u);
}

ITEST(low_stt_confidence_disables_tier1_outright) {
    const Context ctx = empty_context();
    ITEST_TRUE(encode("en", "Fire at the north gate", ctx, 699).e.outcome == Tier1Outcome::LowConfidence);
    ITEST_TRUE(encode("en", "Fire at the north gate", ctx, 700).e.outcome == Tier1Outcome::Ok);
    ITEST_TRUE(encode("en", "okay", ctx, 0).e.outcome == Tier1Outcome::LowConfidence);   // checked before anything else
    ITEST_TRUE(encode("ta", corpus_text("t01"), ctx, 549).e.outcome == Tier1Outcome::LowConfidence);
    ITEST_TRUE(encode("ta", corpus_text("t01"), ctx, 550).e.outcome == Tier1Outcome::Ok);
}

ITEST(critical_is_raised_by_is_alert_or_the_override_and_never_lowered) {
    const Context ctx = empty_context();
    Tier1Decoded d;
    const Encoded normal = encode("en", "Police move", ctx);
    ITEST_TRUE(normal.e.priority == Priority::Normal && normal.e.readback.bar == 600u);
    const Encoded raised = encode("en", "Police move", ctx, 900, true);
    ITEST_TRUE(raised.e.priority == Priority::Critical && raised.e.readback.bar == 800u);
    ITEST_TRUE(decode(raised.e.payload, d) == Tier1DecodeStatus::Ok && d.metadata.priority == Priority::Critical);
    const Encoded alert = encode("en", "Fire at the north gate", ctx);   // is_alert
    ITEST_TRUE(alert.e.priority == Priority::Critical);
    ITEST_TRUE(encode("en", "Fire at the north gate", ctx, 900, true).e.priority == Priority::Critical);
    ITEST_TRUE(decode(normal.e.payload, d) == Tier1DecodeStatus::Ok && d.metadata.priority == Priority::Normal);
}

ITEST(the_rule_compiler_rejects_malformed_tables_and_accepts_the_fixture) {
    const std::string src     = fx().base.lang.src_dir;
    const std::string answers = src + "/sender/answers.tsv";
    const rulec::CompileResult ok = rulec::compile(src + "/common", src + "/sender/rules.tsv", answers);
    ITEST_TRUE(ok.ok && ok.rule_count == 20u && ok.answer_count == 1u);
    std::string built;
    ITEST_TRUE(langfx::read_file(fx().packs_dir + "/sender/rules.bin", built));
    ITEST_TRUE(built.size() == ok.rules_bin.size() && std::memcmp(built.data(), ok.rules_bin.data(), built.size()) == 0);

    struct Bad {
        const char* file;
        const char* message;
    };
    const Bad bad[] = {
        {"dup_priority.tsv", "duplicate rule_priority 100 in bucket send"},
        {"unreachable_intent.tsv", "intent REQUEST_STOP is not reachable"},
        {"uncovered_concept.tsv", "STATE concept trapped has no rule"},
        {"undeclared_slot.tsv", "condition on OBJECT, which intent REQUEST_EVACUATE_AT does not declare"},
    };
    for (const Bad& b : bad) {
        const rulec::CompileResult r = rulec::compile(src + "/common", fx().rulec_dir + "/" + b.file, answers);
        ITEST_TRUE(!r.ok && contains(r.error, b.message));
        if (r.ok || !contains(r.error, b.message)) std::printf("  %s: %s\n", b.file, r.error.c_str());
    }
}

ITEST(shared_tier1_code_includes_nothing_sender_only) {
    ITEST_EQ(fx().shared_sources.size(), 4u);
    const char* const forbidden[] = {"tier1/rules.h", "tier1/head.h", "tier1/slots.h", "tier1/readback.h",
                                     "tier1/encode.h", "tier1/adjacency.h", "tier1/rulec/"};
    for (const std::string& path : fx().shared_sources) {
        std::string text;
        ITEST_TRUE(langfx::read_file(path, text));
        for (const char* f : forbidden) {
            const bool found = text.find(std::string("#include \"") + f) != std::string::npos;
            ITEST_TRUE(!found);
            if (found) std::printf("  %s includes %s\n", path.c_str(), f);
        }
    }
}

// ---------------------------------------------------------------------------
// Read-back safety R1 / R2 (fixed before commit, after an independent review)
// ---------------------------------------------------------------------------

ITEST(readback_refuses_unexplained_spoken_words_r1) {
    const Context ctx = empty_context();
    // An unlisted negation word is never filler. Before R1 these were sent as
    // "Send water" at 666/600.
    const Encoded never = encode("en", "Never send water", ctx);
    ITEST_TRUE(never.e.outcome == Tier1Outcome::ReadbackUnexplainedWord && never.e.readback.unexplained == "never");
    ITEST_TRUE(never.e.readback.similarity >= never.e.readback.bar);   // the bar alone would have passed it
    const std::string na = cps({0x092A, 0x093E, 0x0928, 0x0940, 0x20, 0x0928, 0x20, 0x092D, 0x0947, 0x091C, 0x094B});
    const Encoded hindi = encode("hi", na, ctx);
    ITEST_TRUE(hindi.e.outcome == Tier1Outcome::ReadbackUnexplainedWord && hindi.e.readback.unexplained == cps({0x0928}));
    ITEST_TRUE(hindi.e.readback.similarity >= hindi.e.readback.bar);

    // A spoken value the intent does not carry; a word said twice, rendered once.
    const Encoded five = encode("en", "Police move 5", ctx);
    ITEST_TRUE(five.e.outcome == Tier1Outcome::ReadbackUnexplainedWord && five.e.readback.unexplained == "5");
    const Encoded twice = encode("en", "Police police move", ctx);
    ITEST_TRUE(twice.e.outcome == Tier1Outcome::ReadbackUnexplainedWord && twice.e.readback.unexplained == "police");
    AdjacencyFsm fsm;
    fsm.on_query(iid("QUERY_CASUALTY_COUNT"), 0u, 10u);
    ITEST_TRUE(encode("en", "three", ctx, 900, false, &fsm).e.outcome == Tier1Outcome::ReadbackUnexplainedWord);

    // Rendered words the speaker did not say are allowed; the bar still judges them.
    const Encoded terse = encode("en", "Send food market", ctx);
    ITEST_TRUE(terse.e.outcome == Tier1Outcome::Ok && terse.e.readback.covered && terse.e.readback.similarity == 600u);
    ITEST_TRUE(encode("en", "Send food market", ctx, 900, true).e.outcome == Tier1Outcome::ReadbackBelowBar);

    // Exactly the words the literal was taken from are exempt.
    const std::string said = "Tell Ravi to move";
    const UtteranceExtraction x = fx().base.lang.extract("en", said);
    Tier1Frame f;
    f.intent = iid("REQUEST_MOVE");
    f.slots[SLOT_ACTOR].mode    = SlotMode::Literal;
    f.slots[SLOT_ACTOR].literal = "Ravi";
    SourceSpan span{0u, 0u};
    ITEST_TRUE(literal_source_span(x.clauses.at(0), span));
    const ReadbackResult exempt = readback_check(pack("en"), common(), f, ctx, x.clauses.at(0), Priority::Normal, &span);
    ITEST_TRUE(exempt.rendered && exempt.covered);
    const ReadbackResult counted = readback_check(pack("en"), common(), f, ctx, x.clauses.at(0), Priority::Normal, nullptr);
    ITEST_TRUE(counted.rendered && !counted.covered && counted.unexplained == "tell");
    ITEST_TRUE(encode("en", said, ctx).e.outcome == Tier1Outcome::Ok);
}

ITEST(readback_refuses_a_transmitted_slot_the_template_does_not_render_r2) {
    const Context ctx = empty_context();
    const Encoded q = encode("en", "Send 5 blankets to the relief camp", ctx);
    ITEST_TRUE(q.e.outcome == Tier1Outcome::ReadbackUnrenderedSlot);
    ITEST_TRUE(q.e.frame.slots[SLOT_QUANTITY].mode == SlotMode::Id && q.e.frame.slots[SLOT_QUANTITY].value == 5u);
    ITEST_EQ(q.e.readback.unrendered_slots, 1u << SLOT_QUANTITY);
    ITEST_TRUE(encode("hi", corpus_text("h12"), ctx).e.outcome == Tier1Outcome::ReadbackUnrenderedSlot);

    Tier1Frame f = q.e.frame;
    f.slots[SLOT_QUANTITY] = FrameSlot{};
    const UtteranceExtraction x = fx().base.lang.extract("en", "Send blankets to the relief camp");
    const ReadbackResult r = readback_check(pack("en"), common(), f, ctx, x.clauses.at(0), Priority::Normal, nullptr);
    ITEST_TRUE(r.unrendered_slots == 0u && r.passed);
}

ITEST(pack_compiler_requires_every_language_template_to_render_the_same_required_slots_r2) {
    std::string error;
    ITEST_TRUE(rulec::check_templates(common(), {{"hi", &pack("hi")}, {"ta", &pack("ta")}, {"en", &pack("en")}}, error));

    const auto en_with = [](const std::string& intent, const std::string& text, LanguagePack& out) {
        std::vector<TemplateSource> sources;
        for (const auto& row : langfx::read_tsv(fx().base.lang.src_dir + "/lang/en/templates.tsv")) {
            if (row.at(0) == intent && text.empty()) continue;   // "" drops the intent's template
            sources.push_back(TemplateSource{fx().base.lang.intents.at(row.at(0)), row.at(0) == intent ? text : row.at(1)});
        }
        PackFiles files;
        std::string why;
        if (!read_pack_directory(fx().packs_dir + "/lang/en", LanguagePack::file_names(), files, why)) return false;
        for (PackFile& file : files) {
            if (file.first == "templates.bin") file.second = serialize_templates(sources);
        }
        return out.load(std::move(files), common(), why);
    };
    LanguagePack missing_location;
    ITEST_TRUE(en_with("REQUEST_MEDICAL_AT", "Send {OBJECT:plain}", missing_location));
    ITEST_TRUE(!rulec::check_templates(common(), {{"hi", &pack("hi")}, {"en", &missing_location}}, error) &&
               contains(error, "use different slots in hi and en"));
    LanguagePack optional_slot;
    ITEST_TRUE(en_with("REQUEST_MEDICAL_AT", "Send {QUANTITY:digits} {OBJECT:plain} {LOCATION:destination}", optional_slot));
    ITEST_TRUE(!rulec::check_templates(common(), {{"en", &optional_slot}}, error) &&
               contains(error, "uses slot QUANTITY, which the intent does not require"));

    // Coverage is never skipped (Phase 10): a missing template, or a form some
    // concept of the slot lacks, fails the build instead of a render at runtime.
    LanguagePack no_template;
    ITEST_TRUE(en_with("REPORT_FIRE_AT", "", no_template));
    ITEST_TRUE(!rulec::check_templates(common(), {{"hi", &pack("hi")}, {"en", &no_template}}, error) &&
               contains(error, "has no template in en"));
    ITEST_TRUE(!rulec::check_templates(common(), {{"en", &no_template}, {"hi", &pack("hi")}}, error) &&
               contains(error, "has no template in en"));
    LanguagePack no_form;
    // "locative" is a form of this pack (the loader rejects unknown form names),
    // but no ACTOR concept has one.
    ITEST_TRUE(en_with("REQUEST_MOVE", "{ACTOR:locative} move", no_form));
    ITEST_TRUE(!rulec::check_templates(common(), {{"en", &no_form}}, error) && contains(error, "has no form \"locative\" in en"));
}

// ---------------------------------------------------------------------------
// Literal length 1 … 255 — encoder, decoder, malformed input
// ---------------------------------------------------------------------------

ITEST(literal_length_is_bounded_1_to_255_end_to_end) {
    const SubwordVocabulary& vocab = subwords().vocabulary();
    u32 move_index = 0u;
    for (u32 i = 0u; i < common().intents().size(); ++i) {
        if (common().intents()[i].id == iid("REQUEST_MOVE")) move_index = i;
    }

    // Encoder: 255 tokens encode, 256 are refused — by the frame encoder and on the sender path.
    Tier1Frame f;
    f.intent = iid("REQUEST_MOVE");
    f.slots[SLOT_ACTOR].mode = SlotMode::Literal;
    std::vector<Symbol> symbols;
    f.slots[SLOT_ACTOR].literal = t2fx::repeat_byte(255u, 0xFEu);
    ITEST_TRUE(frame_to_symbols(common(), vocab, f, symbols) == FrameFault::None && symbols.size() == 4u + 255u);
    f.slots[SLOT_ACTOR].literal = t2fx::repeat_byte(256u, 0xFEu);
    ITEST_TRUE(frame_to_symbols(common(), vocab, f, symbols) == FrameFault::LiteralTooLong);
    ITEST_TRUE(encode("en", t2fx::repeat_byte(256u, 0xFEu) + " move", empty_context()).e.outcome == Tier1Outcome::LiteralTooLong);

    // Decoder: [intent][ACTOR Literal][LOCATION Absent][length][tokens…]
    const auto stream = [move_index](u32 length, u32 tokens) {
        std::vector<Symbol> s = {move_index, static_cast<Symbol>(SlotMode::Literal), static_cast<Symbol>(SlotMode::Absent), length};
        for (u32 k = 0u; k < tokens; ++k) s.push_back(0xFEu);
        return s;
    };
    const auto decodes = [&vocab](const std::vector<Symbol>& s) {
        Tier1Frame out;
        return symbols_to_frame(common(), vocab, s.data(), static_cast<u32>(s.size()), out);
    };
    ITEST_TRUE(decodes(stream(255u, 255u)));          // control
    ITEST_TRUE(decodes(stream(1u, 1u)));              // control
    ITEST_TRUE(!decodes(stream(0u, 0u)));             // length 0
    ITEST_TRUE(!decodes(stream(256u, 256u)));         // length 256
    ITEST_TRUE(!decodes(stream(0xFFFFFFFFu, 3u)));    // malformed length
    ITEST_TRUE(!decodes(stream(3u, 2u)));             // truncated literal
    ITEST_TRUE(!decodes(stream(2u, 3u)));             // trailing token
    std::vector<Symbol> outside = stream(1u, 0u);
    outside.push_back(vocab.size());
    ITEST_TRUE(!decodes(outside));                    // token outside the vocabulary

    // The static model gives neither length any probability, so no coded payload can carry one.
    const Tier1Model model(common(), subwords().ngram());
    const Symbol prefix[3] = {move_index, static_cast<Symbol>(SlotMode::Literal), static_cast<Symbol>(SlotMode::Absent)};
    const Model& lengths = model.model_at(3u, prefix);
    ITEST_TRUE(lengths.range_of(0u).high == lengths.range_of(0u).low);
    ITEST_TRUE(lengths.range_of(256u).high == lengths.range_of(256u).low);
    ITEST_TRUE(lengths.range_of(1u).high > lengths.range_of(1u).low && lengths.range_of(255u).high > lengths.range_of(255u).low);

    // Malformed payloads: random coder bytes behind every candidate Tier 1 header
    // (symbol_count 1 … 40, with and without a hash) either decode to a valid
    // frame or are rejected — never a literal outside 1 … 255 tokens.
    t2fx::XorShift32 rng{0x1E7Du};
    u32 ok = 0u;
    u32 rejected = 0u;
    u32 other = 0u;
    u32 with_literal = 0u;
    for (u32 round = 0u; round < 300u; ++round) {
        std::vector<u8> body(64u);
        for (u8& b : body) b = static_cast<u8>(rng.next());
        const u16 wire_hash = static_cast<u16>(rng.next() & 0xFFFu);
        for (u32 count = 1u; count <= 40u; ++count) {
            for (u32 hash = 0u; hash < 2u; ++hash) {
                std::vector<u8> bytes(8u + body.size(), 0u);
                BitWriter w(bytes.data(), static_cast<u32>(bytes.size()));
                Metadata md;
                md.tier         = Tier::Tier1;
                md.symbol_count = static_cast<u16>(count);
                md.seq          = static_cast<u8>(round);
                md.hash_present = hash != 0u;
                md.context_hash = hash != 0u ? wire_hash : u16{0};
                ITEST_TRUE(write_metadata(w, md));
                std::copy(body.begin(), body.end(), bytes.begin() + static_cast<std::ptrdiff_t>(w.byte_length()));
                Tier1Decoded d;
                const Tier1DecodeStatus status = decode(bytes, d);
                if (status == Tier1DecodeStatus::Ok) {
                    ++ok;
                    std::vector<Symbol> again;
                    ITEST_TRUE(frame_to_symbols(common(), vocab, d.frame, again) == FrameFault::None);
                    bool literal = false;
                    for (const FrameSlot& fs : d.frame.slots) {
                        if (fs.mode != SlotMode::Literal) continue;
                        literal = true;
                        std::vector<Symbol> t;
                        vocab.tokenize(reinterpret_cast<const u8*>(fs.literal.data()), fs.literal.size(), t);
                        ITEST_TRUE(!t.empty() && t.size() <= kMaxLiteralTokens);
                    }
                    if (literal) ++with_literal;
                } else if (status == Tier1DecodeStatus::Malformed) {
                    ++rejected;
                } else {
                    ++other;
                }
            }
        }
    }
    ITEST_EQ(other, 0u);
    ITEST_TRUE(ok != 0u && with_literal != 0u);   // the valid-frame path is exercised, literals included
    std::printf("  literal bounds: %u random payloads, %u decoded to valid frames (%u with a literal), %u rejected as malformed\n",
                ok + rejected + other, ok, with_literal, rejected);
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--packs") == 0 && a + 1 < argc) {
            packs = argv[++a];
        } else if (std::strcmp(argv[a], "--fixtures") == 0 && a + 1 < argc) {
            fixtures = argv[++a];
        } else if (std::strcmp(argv[a], "--rulec-fixtures") == 0 && a + 1 < argc) {
            fx().rulec_dir = argv[++a];
        } else if (std::strcmp(argv[a], "--shared-sources") == 0) {
            while (a + 1 < argc) fx().shared_sources.push_back(argv[++a]);
        }
    }
    if (!fx().load(packs, fixtures)) {
        std::printf("fixture not loaded: %s\n", fx().error.c_str());
        return 1;
    }
    return ::itest::run_all("unit.tier1");
}
