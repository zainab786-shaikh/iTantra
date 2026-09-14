// Conformance — C-17, C-18, C-19 (validation-benchmark-contract §5.2; tier §8.1,
// §8.2, §13.1). Implementation plan Phase 9.
//
//   C-17 [H]  every clause where Tier 1 is safe: both encodings are compared and
//             the selected tier is the smaller of the two.
//   C-18 [H]  every clause where a safety gate trips: Tier 2, regardless of size —
//             including every safe clause whose Tier 1 packet is smaller, rerun
//             below its language's STT confidence threshold.
//   C-19 [H]  the comparison uses the complete native packet: metadata + coder
//             bits + flush + padding + AEAD tag, checked against real sealing.
//             NEGATIVE TEST (exit criterion): clauses where comparing coder bits
//             alone ("payload size") picks Tier 1 but the packet rule picks
//             Tier 2 must exist, and on every one the selector must send Tier 2.
//             A selector comparing payload size only fails this test.
//
//   c17_c19_test --packs <dir> --fixtures <dir>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
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

// One selection with the independent encodings and measurements behind it.
struct Measured {
    const selfx::Sample* sample = nullptr;
    bool                 boost  = false;
    TierSelection        s;
    Tier1Encoding        t1;
    selfx::Tier2Run      t2;
    u32                  t1_coder_bits = 0u;
};

std::vector<selfx::Sample>& samples() {
    static std::vector<selfx::Sample> v;
    return v;
}

std::vector<Measured>& measured() {
    static std::vector<Measured> v;
    return v;
}

Tier1Encoding tier1_for(const SelectTables& tables, const SelectRequest& r) {
    Tier1Request q;
    q.clause          = r.clause;
    q.input           = r.input;
    q.input_length    = r.input_length;
    q.stt_confidence  = r.stt_confidence;
    q.manual_critical = r.manual_critical;
    q.seq             = r.seq;
    q.context         = r.context;
    q.adjacency       = r.adjacency;
    q.policy          = r.policy;
    return tier1_encode(tables.tier1, q);
}

u32 bytes_of_bits(u32 bits) {
    return (bits + 7u) / 8u;
}

