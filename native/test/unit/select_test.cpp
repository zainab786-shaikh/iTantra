// Unit — tier selection (implementation plan Phase 9; tier §8, §11.3; packet §5,
// §7.5, §11).
//
//   all seven tier §8.1 safety triggers route to Tier 2
//   low STT confidence → Tier 1 is never selected
//   the size rule is strict (a tie sends Tier 2) and reads complete packets
//   priority: a safe alert stays CRITICAL on either tier; Tier 2 alone carries
//             only the manual override
//   a clause beyond 2078 Tier 2 tokens is never split: Tier 1 if safe, else
//             ClauseTooLong with nothing to send
//   context update per tier and language pair; no cross-language preference
//
//   select_test --packs <dir> --fixtures <dir>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "packet/parse.h"
#include "select/select.h"
#include "tier1/decode.h"
#include "tier2/decode.h"
#include "itest.h"
#include "select_fixture.h"

using namespace itantra;

namespace {

selfx::Fixture& fx() {
    static selfx::Fixture f;
    return f;
}

std::string tier2_text(const SelectRequest& r) {
    return std::string(reinterpret_cast<const char*>(r.input) + r.tier2_text.begin, r.tier2_text.end - r.tier2_text.begin);
}

// The selected Tier 2 payload is the clause's exact bytes at the selection's priority.
bool tier2_delivers(const TierSelection& s, const SelectRequest& r) {
    Tier2Decoded d;
    return s.outcome == SelectOutcome::Tier2 &&
           tier2_decode(fx().base.tables, s.payload.data(), static_cast<u32>(s.payload.size()), r.context, d) ==
               Tier2Status::Ok &&
           d.text == tier2_text(r) && d.metadata.priority == s.priority;
}

}  // namespace

ITEST(all_seven_safety_triggers_route_to_tier2) {
    struct Case {
        const char*   text;
        i64           confidence;
        SafetyTrigger trigger;
        Tier1Outcome  outcome;
    };
    const i64 low = fx().pack("en").stt_confidence_threshold() - 1;
    const Case cases[] = {
        {"Fire at the north gate", low, SafetyTrigger::LowSttConfidence, Tier1Outcome::LowConfidence},
        {"okay", 900, SafetyTrigger::NoHead, Tier1Outcome::NoHead},
        {"Fire flood at the bridge", 900, SafetyTrigger::TwoTopClass, Tier1Outcome::TwoTopClass},
        {"evacuate", 900, SafetyTrigger::NoRule, Tier1Outcome::NoRule},
        {"fire", 900, SafetyTrigger::RequiredSlotMissing, Tier1Outcome::RequiredSlotMissing},
        {"Never send water", 900, SafetyTrigger::ReadbackLostMeaning, Tier1Outcome::ReadbackUnexplainedWord},
    };
    u32 routed = 0u;
    for (const Case& c : cases) {
        const selfx::Sample sample = selfx::single(fx(), c.text, "en", c.text, "-", c.confidence);
        ITEST_EQ(sample.x.clauses.size(), 1u);
        for (bool boost : {true, false}) {
            const SelectRequest r = selfx::request_for(sample, boost, "en");
            const TierSelection s = select_tier(fx().tables("en"), r);
            ITEST_TRUE(s.tier1.outcome == c.outcome);
            ITEST_TRUE(s.tier1_verdict == Tier1Verdict::Unsafe && s.trigger == c.trigger);
            ITEST_TRUE(tier2_delivers(s, r));
            ITEST_EQ(s.tier1_packet_bytes, 0u);
            ITEST_TRUE(s.context_update == ContextUpdate::FromText);
            if (s.trigger != c.trigger || !tier2_delivers(s, r)) {
                std::printf("  \"%s\": Tier 1 %s, trigger %s, selected %s\n", c.text, tier1_outcome_name(s.tier1.outcome),
                            safety_trigger_name(s.trigger), select_outcome_name(s.outcome));
            }
            if (boost) ++routed;
        }
    }

    // The seventh: negation copies disagree. The sender writes both copies from
    // one bit, so a disagreement can only come from a corrupted encoding: flip
    // one copy of a safe Tier 1 payload, and also report its self-check failing.
    const selfx::Sample sample = selfx::single(fx(), "e01", "en", "Fire at the north gate");
    const SelectRequest r      = selfx::request_for(sample, true, "en");
    const SelectTables  tables = fx().tables("en");
    const TierSelection clean  = select_tier(tables, r);
    ITEST_TRUE(clean.tier1_verdict == Tier1Verdict::Safe && clean.tier1.metadata_bits == 19u);

    Tier1Encoding flipped = clean.tier1;
    flipped.payload.at(2) = static_cast<u8>(flipped.payload.at(2) ^ 0x20u);   // bit 18: the second negation copy
    Metadata m;
    u32 offset = 0u;
    ITEST_TRUE(parse_metadata(flipped.payload.data(), static_cast<u32>(flipped.payload.size()), m, offset) ==
               ParseStatus::NegationMismatch);
    Tier1Encoding self_check = clean.tier1;
    self_check.outcome = Tier1Outcome::SelfCheckFailed;

    for (const Tier1Encoding* bad : {&flipped, &self_check}) {
        SafetyTrigger trigger = SafetyTrigger::None;
        const Tier1Verdict verdict = assess_tier1(tables, r, *bad, trigger);
        ITEST_TRUE(verdict == Tier1Verdict::Unsafe && trigger == SafetyTrigger::NegationCopiesDisagree);
        const selfx::Tier2Run t2 = selfx::tier2_for(fx(), r, selection_priority(r, *bad, verdict));
        const TierSelection s = choose_tier(tables, r, *bad, t2.status, t2.payload);
        ITEST_TRUE(s.trigger == SafetyTrigger::NegationCopiesDisagree && tier2_delivers(s, r));
    }
    ++routed;
    std::printf("  %u of the seven tier 8.1 triggers each sent Tier 2 with the clause's exact bytes\n", routed);
    ITEST_EQ(routed, 7u);
}

