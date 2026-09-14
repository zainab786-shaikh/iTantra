// Conformance — C-31 (validation-benchmark-contract §5.2; receiver §6.3, §7.1;
// context §19). Implementation plan Phase 10.
//
//   C-31 [H]  any slot appearing in unresolved[] is NEVER rendered as a value —
//             not spoken, not displayed, not defaulted.
//
// Every Tier 1 frame of the fixture corpora, and every variant of it with each
// explicit slot (TIME excepted) taken from context as INHERIT and as REF, is
// sealed and received by a phone in every fixture language holding:
//   the sender's context (hash matches)      → delivered, fully rendered
//   an empty context                          → mismatch
//   the sender's context with that slot changed → mismatch
// Across every output: a non-empty unresolved[] means no text at all and a
// non-ok status, and the unresolved slots are exactly the INHERIT / REF slots.
// Boosted Tier 2 under a mismatch likewise yields no text.
//
//   c31_test --packs <dir> --fixtures <dir>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "tier1/decode.h"
#include "itest.h"
#include "receiver_fixture.h"

using namespace itantra;

namespace {

selfx::Fixture& fx() {
    static selfx::Fixture f;
    return f;
}

}  // namespace

ITEST(c31_unresolved_slots_are_never_rendered_as_values) {
    const SessionKeys keys = rxfx::session_keys();
    std::vector<selfx::Sample> samples = selfx::all_samples(fx());
    u32 frames = 0u, matched = 0u, mismatched = 0u, unresolved_outputs = 0u, tier2_refused = 0u;
    u32 by_mode[3] = {0u, 0u, 0u};   // outputs listing: an Inherit slot, a Ref slot, both

    for (const selfx::Sample& base : samples) {
        const Tier1Encoding e = rxfx::tier1_for(fx(), base, seq_to_wire(1u));
        if (e.outcome != Tier1Outcome::Ok) continue;

        std::vector<std::pair<Tier1Frame, Context>> variants;
        if (frame_uses_context(e.frame)) variants.emplace_back(e.frame, base.ctx);
        for (u32 s = 0u; s < kConceptSlotCount; ++s) {
            if (s == SLOT_TIME || e.frame.slots[s].mode != SlotMode::Id) continue;
            for (SlotMode mode : {SlotMode::Inherit, SlotMode::Ref}) {
                Tier1Frame f = e.frame;
                const u16 value = f.slots[s].value;
                f.slots[s] = FrameSlot{mode, 0u, {}};
                Context c = base.ctx;
                CommitPayload w{};
                w.seq = 1u;
                w.slots[s] = SlotUpdate{SlotOp::Write, value};
                if (commit(c, w) != CommitResult::Ok) continue;
                variants.emplace_back(f, c);
            }
        }

        for (const auto& variant : variants) {
            const Tier1Frame& frame = variant.first;
            const Context&    sender_ctx = variant.second;
            const std::vector<u8> plain =
                rxfx::tier1_plaintext(fx(), frame, seq_to_wire(1u), e.priority, e.negated, &sender_ctx);
            ITEST_TRUE(!plain.empty());
            if (plain.empty()) continue;
            const std::vector<u8> sealed = rxfx::seal(keys, 1u, plain);
            ++frames;

            u8 context_slots = 0u;
            bool has_inherit = false, has_ref = false;
            for (u32 s = 0u; s < kConceptSlotCount; ++s) {
                const SlotMode m = frame.slots[s].mode;
                if (m == SlotMode::Inherit || m == SlotMode::Ref) context_slots = static_cast<u8>(context_slots | (1u << s));
                has_inherit = has_inherit || m == SlotMode::Inherit;
                has_ref     = has_ref || m == SlotMode::Ref;
            }

            std::vector<Context> states;
            states.push_back(sender_ctx);
            Context empty;
            init_context(empty);
            states.push_back(empty);
            for (u32 s = 0u; s < kConceptSlotCount; ++s) {
                if ((context_slots & (1u << s)) == 0u) continue;
                Context changed = sender_ctx;
                CommitPayload w{};
                w.seq = 1u;
                w.slots[s] = SlotUpdate{SlotOp::Write, static_cast<u16>(sender_ctx.slots[s].current + 1u)};
                if (commit(changed, w) == CommitResult::Ok) states.push_back(changed);
                break;
            }

            Tier1Decoded decoded;
            ITEST_TRUE(tier1_decode(fx().base.lang.common, fx().base.tables, plain.data(), static_cast<u32>(plain.size()),
                                    decoded) == Tier1DecodeStatus::Ok);

            for (const std::string& listener : langfx::fixture_languages()) {
                for (const Context& state : states) {
                    rxfx::Receiver rx(fx(), listener, keys);
                    rx.session.context = state;
                    const ReceiveResult r = rx.receive(sealed);
                    const bool hash_ok = wire_context_hash(context_hash(state)) == wire_context_hash(context_hash(sender_ctx));

                    // The property, on every output.
                    ITEST_TRUE(r.emit);
                    if (!r.output.unresolved.empty()) {
                        ++unresolved_outputs;
                        ITEST_TRUE(r.output.text.empty());
                        ITEST_TRUE(r.output.status != OutputStatus::Ok);
                        ITEST_TRUE(!r.context_committed && rxfx::same_context(rx.session.context, state));
                    }

                    u8 listed = 0u;
                    for (u8 s : r.output.unresolved) listed = static_cast<u8>(listed | (1u << s));
                    if (hash_ok) {
                        std::string expected;
                        const bool renders = tier1_render(fx().pack(listener), decoded,
                                                          tier1_resolve(fx().base.lang.common, decoded, &state),
                                                          expected) == Tier1RenderStatus::Ok;
                        ITEST_TRUE(r.output.unresolved.empty() && renders && r.outcome == ReceiveOutcome::Delivered);
                        ITEST_TRUE(r.output.text == expected);
                        ++matched;
                    } else {
                        ITEST_TRUE(r.outcome == ReceiveOutcome::Tier1Unresolved && listed == context_slots);
                        ITEST_TRUE(r.output.status == OutputStatus::ContextMismatch && r.request_sync);
                        ++mismatched;
                        by_mode[has_inherit && has_ref ? 2 : (has_ref ? 1 : 0)] += 1u;
                    }
                }
            }
        }

        // Boosted Tier 2 whose boost context the receiver does not hold: no text.
        Context empty;
        init_context(empty);
        if (context_hash(base.ctx) != context_hash(empty)) {
            const std::vector<u8> t2 = rxfx::tier2_plain(fx(), base, seq_to_wire(1u), true, Priority::Normal);
            rxfx::Receiver rx(fx(), base.lang, keys);
            const ReceiveResult r = rx.receive(rxfx::seal(keys, 1u, t2));
            ITEST_TRUE(r.outcome == ReceiveOutcome::Tier2ContextMismatch && r.output.text.empty() &&
                       r.output.unresolved.empty() && !r.context_committed);
            ++tier2_refused;
        }
    }

    std::printf("  C-31: %u Tier 1 frames x %zu listener languages: %u matched (fully rendered), %u mismatched (INHERIT only %u, REF only %u, both %u)\n",
                frames, langfx::fixture_languages().size(), matched, mismatched, by_mode[0], by_mode[1], by_mode[2]);
    std::printf("  C-31: %u outputs listed unresolved slots, every one with no text and a non-ok status; %u boosted Tier 2 mismatches produced no text\n",
                unresolved_outputs, tier2_refused);
    ITEST_TRUE(frames >= 50u && matched >= 100u && mismatched >= 200u && by_mode[1] >= 20u && tier2_refused >= 3u);
    ITEST_EQ(unresolved_outputs, mismatched);
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
    return ::itest::run_all("conformance.c31");
}