void measure_all() {
    if (!measured().empty()) return;
    samples() = selfx::all_samples(fx());
    // The same clause said again: its own values are now context, so Tier 1
    // inherits them and carries the 12-bit hash (a repeated or confirmed message).
    const std::size_t said_once = samples().size();
    for (std::size_t i = 0u; i < said_once; ++i) {
        selfx::Sample again = samples()[i];
        UtteranceExtraction one;
        one.clauses.push_back(again.x.clauses.at(again.k));
        Context ctx = again.ctx;
        if (!t2fx::commit_extraction(ctx, one, 1u) || context_hash(ctx) == context_hash(again.ctx)) continue;
        again.ctx = ctx;
        again.id += "+again";
        samples().push_back(std::move(again));
    }
    measured().reserve(2u * samples().size());
    for (const selfx::Sample& sample : samples()) {
        for (bool boost : {true, false}) {
            Measured m;
            m.sample = &sample;
            m.boost  = boost;
            const SelectTables  tables = fx().tables(sample.lang);
            const SelectRequest r      = selfx::request_for(sample, boost, sample.lang);
            m.s  = select_tier(tables, r);
            m.t1 = tier1_for(tables, r);
            m.t2 = selfx::tier2_for(fx(), r, m.s.priority);
            if (m.t1.outcome == Tier1Outcome::Ok) m.t1_coder_bits = selfx::tier1_coder_bits(fx(), m.t1);
            measured().push_back(std::move(m));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// C-17
// ---------------------------------------------------------------------------

ITEST(c17_tier1_safe_selects_the_smaller_complete_packet) {
    measure_all();
    u32 safe = 0u, chose1 = 0u, chose2 = 0u, ties = 0u;
    for (const Measured& m : measured()) {
        if (m.t1.outcome != Tier1Outcome::Ok) continue;
        ++safe;
        ITEST_TRUE(m.s.tier1_verdict == Tier1Verdict::Safe && m.t2.status == Tier2Status::Ok);
        const u32 p1 = static_cast<u32>(m.t1.payload.size()) + kAeadTagBytes;
        const u32 p2 = u32{m.t2.payload.len} + kAeadTagBytes;
        ITEST_EQ(m.s.tier1_packet_bytes, p1);
        ITEST_EQ(m.s.tier2_packet_bytes, p2);
        const SelectOutcome expected = p1 < p2 ? SelectOutcome::Tier1 : SelectOutcome::Tier2;
        ITEST_TRUE(m.s.outcome == expected);
        ITEST_TRUE(m.s.payload.size() == std::min(p1, p2) - kAeadTagBytes);
        if (m.s.outcome == SelectOutcome::Tier1) {
            ITEST_TRUE(m.s.payload == m.t1.payload);
            Tier1Decoded d;
            ITEST_TRUE(tier1_decode(fx().base.lang.common, fx().base.tables, m.s.payload.data(),
                                    static_cast<u32>(m.s.payload.size()), d) == Tier1DecodeStatus::Ok &&
                       d.frame == m.t1.frame && d.metadata.priority == m.s.priority);
            ++chose1;
        } else {
            ITEST_TRUE(m.s.payload == std::vector<u8>(m.t2.payload.bytes, m.t2.payload.bytes + m.t2.payload.len));
            Tier2Decoded d;
            const SelectRequest r = selfx::request_for(*m.sample, m.boost, m.sample->lang);
            ITEST_TRUE(tier2_decode(fx().base.tables, m.s.payload.data(), static_cast<u32>(m.s.payload.size()), r.context,
                                    d) == Tier2Status::Ok &&
                       d.text == std::string(m.sample->text, r.tier2_text.begin, r.tier2_text.end - r.tier2_text.begin));
            ++chose2;
            if (p1 == p2) ++ties;
        }
    }
    std::printf("  C-17: %u safe selections (boosted and unboosted Tier 2): %u Tier 1 smaller, %u Tier 2 not larger (%u ties)\n",
                safe, chose1, chose2, ties);
    ITEST_TRUE(safe >= 40u && chose1 >= 10u && chose2 >= 1u);
}

// ---------------------------------------------------------------------------
// C-18
// ---------------------------------------------------------------------------

ITEST(c18_any_safety_gate_selects_tier2_regardless_of_size) {
    measure_all();
    u32 gated = 0u;
    for (const Measured& m : measured()) {
        if (m.t1.outcome == Tier1Outcome::Ok) continue;
        ++gated;
        ITEST_TRUE(m.s.tier1_verdict == Tier1Verdict::Unsafe && m.s.trigger != SafetyTrigger::None);
        ITEST_TRUE(m.s.outcome == SelectOutcome::Tier2 && m.s.tier1_packet_bytes == 0u);
        ITEST_TRUE(m.s.payload == std::vector<u8>(m.t2.payload.bytes, m.t2.payload.bytes + m.t2.payload.len));
    }

    // Regardless of size: every clause whose safe Tier 1 packet is SMALLER, with
    // its STT confidence pushed below the threshold.
    u32 smaller_but_gated = 0u;
    for (const Measured& m : measured()) {
        if (m.s.outcome != SelectOutcome::Tier1) continue;
        selfx::Sample low = *m.sample;
        low.confidence    = fx().pack(low.lang).stt_confidence_threshold() - 1;
        const TierSelection s = select_tier(fx().tables(low.lang), selfx::request_for(low, m.boost, low.lang));
        ITEST_TRUE(s.outcome == SelectOutcome::Tier2 && s.trigger == SafetyTrigger::LowSttConfidence);
        ITEST_TRUE(s.tier2_packet_bytes > m.s.tier1_packet_bytes);
        ++smaller_but_gated;
    }
    std::printf("  C-18: %u gated selections sent Tier 2; %u clauses whose Tier 1 packet was smaller sent Tier 2 once gated\n",
                gated, smaller_but_gated);
    ITEST_TRUE(gated >= 40u && smaller_but_gated >= 10u);
}

// ---------------------------------------------------------------------------
// C-19
// ---------------------------------------------------------------------------

ITEST(c19_packet_size_is_metadata_coder_flush_padding_and_tag) {
    measure_all();
    u8 psk[kSessionKeyBytes], ni[kHelloNonceBytes], nr[kHelloNonceBytes];
    for (u32 i = 0u; i < kSessionKeyBytes; ++i) psk[i] = static_cast<u8>(i * 7u + 1u);
    for (u32 i = 0u; i < kHelloNonceBytes; ++i) {
        ni[i] = static_cast<u8>(i + 40u);
        nr[i] = static_cast<u8>(200u - i);
    }
    SessionKeys keys;
    derive_session_keys(psk, ni, nr, keys);
    u8 nonce[kAeadNonceBytes];
    ITEST_TRUE(derive_nonce(keys.session_id, Direction::InitiatorToResponder, SeqCounter{17u}, nonce));

    u32 checked = 0u;
    const auto check = [&](const std::vector<u8>& plaintext, u32 metadata_bits, u32 coder, u32 reported) {
        // Decomposition: the plaintext is exactly metadata + coder + flush, padded to a byte.
        ITEST_EQ(static_cast<u32>(plaintext.size()), bytes_of_bits(metadata_bits + coder + kCoderFlushBits));
        ITEST_EQ(reported, static_cast<u32>(plaintext.size()) + kAeadTagBytes);
        // And what sealing really produces.
        std::vector<u8> sealed(plaintext.size() + kAeadTagBytes);
        u32 out = 0u;
        ITEST_TRUE(aead_seal(keys.key, nonce, nullptr, 0u, plaintext.data(), static_cast<u32>(plaintext.size()), sealed.data(),
                             static_cast<u32>(sealed.size()), out) == AeadStatus::Ok);
        ITEST_EQ(out, static_cast<u32>(plaintext.size()) + kAeadOverheadBytes);
        if (!kAeadBypassed) ITEST_EQ(out, reported);
        ++checked;
    };
    for (const Measured& m : measured()) {
        if (m.s.tier1_packet_bytes != 0u) check(m.t1.payload, m.t1.metadata_bits, m.t1_coder_bits, m.s.tier1_packet_bytes);
        if (m.s.tier2_packet_bytes != 0u) {
            check(std::vector<u8>(m.t2.payload.bytes, m.t2.payload.bytes + m.t2.payload.len), m.t2.payload.metadata_bits,
                  m.t2.coder_bits, m.s.tier2_packet_bytes);
        }
    }
    wipe_session_keys(keys);
    std::printf("  C-19: %u packets = metadata + coder bits + %u flush bits + padding + %u-byte tag, matching real sealing%s\n",
                checked, kCoderFlushBits, kAeadTagBytes, kAeadBypassed ? " (AEAD bypassed in this build: tag counted, not produced)" : "");
    ITEST_TRUE(checked >= 150u);
}

ITEST(c19_negative_payload_only_comparison_picks_the_larger_packet) {
    measure_all();
    u32 safe = 0u, forward_strict = 0u, forward_tie = 0u, reverse = 0u, printed = 0u;
    u64 payload1 = 0u, payload2 = 0u, packet1 = 0u, packet2 = 0u;
    for (const Measured& m : measured()) {
        if (m.s.tier1_verdict != Tier1Verdict::Safe || !m.s.tier2_verified) continue;
        ++safe;
        const bool payload_rule = m.t1_coder_bits < m.t2.coder_bits;                                     // WRONG
        const bool packet_rule  = tier1_packet_is_smaller(m.s.tier1_packet_bytes, m.s.tier2_packet_bytes);  // C-19
        ITEST_TRUE((m.s.outcome == SelectOutcome::Tier1) == packet_rule);
        payload1 += bytes_of_bits(m.t1_coder_bits);
        payload2 += bytes_of_bits(m.t2.coder_bits);
        packet1 += m.s.tier1_packet_bytes;
        packet2 += m.s.tier2_packet_bytes;
        if (payload_rule == packet_rule) continue;
        // The cases C-19 exists for, asserted the right way round.
        if (payload_rule) {
            // Payload size says Tier 1; its packet is larger (or equal): Tier 2 is sent.
            ITEST_TRUE(m.s.outcome == SelectOutcome::Tier2);
            (m.s.tier1_packet_bytes > m.s.tier2_packet_bytes ? forward_strict : forward_tie) += 1u;
        } else {
            // Payload size says Tier 2; Tier 1's packet is strictly smaller: Tier 1 is sent.
            ITEST_TRUE(m.s.outcome == SelectOutcome::Tier1);
            ++reverse;
        }
        if (printed++ < 8u) {
            std::printf("    %-16s %-8s coder bits T1 %3u vs T2 %3u, packet T1 %2u B vs T2 %2u B (metadata %u vs %u bits): payload rule %s, sent %s\n",
                        m.sample->id.c_str(), m.boost ? "boosted" : "unboost", m.t1_coder_bits, m.t2.coder_bits,
                        m.s.tier1_packet_bytes, m.s.tier2_packet_bytes, m.t1.metadata_bits, m.t2.payload.metadata_bits,
                        payload_rule ? "Tier1" : "Tier2", select_outcome_name(m.s.outcome));
        }
    }
    const u32 discriminating = forward_strict + forward_tie + reverse;
    std::printf("  C-19 negative: %u of %u safe selections decide differently on payload size: %u payload-Tier1 / packet strictly larger, %u payload-Tier1 / packets equal, %u payload-Tier2 / Tier 1 packet strictly smaller\n",
                discriminating, safe, forward_strict, forward_tie, reverse);
    if (payload1 != 0u && packet1 != 0u) {
        std::printf("  tier 13.1: total coder payload T1 %llu B vs T2 %llu B (ratio %llu.%02llu x); packets %llu B vs %llu B (ratio %llu.%02llu x)\n",
                    static_cast<unsigned long long>(payload1), static_cast<unsigned long long>(payload2),
                    static_cast<unsigned long long>(payload2 / payload1),
                    static_cast<unsigned long long>((payload2 * 100u / payload1) % 100u),
                    static_cast<unsigned long long>(packet1), static_cast<unsigned long long>(packet2),
                    static_cast<unsigned long long>(packet2 / packet1),
                    static_cast<unsigned long long>((packet2 * 100u / packet1) % 100u));
    }
    // Exit criterion: discriminating cases must exist, or this test would pass
    // for a payload-only selector too — and at least one must be strict, so the
    // failure does not rest on the tie rule alone.
    ITEST_TRUE(discriminating >= 1u);
    ITEST_TRUE(forward_strict + reverse >= 1u);
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
    return ::itest::run_all("conformance.c17_c19");
}