ITEST(low_confidence_never_selects_tier1) {
    std::vector<selfx::Sample> samples = selfx::all_samples(fx());
    u32 checked = 0u;
    u32 would_be_tier1 = 0u;
    for (selfx::Sample& sample : samples) {
        const i64 threshold = fx().pack(sample.lang).stt_confidence_threshold();
        for (bool boost : {true, false}) {
            sample.confidence = 900;
            const TierSelection normal = select_tier(fx().tables(sample.lang), selfx::request_for(sample, boost, sample.lang));
            if (normal.outcome == SelectOutcome::Tier1) ++would_be_tier1;
            for (i64 confidence : {threshold - 1, i64{0}, std::numeric_limits<i64>::min()}) {
                sample.confidence = confidence;
                for (bool critical : {false, true}) {
                    sample.critical = critical;
                    const SelectRequest r = selfx::request_for(sample, boost, sample.lang);
                    const TierSelection s = select_tier(fx().tables(sample.lang), r);
                    ITEST_TRUE(s.outcome == SelectOutcome::Tier2 && s.trigger == SafetyTrigger::LowSttConfidence);
                    ITEST_TRUE(s.priority == (critical ? Priority::Critical : Priority::Normal));
                    ITEST_TRUE(tier2_delivers(s, r));
                    ++checked;
                }
            }
            sample.critical = false;
        }
    }
    std::printf("  %u low-confidence selections over %zu clauses: none Tier 1 (%u of them select Tier 1 at 900)\n", checked,
                samples.size(), would_be_tier1);
    ITEST_TRUE(would_be_tier1 >= 10u);
}

ITEST(size_rule_is_strict_and_reads_whole_packets) {
    const selfx::Sample sample = selfx::single(fx(), "e01", "en", "Fire at the north gate");
    const SelectRequest r      = selfx::request_for(sample, true, "en");
    const SelectTables  tables = fx().tables("en");
    const TierSelection base   = select_tier(tables, r);
    ITEST_TRUE(base.tier1_verdict == Tier1Verdict::Safe && base.tier2_verified);
    const selfx::Tier2Run t2 = selfx::tier2_for(fx(), r, base.priority);
    ITEST_TRUE(t2.status == Tier2Status::Ok);

    // Zero bytes after the flush are never read (packet §4.1), so padding either
    // payload changes its packet size and nothing else.
    const std::size_t equal = std::max<std::size_t>(base.tier1.payload.size(), t2.payload.len) + 1u;
    const auto padded1 = [&](std::size_t n) {
        Tier1Encoding e = base.tier1;
        e.payload.resize(n, 0u);
        return e;
    };
    const auto padded2 = [&](std::size_t n) {
        NativePayload p = t2.payload;
        for (std::size_t i = p.len; i < n; ++i) p.bytes[i] = 0u;
        p.len = static_cast<u16>(n);
        return p;
    };
    const TierSelection tie = choose_tier(tables, r, padded1(equal), t2.status, padded2(equal));
    ITEST_TRUE(tie.outcome == SelectOutcome::Tier2 && tie.tier1_packet_bytes == tie.tier2_packet_bytes);
    const TierSelection smaller1 = choose_tier(tables, r, padded1(equal), t2.status, padded2(equal + 1u));
    ITEST_TRUE(smaller1.outcome == SelectOutcome::Tier1 && smaller1.payload == padded1(equal).payload);
    const TierSelection smaller2 = choose_tier(tables, r, padded1(equal + 1u), t2.status, padded2(equal));
    ITEST_TRUE(smaller2.outcome == SelectOutcome::Tier2);
    ITEST_EQ(tie.tier1_packet_bytes, static_cast<u32>(equal) + kAeadTagBytes);

    // A Tier 2 payload at the wrong priority, seq or language is not a candidate.
    Tier2Message wrong;
    wrong.seq           = r.seq;
    wrong.priority      = base.priority == Priority::Critical ? Priority::Normal : Priority::Critical;
    wrong.language      = r.sender_language;
    wrong.boost_context = r.context;
    NativePayload p;
    ITEST_TRUE(tier2_encode(fx().base.tables, r.input + r.tier2_text.begin, r.tier2_text.end - r.tier2_text.begin, wrong, p) ==
               Tier2Status::Ok);
    const TierSelection mismatched = choose_tier(tables, r, base.tier1, Tier2Status::Ok, p);
    ITEST_TRUE(!mismatched.tier2_verified && mismatched.outcome == SelectOutcome::Tier1);
}

ITEST(priority_is_never_lowered_by_the_choice_of_tier) {
    const SelectTables tables = fx().tables("en");
    // "Fire at the north gate" is REPORT_FIRE_AT, is_alert.
    const selfx::Sample alert = selfx::single(fx(), "e01", "en", "Fire at the north gate");
    const SelectRequest r     = selfx::request_for(alert, true, "en");
    const TierSelection safe  = select_tier(tables, r);
    ITEST_TRUE(safe.tier1_verdict == Tier1Verdict::Safe && safe.priority == Priority::Critical);

    // Force Tier 2 to win on size: the verified alert stays CRITICAL on Tier 2.
    Tier1Encoding bigger = safe.tier1;
    bigger.payload.resize(bigger.payload.size() + 64u, 0u);
    const selfx::Tier2Run t2 = selfx::tier2_for(fx(), r, selection_priority(r, bigger, Tier1Verdict::Safe));
    const TierSelection s = choose_tier(tables, r, bigger, t2.status, t2.payload);
    ITEST_TRUE(s.outcome == SelectOutcome::Tier2 && s.priority == Priority::Critical && tier2_delivers(s, r));

    // Not safe: Tier 2 carries only the manual override (packet §11.1).
    selfx::Sample low = selfx::single(fx(), "e07", "en", "Fire at the north gate", "-", 0);
    const TierSelection unsafe = select_tier(tables, selfx::request_for(low, true, "en"));
    ITEST_TRUE(unsafe.outcome == SelectOutcome::Tier2 && unsafe.priority == Priority::Normal);
    low.critical = true;
    const TierSelection manual = select_tier(tables, selfx::request_for(low, true, "en"));
    ITEST_TRUE(manual.outcome == SelectOutcome::Tier2 && manual.priority == Priority::Critical);

    // NORMAL intent + manual override → CRITICAL on whichever tier is chosen.
    selfx::Sample normal = selfx::single(fx(), "e04", "en", "Police move");
    ITEST_TRUE(select_tier(tables, selfx::request_for(normal, true, "en")).priority == Priority::Normal);
    normal.critical = true;
    ITEST_TRUE(select_tier(tables, selfx::request_for(normal, true, "en")).priority == Priority::Critical);
}

ITEST(clause_beyond_2078_tokens_is_never_split) {
    const SelectTables tables = fx().tables("en");
    // Byte 0x01 is in no subword, so each is one Tier 2 token.
    const auto sample_with_tail = [&](const std::string& clause_text, std::size_t tail) {
        selfx::Sample s = selfx::single(fx(), "long", "en", clause_text);
        s.text  = clause_text + t2fx::repeat_byte(tail, 0x01u);
        s.spans = {SourceSpan{0u, static_cast<u32>(s.text.size())}};
        return s;
    };

    // Tier 1 not safe, Tier 2 too long: nothing to send, nothing split.
    const selfx::Sample none = sample_with_tail("okay", kMaxSymbolCount);
    const TierSelection a = select_tier(tables, selfx::request_for(none, false, "en"));
    ITEST_TRUE(a.tier2_status == Tier2Status::TooLong && a.outcome == SelectOutcome::ClauseTooLong);
    ITEST_TRUE(a.payload.empty() && a.context_update == ContextUpdate::None && a.metadata_bits == 0u);

    // One token fewer fits exactly.
    const selfx::Sample fits = sample_with_tail("okay", kMaxSymbolCount - 4u);
    const TierSelection b = select_tier(tables, selfx::request_for(fits, false, "en"));
    ITEST_TRUE(b.outcome == SelectOutcome::Tier2 && b.tier2_packet_bytes > 1000u);
    Metadata m;
    u32 offset = 0u;
    ITEST_TRUE(parse_metadata(b.payload.data(), static_cast<u32>(b.payload.size()), m, offset) == ParseStatus::Ok &&
               m.symbol_count == kMaxSymbolCount);

    // Tier 1 safe, Tier 2 too long: the safe Tier 1 encoding is the only candidate.
    const selfx::Sample safe = sample_with_tail("Fire at the north gate", kMaxSymbolCount);
    const TierSelection c = select_tier(tables, selfx::request_for(safe, false, "en"));
    ITEST_TRUE(c.tier2_status == Tier2Status::TooLong && c.outcome == SelectOutcome::Tier1 && c.tier2_packet_bytes == 0u);
    ITEST_TRUE(c.context_update == ContextUpdate::FromFrame);

    // Tier 2 failing for any other reason with Tier 1 unsafe: NoEncoding.
    NativePayload empty;
    empty.len           = 0u;
    empty.metadata_bits = 0u;
    const SelectRequest r = selfx::request_for(none, false, "en");
    const TierSelection d = choose_tier(tables, r, a.tier1, Tier2Status::CoderFailure, empty);
    ITEST_TRUE(d.outcome == SelectOutcome::NoEncoding && d.payload.empty());
}

ITEST(context_update_follows_tier_and_language_pair_without_a_cross_language_preference) {
    std::vector<selfx::Sample> samples = selfx::all_samples(fx());
    u32 tier1 = 0u;
    u32 tier2 = 0u;
    for (const selfx::Sample& sample : samples) {
        const TierSelection same = select_tier(fx().tables(sample.lang), selfx::request_for(sample, true, sample.lang));
        for (const std::string& listener : langfx::fixture_languages()) {
            if (listener == sample.lang) continue;
            const TierSelection cross = select_tier(fx().tables(sample.lang), selfx::request_for(sample, true, listener));
            // tier §8.4 is deferred: the language pair never changes the choice or the bytes.
            ITEST_TRUE(cross.outcome == same.outcome && cross.payload == same.payload && cross.priority == same.priority);
            ITEST_TRUE(cross.context_update ==
                       (cross.outcome == SelectOutcome::Tier1 ? ContextUpdate::FromFrame : ContextUpdate::Skip));
        }
        ITEST_TRUE(same.context_update ==
                   (same.outcome == SelectOutcome::Tier1 ? ContextUpdate::FromFrame : ContextUpdate::FromText));
        if (same.outcome == SelectOutcome::Tier1) {
            ITEST_TRUE(same.commit.seq == 17u);
            ++tier1;
        } else {
            ++tier2;
        }
    }
    std::printf("  %u Tier 1 / %u Tier 2 selections identical in every listener language\n", tier1, tier2);
}

ITEST(tier1_unavailable_or_invalid_falls_back_and_bad_requests_are_refused) {
    const selfx::Sample sample = selfx::single(fx(), "e01", "en", "Fire at the north gate");
    const SelectRequest r      = selfx::request_for(sample, true, "en");

    SelectTables no_rules = fx().tables("en");
    no_rules.tier1.rules  = nullptr;
    const TierSelection s = select_tier(no_rules, r);
    ITEST_TRUE(s.tier1.outcome == Tier1Outcome::InvalidArgument && s.tier1_verdict == Tier1Verdict::Invalid);
    ITEST_TRUE(tier2_delivers(s, r));

    // A Tier 1 payload whose metadata contradicts its encoding is not sent.
    const TierSelection clean = select_tier(fx().tables("en"), r);
    Tier1Encoding wrong_seq = clean.tier1;
    wrong_seq.payload.at(0) = static_cast<u8>(wrong_seq.payload.at(0) ^ 0x01u);   // low bit of seq's first byte
    SafetyTrigger trigger = SafetyTrigger::None;
    ITEST_TRUE(assess_tier1(fx().tables("en"), r, wrong_seq, trigger) == Tier1Verdict::Invalid);
    Tier1Encoding too_long = clean.tier1;
    too_long.outcome = Tier1Outcome::TooLong;
    ITEST_TRUE(assess_tier1(fx().tables("en"), r, too_long, trigger) == Tier1Verdict::Unavailable);

    const auto refused = [&](SelectRequest bad, const SelectTables& tables) {
        return select_tier(tables, bad).outcome == SelectOutcome::InvalidArgument;
    };
    SelectRequest bad = r;
    bad.sender_language = t2fx::test_language_id("hi");   // not the pack's language
    ITEST_TRUE(refused(bad, fx().tables("en")));
    bad = r;
    bad.listener_language = 0u;
    ITEST_TRUE(refused(bad, fx().tables("en")));
    bad = r;
    bad.context = nullptr;
    ITEST_TRUE(refused(bad, fx().tables("en")));
    bad = r;
    bad.tier2_text.end = static_cast<u32>(r.input_length + 1u);
    ITEST_TRUE(refused(bad, fx().tables("en")));
    SelectTables no_tier2 = fx().tables("en");
    no_tier2.tier2 = nullptr;
    ITEST_TRUE(refused(r, no_tier2));
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--packs") == 0) packs = argv[a + 1];
        if (std::strcmp(argv[a], "--fixtures") == 0) fixtures = argv[a + 1];
    }
    if (!fx().load(packs, fixtures)) {
        std::printf("fixture not loaded: %s\n", fx().error.c_str());
        return 1;
    }
    return ::itest::run_all("unit.select");
}
