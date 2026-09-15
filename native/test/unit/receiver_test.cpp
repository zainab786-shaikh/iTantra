// Unit — receiver pipeline (implementation plan Phase 10; receiver-pipeline-spec
// §2–§11).
//
//   gate order: authenticate → parse → gates → decode
//   hash verified BEFORE decoding any inherited slot
//   no-inherit packet decodes regardless of hash state
//   Tier 1 + hash mismatch → decodes; INHERIT / REF → unresolved[]
//   Tier 2 boosted + mismatch → refuses to decode; unboosted resend succeeds
//   commit ONLY on success; never on mismatch or authentication failure
//   FEC stage is a no-op — no algorithm present
//   language id in output: Tier 1 receiver's, Tier 2 sender's; cross-language
//     Tier 2 skips the context update on both phones
//   every §9 failure branch; counter recovery; priority; receiver-only includes
//
//   receiver_test --packs <dir> --fixtures <dir> --receiver-sources <files...>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "packet/parse.h"
#include "tier1/decode.h"
#include "itest.h"
#include "receiver_fixture.h"

using namespace itantra;
using rxfx::Phone;
using rxfx::Receiver;
using S = ReceiveStage;

namespace {

selfx::Fixture& fx() {
    static selfx::Fixture f;
    return f;
}

std::vector<std::string>& receiver_sources() {
    static std::vector<std::string> v;
    return v;
}

const SessionKeys& keys() {
    static const SessionKeys k = rxfx::session_keys();
    return k;
}

LangId id(const char* code) {
    return t2fx::test_language_id(code);
}

// The expected Tier 1 rendering of `plain` for a listener holding `ctx`.
std::string expected_tier1_text(const std::vector<u8>& plain, const std::string& listener, const Context& ctx) {
    Tier1Decoded d;
    if (tier1_decode(fx().base.lang.common, fx().base.tables, plain.data(), static_cast<u32>(plain.size()), d) !=
        Tier1DecodeStatus::Ok) {
        return "<decode failed>";
    }
    std::string text;
    if (tier1_render(fx().pack(listener), d, tier1_resolve(fx().base.lang.common, d, &ctx), text) != Tier1RenderStatus::Ok) {
        return "<render failed>";
    }
    return text;
}

std::vector<u8> plaintext_of(const std::vector<u8>& sealed, SeqCounter counter) {
    NativePayload p;
    if (open_payload(sealed.data(), static_cast<u32>(sealed.size()), keys(), rxfx::kSenderDirection, counter, p) !=
        ParseStatus::Ok) {
        return {};
    }
    return std::vector<u8>(p.bytes, p.bytes + p.len);
}

}  // namespace

// ---------------------------------------------------------------------------
// Order and gates
// ---------------------------------------------------------------------------

ITEST(gate_order_is_authenticate_parse_gates_decode) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::vector<u8> p = tx.tier1("Fire at the north gate");
    ITEST_TRUE(!p.empty());
    const ReceiveResult r = rx.receive(p);
    ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.emit && r.counter == 1u);
    ITEST_TRUE(rxfx::trace_is(r, {S::Fec, S::Decrypt, S::Parse, S::Negation, S::SeqGap, S::Replay, S::Hash, S::Model,
                                  S::Decode, S::Reconstruct, S::Commit, S::Emit}));

    // Corrupt bytes: nothing after authentication runs — not even the parser.
    std::vector<u8> bad = tx.tier1("Fire at the north gate");
    bad.at(1) = static_cast<u8>(bad.at(1) ^ 0x10u);
    const ReceiveResult a = rx.receive(bad);
    ITEST_TRUE(a.outcome == ReceiveOutcome::AuthenticationFailed && !a.emit && !a.metadata_valid);
    ITEST_TRUE(rxfx::trace_is(a, {S::Fec, S::Decrypt}));
    // Every reconstructible counter is tried; at session start one of the 256
    // reconstructs to counter 0, which is never valid (crypto/nonce.h).
    ITEST_EQ(a.candidates_tried, 255u);
}

ITEST(hash_is_verified_before_any_inherited_slot_is_decoded) {
    Phone tx(fx(), "en", keys());
    ITEST_TRUE(fx().build_context("LOCATION=hospital", tx.ctx));
    Receiver synced(fx(), "en", keys());
    synced.session.context = tx.ctx;
    Receiver stale(fx(), "en", keys());
    const Context stale_before = stale.session.context;

    const std::vector<u8> p = tx.tier1("Send an ambulance to the hospital");   // LOCATION inherited
    ITEST_TRUE(!p.empty());
    const ReceiveResult good = synced.receive(p);
    ITEST_TRUE(good.outcome == ReceiveOutcome::Delivered && good.metadata.hash_present && good.context_matched);
    ITEST_TRUE(good.output.unresolved.empty() && !good.output.text.empty() && good.context_committed);

    const ReceiveResult bad = stale.receive(p);
    ITEST_TRUE(rxfx::stage_index(bad, S::Hash) >= 0);
    ITEST_TRUE(rxfx::stage_index(bad, S::Hash) < rxfx::stage_index(bad, S::Decode));
    ITEST_TRUE(rxfx::stage_index(bad, S::Decode) < rxfx::stage_index(bad, S::Reconstruct));
    ITEST_TRUE(bad.outcome == ReceiveOutcome::Tier1Unresolved && !bad.context_matched);
    ITEST_TRUE(bad.output.unresolved == std::vector<u8>{static_cast<u8>(SLOT_LOCATION)});
    ITEST_TRUE(bad.output.text.empty() && bad.output.status == OutputStatus::ContextMismatch);
    ITEST_TRUE(!bad.context_committed && bad.request_sync && stale.session.context_suspect);
    ITEST_TRUE(rxfx::same_context(stale.session.context, stale_before));
}

ITEST(packet_without_inheritance_decodes_whatever_the_hash_state) {
    const char* const states[] = {"-", "LOCATION=hospital", "OBJECT=water;QUANTITY#7"};
    u32 delivered = 0u;
    for (const char* state : states) {
        for (int tier = 1; tier <= 2; ++tier) {
            Phone    tx(fx(), "en", keys());
            Receiver rx(fx(), "en", keys());
            ITEST_TRUE(fx().build_context(state, rx.session.context));
            const std::vector<u8> p = tier == 1 ? tx.tier1("Fire at the north gate") : tx.tier2("okay", "en", false);
            const ReceiveResult r = rx.receive(p);
            ITEST_TRUE(!r.metadata.hash_present && r.outcome == ReceiveOutcome::Delivered);
            ITEST_TRUE(r.output.unresolved.empty() && !r.output.text.empty() && r.context_committed);
            if (r.outcome == ReceiveOutcome::Delivered) ++delivered;
        }
    }
    ITEST_EQ(delivered, 6u);
}

ITEST(tier1_hash_mismatch_decodes_and_only_inherit_and_ref_are_unresolved) {
    // REQUEST_MEDICAL_AT with explicit OBJECT and LOCATION, then with them taken
    // from context as Ref / Inherit (the sender never emits Ref; it still decodes).
    selfx::Sample e02 = selfx::single(fx(), "e02", "en", "Send an ambulance to the hospital");
    const Tier1Encoding base = rxfx::tier1_for(fx(), e02, 1u);
    ITEST_TRUE(base.outcome == Tier1Outcome::Ok && base.frame.slots[SLOT_OBJECT].mode == SlotMode::Id &&
               base.frame.slots[SLOT_LOCATION].mode == SlotMode::Id);
    Context ctx;
    init_context(ctx);
    CommitPayload w{};
    w.seq = 1u;
    w.slots[SLOT_OBJECT]   = SlotUpdate{SlotOp::Write, base.frame.slots[SLOT_OBJECT].value};
    w.slots[SLOT_LOCATION] = SlotUpdate{SlotOp::Write, base.frame.slots[SLOT_LOCATION].value};
    ITEST_TRUE(commit(ctx, w) == CommitResult::Ok);

    struct Variant {
        SlotMode        object, location;
        std::vector<u8> unresolved;
    };
    const Variant variants[] = {
        {SlotMode::Ref, SlotMode::Inherit, {SLOT_OBJECT, SLOT_LOCATION}},
        {SlotMode::Id, SlotMode::Ref, {SLOT_LOCATION}},
        {SlotMode::Inherit, SlotMode::Id, {SLOT_OBJECT}},
    };
    const std::string explicit_text = expected_tier1_text(base.payload, "en", ctx);
    for (const Variant& v : variants) {
        Tier1Frame f = base.frame;
        if (v.object != SlotMode::Id) f.slots[SLOT_OBJECT] = FrameSlot{v.object, 0u, {}};
        if (v.location != SlotMode::Id) f.slots[SLOT_LOCATION] = FrameSlot{v.location, 0u, {}};
        const std::vector<u8> plain = rxfx::tier1_plaintext(fx(), f, 1u, Priority::Critical, false, &ctx);
        ITEST_TRUE(!plain.empty());
        const std::vector<u8> sealed = rxfx::seal(keys(), 1u, plain);

        Receiver synced(fx(), "en", keys());
        synced.session.context = ctx;
        const ReceiveResult good = synced.receive(sealed);
        ITEST_TRUE(good.outcome == ReceiveOutcome::Delivered && good.output.text == explicit_text);

        Receiver stale(fx(), "en", keys());
        const ReceiveResult bad = stale.receive(sealed);
        ITEST_TRUE(bad.outcome == ReceiveOutcome::Tier1Unresolved && bad.output.unresolved == v.unresolved);
        ITEST_TRUE(bad.output.text.empty() && bad.output.priority == Priority::Critical && bad.emit);
        ITEST_TRUE(!bad.context_committed && bad.request_sync && !bad.request_repeat);
    }
}

ITEST(tier2_boosted_mismatch_refuses_to_decode_and_the_unboosted_resend_succeeds) {
    Phone tx(fx(), "en", keys());
    ITEST_TRUE(fx().build_context("LOCATION=hospital;OBJECT=water", tx.ctx));
    Receiver rx(fx(), "en", keys());
    const Context before = rx.session.context;

    const std::string text = "Send water to the hospital now please";
    const ReceiveResult r = rx.receive(tx.tier2(text, "en", true, false, false));
    ITEST_TRUE(r.outcome == ReceiveOutcome::Tier2ContextMismatch && r.metadata.hash_present);
    ITEST_TRUE(rxfx::trace_is(r, {S::Fec, S::Decrypt, S::Parse, S::Negation, S::SeqGap, S::Replay, S::Hash, S::Model, S::Emit}));
    ITEST_TRUE(r.emit && r.output.text.empty() && r.output.status == OutputStatus::ContextMismatch);
    ITEST_TRUE(r.request_sync && r.request_unboosted_resend && !r.context_committed);
    ITEST_TRUE(rxfx::same_context(rx.session.context, before));

    const ReceiveResult resend = rx.receive(tx.tier2(text, "en", false));
    ITEST_TRUE(resend.outcome == ReceiveOutcome::Delivered && !resend.metadata.hash_present);
    ITEST_TRUE(resend.output.text == text && resend.context_update == ReceiverContextUpdate::FromText);
}

ITEST(context_commits_only_on_success) {
    // Success: committed, and both phones hold the same context afterwards.
    {
        Phone    tx(fx(), "en", keys());
        Receiver rx(fx(), "en", keys());
        const ReceiveResult a = rx.receive(tx.tier1("Send blankets to the relief camp"));
        ITEST_TRUE(a.outcome == ReceiveOutcome::Delivered && a.context_committed && a.context_update == ReceiverContextUpdate::FromFrame);
        ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
        const ReceiveResult b = rx.receive(tx.tier2("Fire flood at the bridge", "en", true));
        ITEST_TRUE(b.outcome == ReceiveOutcome::Delivered && b.context_committed && b.context_update == ReceiverContextUpdate::FromText);
        ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
        ITEST_TRUE(rx.session.context.seq == seq_to_wire(tx.last));
        // Seq gap, otherwise decoded: committed (§8.1).
        const ReceiveResult c = rx.receive(tx.tier1("Police move", false, 3u));
        ITEST_TRUE(c.seq_gap && c.outcome == ReceiveOutcome::Delivered && c.context_committed);
        ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
    }
    // Every failure: the context is untouched.
    u32 untouched = 0u;
    const auto unchanged = [&](Receiver& rx, const std::vector<u8>& packet, ReceiveOutcome expected) {
        const Context before = rx.session.context;
        const ReceiveResult r = rx.receive(packet);
        const bool ok = r.outcome == expected && !r.context_committed && rxfx::same_context(rx.session.context, before);
        if (ok) ++untouched;
        return ok;
    };
    {
        Phone    tx(fx(), "en", keys());
        Receiver rx(fx(), "en", keys());
        std::vector<u8> p = tx.tier1("Fire at the north gate");
        p.back() = static_cast<u8>(p.back() ^ 1u);
        ITEST_TRUE(unchanged(rx, p, ReceiveOutcome::AuthenticationFailed));
    }
    {
        Phone    tx(fx(), "en", keys());
        Receiver rx(fx(), "en", keys());
        const std::vector<u8> p = tx.tier1("Fire at the north gate");
        ITEST_TRUE(rx.receive(p).outcome == ReceiveOutcome::Delivered);
        ITEST_TRUE(unchanged(rx, p, ReceiveOutcome::Replayed));
    }
    {
        Phone tx(fx(), "en", keys());
        ITEST_TRUE(fx().build_context("LOCATION=hospital", tx.ctx));
        Receiver rx(fx(), "en", keys());
        ITEST_TRUE(unchanged(rx, tx.tier1("Send an ambulance to the hospital"), ReceiveOutcome::Tier1Unresolved));
        ITEST_TRUE(unchanged(rx, tx.tier2("Send water to the hospital", "en", true), ReceiveOutcome::Tier2ContextMismatch));
    }
    {
        Phone    tx(fx(), "en", keys());
        Receiver rx(fx(), "en", keys());
        selfx::Sample s = tx.sample("Fire at the north gate");
        const u8 seq = tx.next(0u);
        std::vector<u8> plain = rxfx::tier1_for(fx(), s, seq).payload;
        plain.at(2) = static_cast<u8>(plain.at(2) ^ 0x20u);
        ITEST_TRUE(unchanged(rx, rxfx::seal(keys(), tx.counter, plain), ReceiveOutcome::NegationRejected));
    }
    ITEST_EQ(untouched, 5u);
}

// ---------------------------------------------------------------------------
// §9 failure branches, one test each
// ---------------------------------------------------------------------------

ITEST(branch_fec_stage_is_a_no_op_with_no_algorithm) {
    // "FEC uncorrectable" exists only in the final architecture: the stage is the identity.
    const u8 bytes[] = {1u, 2u, 3u, 4u};
    const u8* out = nullptr;
    ITEST_EQ(fec_decode(bytes, 4u, out), 4u);
    ITEST_TRUE(out == bytes);
    ITEST_EQ(fec_decode(nullptr, 0u, out), 0u);
    ITEST_TRUE(out == nullptr);

    // A single flipped bit is not corrected: the packet fails authentication.
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    std::vector<u8> p = tx.tier1("Fire at the north gate");
    p.at(0) = static_cast<u8>(p.at(0) ^ 0x80u);
    const ReceiveResult r = rx.receive(p);
    ITEST_TRUE(r.outcome == ReceiveOutcome::AuthenticationFailed && r.trace[0] == S::Fec);
}

ITEST(branch_authentication_failure_discards_with_no_output) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::vector<u8> good = tx.tier1("Fire at the north gate");
    std::vector<u8> tag = good;
    tag.back() = static_cast<u8>(tag.back() ^ 0x01u);
    std::vector<u8> truncated(good.begin(), good.begin() + 3);
    SessionKeys other = keys();
    other.key[0] = static_cast<u8>(other.key[0] ^ 1u);
    for (const std::vector<u8>* p : {&tag, &truncated}) {
        const ReceiveResult r = rx.receive(*p);
        ITEST_TRUE(r.outcome == ReceiveOutcome::AuthenticationFailed && !r.emit && !r.request_repeat && !r.request_sync);
    }
    ITEST_TRUE(rx.receive({}).outcome == ReceiveOutcome::AuthenticationFailed);
    Receiver wrong_key(fx(), "en", other);
    ITEST_TRUE(wrong_key.receive(good).outcome == ReceiveOutcome::AuthenticationFailed);
    // Nothing was recorded: the genuine packet still arrives, and its gap is 0.
    ITEST_EQ(rx.session.replay.largest_accepted(), 0u);
    const ReceiveResult r = rx.receive(good);
    ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && !r.seq_gap);
    // The next good packet after a lost one reveals the gap (§9 "Repair").
    tx.tier1("Police move");   // lost
    const ReceiveResult after = rx.receive(tx.tier1("Army stop"));
    ITEST_TRUE(after.outcome == ReceiveOutcome::Delivered && after.seq_gap && after.gap == 1u && after.request_sync);
}

ITEST(branch_negation_disagreement_rejects_and_requests_repeat) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    for (const char* text : {"Fire at the north gate", "Do not send water"}) {
        selfx::Sample s = tx.sample(text);
        const u8 seq = tx.next(0u);
        const Tier1Encoding e = rxfx::tier1_for(fx(), s, seq);
        ITEST_TRUE(e.outcome == Tier1Outcome::Ok && e.metadata_bits == 19u);
        for (u8 mask : {u8{0x40u}, u8{0x20u}}) {   // bit 17 or bit 18: either copy
            std::vector<u8> plain = e.payload;
            plain.at(2) = static_cast<u8>(plain.at(2) ^ mask);
            const SeqCounter largest = rx.session.replay.largest_accepted();
            const ReceiveResult r = rx.receive(rxfx::seal(keys(), tx.counter, plain));
            ITEST_TRUE(r.outcome == ReceiveOutcome::NegationRejected && r.request_repeat && r.emit);
            ITEST_TRUE(r.output.status == OutputStatus::IntegrityFail && r.output.text.empty());
            ITEST_TRUE(rxfx::trace_is(r, {S::Fec, S::Decrypt, S::Parse, S::Negation, S::Emit}));
            ITEST_EQ(rx.session.replay.largest_accepted(), largest);   // not recorded
        }
    }
}

ITEST(branch_replay_is_discarded_silently) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::vector<u8> first  = tx.tier1("Fire at the north gate");
    const std::vector<u8> second = tx.tier1("Police move");
    ITEST_TRUE(rx.receive(first).outcome == ReceiveOutcome::Delivered);
    ITEST_TRUE(rx.receive(second).outcome == ReceiveOutcome::Delivered);
    for (const std::vector<u8>* p : {&second, &first}) {
        const ReceiveResult r = rx.receive(*p);
        // The older one also shows a seq gap at ⑤, but a replay requests nothing.
        ITEST_TRUE(r.outcome == ReceiveOutcome::Replayed && !r.emit && !r.request_sync && !r.request_repeat);
        ITEST_TRUE(r.trace[r.trace_length - 1u] == S::Replay && !r.context_committed);
    }
    ITEST_TRUE(!rx.session.context_suspect);
}

ITEST(branch_seq_gap_continues_requests_sync_and_commits) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    ITEST_TRUE(rx.receive(tx.tier1("Fire at the north gate")).outcome == ReceiveOutcome::Delivered);
    const ReceiveResult r = rx.receive(tx.tier2("Fire flood at the bridge", "en", false, false, true, 4u));
    ITEST_TRUE(r.seq_gap && r.gap == 4u && r.request_sync && rx.session.context_suspect);
    ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.context_committed && r.output.text == "Fire flood at the bridge");
    ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
    // A later matching hash confirms the contexts agree again.
    ITEST_TRUE(rx.receive(tx.tier2("Send water to the hospital", "en", true)).outcome == ReceiveOutcome::Delivered);
    ITEST_TRUE(!rx.session.context_suspect);
}

ITEST(branch_decode_error_cannot_occur_and_malformed_payloads_are_integrity_failures) {
    // "Decode error — impossible": authenticated Tier 2 payloads of arbitrary coder bytes decode.
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::vector<u8> base = rxfx::tier2_plain(fx(), tx.sample("okay"), 0u, false, Priority::Normal);
    ITEST_TRUE(base.size() >= 3u);
    t2fx::XorShift32 rng{0x1234567u};
    for (u32 i = 0u; i < 200u; ++i) {
        std::vector<u8> plain = base;
        const u8 seq = tx.next(0u);
        // Rewrite seq, metadata bits 7 … 14: its top bit ends byte 0, the rest fill byte 1 above hash_present.
        plain[0] = static_cast<u8>((plain[0] & 0xFEu) | (u32{seq} >> 7));
        plain[1] = static_cast<u8>(((u32{seq} << 1) & 0xFEu) | (plain[1] & 0x01u));
        for (std::size_t b = 3u; b < plain.size(); ++b) plain[b] = static_cast<u8>(rng.next());
        Metadata m;
        u32 off = 0u;
        ITEST_TRUE(parse_metadata(plain.data(), static_cast<u32>(plain.size()), m, off) == ParseStatus::Ok && m.seq == seq);
        const ReceiveResult r = rx.receive(rxfx::seal(keys(), tx.counter, plain));
        ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered);
    }

    // Authenticated but unusable (a sender fault): integrity_fail, repeat, no commit.
    const std::vector<u8> t1 = rxfx::tier1_for(fx(), tx.sample("Fire at the north gate"), tx.next(0u)).payload;
    std::vector<u8> tier00 = t1;
    tier00[0] = static_cast<u8>(tier00[0] & 0x3Fu);
    const Context before = rx.session.context;
    const ReceiveResult bad_tier = rx.receive(rxfx::seal(keys(), tx.counter, tier00));
    ITEST_TRUE(bad_tier.outcome == ReceiveOutcome::Malformed && bad_tier.request_repeat && bad_tier.emit);
    ITEST_TRUE(bad_tier.output.status == OutputStatus::IntegrityFail && !bad_tier.context_committed);
    // Two bytes still carry seq (bits 7 … 14): accepted under its counter, then too short to parse.
    tx.next(0u);
    std::vector<u8> truncated(t1.begin(), t1.begin() + 2);
    const u8 seq_now = seq_to_wire(tx.counter);
    truncated[0] = static_cast<u8>((truncated[0] & 0xFEu) | (u32{seq_now} >> 7));
    truncated[1] = static_cast<u8>(((u32{seq_now} << 1) & 0xFEu) | (truncated[1] & 0x01u));
    const ReceiveResult short_one = rx.receive(rxfx::seal(keys(), tx.counter, truncated));
    ITEST_TRUE(short_one.outcome == ReceiveOutcome::Malformed && rxfx::trace_is(short_one, {S::Fec, S::Decrypt, S::Parse, S::Emit}));
    ITEST_TRUE(rxfx::same_context(rx.session.context, before));
}

// ---------------------------------------------------------------------------
// Counter recovery, languages, priority, separation
// ---------------------------------------------------------------------------

ITEST(counter_is_recovered_without_seq_in_the_clear) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::vector<u8> p1 = tx.tier1("Fire at the north gate");
    const std::vector<u8> p2 = tx.tier1("Police move");
    const std::vector<u8> p3 = tx.tier1("Army stop");
    // Reordered: 1, 3, 2.
    ITEST_TRUE(rx.receive(p1).counter == 1u);
    const ReceiveResult r3 = rx.receive(p3);
    ITEST_TRUE(r3.outcome == ReceiveOutcome::Delivered && r3.counter == 3u && r3.seq_gap);
    const ReceiveResult r2 = rx.receive(p2);
    // Nearest the expectation (4) first, ahead before behind: 4, 5, 3, 6, 2.
    ITEST_TRUE(r2.outcome == ReceiveOutcome::Delivered && r2.counter == 2u && r2.candidates_tried == 5u);

    // The frozen window reaches expected + 128 (128 lost): recovered. expected + 129
    // aliases by 256 and nothing authenticates (packet §3.6 accepts this).
    const ReceiveResult ahead = rx.receive(tx.tier1("Police move", false, 127u));
    ITEST_TRUE(ahead.outcome == ReceiveOutcome::Delivered && ahead.counter == 131u && ahead.gap == 127u);
    const ReceiveResult edge = rx.receive(tx.tier1("Army stop", false, 128u));
    ITEST_TRUE(edge.outcome == ReceiveOutcome::Delivered && edge.counter == 260u && edge.gap == 128u);
    const ReceiveResult too_far = rx.receive(tx.tier1("Police move", false, 129u));
    ITEST_TRUE(too_far.outcome == ReceiveOutcome::AuthenticationFailed && too_far.candidates_tried == 256u);

    // Across the 8-bit wrap.
    Phone    wrap_tx(fx(), "en", keys());
    Receiver wrap_rx(fx(), "en", keys());
    for (u32 i = 0u; i < 300u; ++i) {
        const ReceiveResult r = wrap_rx.receive(wrap_tx.tier1(i % 2u == 0u ? "Police move" : "Army stop"));
        ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.counter == i + 1u);
    }

    // A payload whose seq is not its counter's low 8 bits is not accepted under any counter.
    Phone    bind_tx(fx(), "en", keys());
    Receiver bind_rx(fx(), "en", keys());
    const std::vector<u8> plain = rxfx::tier1_for(fx(), bind_tx.sample("Police move"), 2u).payload;   // seq 2
    const ReceiveResult bound = bind_rx.receive(rxfx::seal(keys(), 1u, plain));                          // counter 1
    ITEST_TRUE(bound.outcome == ReceiveOutcome::AuthenticationFailed && !bound.emit);
}

namespace {

// A plaintext built bit by bit: tier, symbol_count (5 bits, + 11-bit extension
// when the field is 31), seq, then zeros up to `bytes`.
std::vector<u8> raw_plain(u32 tier, u32 count_field, u32 extension, u32 seq, std::size_t bytes) {
    std::vector<u8> out(bytes, 0u);
    u32 pos = 0u;
    const auto put = [&](u32 value, u32 width) {
        for (u32 i = 0u; i < width; ++i, ++pos) {
            if (((value >> (width - 1u - i)) & 1u) != 0u && pos / 8u < out.size()) {
                out[pos / 8u] = static_cast<u8>(out[pos / 8u] | (0x80u >> (pos % 8u)));
            }
        }
    };
    put(tier, 2u);
    put(count_field, 5u);
    if (count_field == 31u) put(extension, 11u);
    put(seq, 8u);
    return out;
}

}  // namespace

ITEST(counter_recovery_never_accepts_a_candidate_on_the_tag_alone) {
    struct Case {
        const char*     name;
        std::vector<u8> plain;
        bool            accepted;   // under counter 1
    };
    const Case cases[] = {
        {"empty plaintext (bare 4-byte packet)", {}, false},
        {"1 byte: seq incomplete", raw_plain(1u, 1u, 0u, 1u, 1u), false},
        {"2 bytes, seq 1: truncated metadata", raw_plain(1u, 1u, 0u, 1u, 2u), true},
        {"tier 00, seq 1", raw_plain(0u, 1u, 0u, 1u, 4u), true},
        {"tier 11, seq 1", raw_plain(3u, 1u, 0u, 1u, 4u), true},
        {"tier 00, seq 2", raw_plain(0u, 1u, 0u, 2u, 4u), false},
        {"tier 11, seq 255", raw_plain(3u, 1u, 0u, 255u, 4u), false},
        {"escape form, 3 bytes: seq unreachable", raw_plain(1u, 31u, 0u, 1u, 3u), false},
        {"escape form, 4 bytes, seq 1", raw_plain(1u, 31u, 0u, 1u, 4u), true},
        // Bits 7 … 14 read 1, but with the escape seq is bits 18 … 25 = 0x55.
        {"escape form, seq 1 only at the short-form position", raw_plain(1u, 31u, 1u << 3, 0x55u, 4u), false},
    };
    for (const Case& c : cases) {
        Receiver rx(fx(), "en", keys());
        const ReceiveResult r = rx.receive(rxfx::seal(keys(), 1u, c.plain));
        if (c.accepted) {
            // Authentic and bound to counter 1, but not a usable payload. (The
            // escape-form one parses as 30 bits of metadata and is refused at
            // decode instead of parse.)
            ITEST_TRUE(r.outcome == ReceiveOutcome::Malformed && r.counter == 1u);
            ITEST_TRUE(r.output.status == OutputStatus::IntegrityFail && r.request_repeat && !r.context_committed);
            ITEST_TRUE(r.trace_length >= 4u && r.trace[1] == S::Decrypt && r.trace[2] == S::Parse);
        } else {
            ITEST_TRUE(r.outcome == ReceiveOutcome::AuthenticationFailed && !r.emit && r.candidates_tried == 255u);
            ITEST_EQ(rx.session.replay.largest_accepted(), 0u);
        }
        const bool ok = c.accepted ? r.outcome == ReceiveOutcome::Malformed : r.outcome == ReceiveOutcome::AuthenticationFailed;
        if (!ok) std::printf("  %s: %s\n", c.name, receive_outcome_name(r.outcome));
    }

    // A real escape-form payload (more than 30 symbols) is recovered normally.
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const std::string text = "Fire flood at the bridge " + std::string(40u, '~');
    const ReceiveResult r = rx.receive(tx.tier2(text, "en", false));
    ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.metadata.symbol_count >= 31u && r.output.text == text);
}

ITEST(context_free_refresh_decodes_under_a_mismatched_receiver_context) {
    u32 refreshes = 0u, tier1 = 0u, tier2 = 0u, boosted_would_fail = 0u;
    for (const t2fx::Utterance& u : fx().base.corpus) {
        const std::size_t clauses = fx().base.lang.extract(u.lang, u.text).clauses.size();
        for (std::size_t k = 0u; k < clauses; ++k) {
            for (bool context_free : {true, false}) {
                Phone tx(fx(), u.lang, keys());
                ITEST_TRUE(fx().build_context("LOCATION=hospital;OBJECT=water", tx.ctx));
                const u16 sender_hash = wire_context_hash(context_hash(tx.ctx));
                Receiver rx(fx(), u.lang, keys());   // its context differs: empty
                ITEST_TRUE(wire_context_hash(context_hash(rx.session.context)) != sender_hash);
                TierSelection sel;
                const std::vector<u8> p = tx.selected(u.text, k, u.lang, sel, context_free);
                ITEST_TRUE(!p.empty());
                const ReceiveResult r = rx.receive(p);
                if (context_free) {
                    // No inheritance and no boost: self-contained, whatever the receiver holds.
                    ITEST_TRUE(!r.metadata.hash_present && r.outcome == ReceiveOutcome::Delivered);
                    ITEST_TRUE(r.output.unresolved.empty() && r.context_committed);
                    ++refreshes;
                    (sel.outcome == SelectOutcome::Tier1 ? tier1 : tier2) += 1u;
                } else if (r.outcome == ReceiveOutcome::Tier2ContextMismatch || r.outcome == ReceiveOutcome::Tier1Unresolved) {
                    ++boosted_would_fail;
                }
            }
        }
    }
    std::printf("  %u context-free refreshes (%u Tier 1, %u unboosted Tier 2) decoded under a mismatched context; %u of the same clauses sent normally did not\n",
                refreshes, tier1, tier2, boosted_would_fail);
    ITEST_TRUE(refreshes >= 40u && tier2 >= 10u && boosted_would_fail >= 10u);
}

ITEST(render_failure_is_render_fail_and_still_commits) {
    // REQUEST_MEDICAL_AT with LOCATION carrying a NUMBER: valid on the wire (a
    // value that is not a LOCATION concept is a number), but every listener's
    // template asks LOCATION for a named form.
    selfx::Sample e02 = selfx::single(fx(), "e02", "en", "Send an ambulance to the hospital");
    const Tier1Encoding base = rxfx::tier1_for(fx(), e02, 1u);
    ITEST_TRUE(base.outcome == Tier1Outcome::Ok);
    Tier1Frame f = base.frame;
    f.slots[SLOT_LOCATION] = FrameSlot{SlotMode::Id, 9999u, {}};
    ITEST_TRUE(value_kind(fx().base.lang.common, SLOT_LOCATION, 9999u) == ValueKind::Number);
    const std::vector<u8> plain = rxfx::tier1_plaintext(fx(), f, 1u, base.priority, false, nullptr);
    ITEST_TRUE(!plain.empty());
    Context expected;
    init_context(expected);
    ITEST_TRUE(commit(expected, frame_commit_payload(f, 1u)) == CommitResult::Ok);
    for (const char* listener : {"en", "hi", "ta"}) {
        Receiver rx(fx(), listener, keys());
        const ReceiveResult r = rx.receive(rxfx::seal(keys(), 1u, plain));
        ITEST_TRUE(r.outcome == ReceiveOutcome::RenderFailed && r.output.status == OutputStatus::RenderFail);
        ITEST_TRUE(r.emit && r.output.text.empty() && r.output.unresolved.empty() && !r.request_repeat);
        ITEST_TRUE(r.context_committed && rxfx::same_context(rx.session.context, expected));
        ITEST_TRUE(r.output.priority == base.priority && r.output.language == id(listener));
    }
}

ITEST(language_id_is_the_receivers_for_tier1_and_the_senders_for_tier2) {
    // Tier 1 from English to Hindi and Tamil: the listener's language and text.
    for (const char* listener : {"hi", "ta", "en"}) {
        Phone    tx(fx(), "en", keys());
        Receiver rx(fx(), listener, keys());
        const Context rx_before = rx.session.context;
        const std::vector<u8> p = tx.tier1("Fire at the north gate");
        const ReceiveResult r = rx.receive(p);
        ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.output.mode == OutputMode::Tier1);
        ITEST_EQ(r.output.language, id(listener));
        ITEST_TRUE(r.output.text == expected_tier1_text(plaintext_of(p, 1u), listener, rx_before));
        ITEST_TRUE(r.context_update == ReceiverContextUpdate::FromFrame);   // Tier 1 commits cross-language
        ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));

        // Tier 2 from English: the sender's language and exact text; context skipped
        // on both phones unless the languages match.
        const std::string text = "Fire flood at the bridge";
        const ReceiveResult t2 = rx.receive(tx.tier2(text, listener, true));
        ITEST_TRUE(t2.outcome == ReceiveOutcome::Delivered && t2.output.mode == OutputMode::Tier2);
        ITEST_EQ(t2.output.language, id("en"));
        ITEST_TRUE(t2.output.text == text);
        const bool same = std::string(listener) == "en";
        ITEST_TRUE(t2.context_update == (same ? ReceiverContextUpdate::FromText : ReceiverContextUpdate::SkippedCrossLanguage));
        ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
    }

    // C-35 / C-08 host precondition: whole corpus conversations through Phase 9
    // selection, in every language pair — both phones agree on the context after
    // every message, and the language rule holds.
    u32 messages = 0u, tier1 = 0u, tier2 = 0u, skipped = 0u;
    for (const std::string& speaker : langfx::fixture_languages()) {
        for (const std::string& listener : langfx::fixture_languages()) {
            Phone    tx(fx(), speaker, keys());
            Receiver rx(fx(), listener, keys());
            for (const t2fx::Utterance& u : fx().base.corpus) {
                if (u.lang != speaker) continue;
                const std::size_t clauses = fx().base.lang.extract(speaker, u.text).clauses.size();
                for (std::size_t k = 0u; k < clauses; ++k) {
                    TierSelection sel;
                    const std::vector<u8> p = tx.selected(u.text, k, listener, sel);
                    ITEST_TRUE(!p.empty());
                    const ReceiveResult r = rx.receive(p);
                    const bool t1 = sel.outcome == SelectOutcome::Tier1;
                    ITEST_TRUE(r.outcome == ReceiveOutcome::Delivered && r.output.priority == sel.priority);
                    ITEST_EQ(r.output.language, t1 ? id(listener.c_str()) : id(speaker.c_str()));
                    ITEST_EQ(context_hash(rx.session.context), context_hash(tx.ctx));
                    if (context_hash(rx.session.context) != context_hash(tx.ctx)) {
                        std::printf("  diverged: %s→%s %s clause %zu\n", speaker.c_str(), listener.c_str(), u.id.c_str(), k);
                    }
                    ++messages;
                    (t1 ? tier1 : tier2) += 1u;
                    if (r.context_update == ReceiverContextUpdate::SkippedCrossLanguage) ++skipped;
                }
            }
        }
    }
    std::printf("  %u messages in 9 language pairs (%u Tier 1, %u Tier 2, %u cross-language Tier 2 skips): contexts equal after every one\n",
                messages, tier1, tier2, skipped);
    ITEST_TRUE(messages >= 100u && skipped >= 10u);
}

ITEST(priority_survives_and_outputs_follow_arrival_order) {
    Phone    tx(fx(), "en", keys());
    Receiver rx(fx(), "en", keys());
    const ReceiveResult normal   = rx.receive(tx.tier1("Police move"));
    const ReceiveResult alert    = rx.receive(tx.tier1("Fire at the north gate"));   // is_alert
    const ReceiveResult manual   = rx.receive(tx.tier2("okay", "en", false, true));  // override
    const ReceiveResult override1 = rx.receive(tx.tier1("Police move", true));
    ITEST_TRUE(normal.output.priority == Priority::Normal && alert.output.priority == Priority::Critical);
    ITEST_TRUE(manual.output.priority == Priority::Critical && override1.output.priority == Priority::Critical);
    ITEST_TRUE(normal.counter == 1u && alert.counter == 2u && manual.counter == 3u && override1.counter == 4u);
}

ITEST(receiver_code_includes_nothing_sender_only) {
    ITEST_EQ(receiver_sources().size(), 5u);
    const char* const forbidden[] = {"tier1/encode.h", "tier1/rules.h", "tier1/head.h", "tier1/slots.h",
                                     "tier1/readback.h", "tier1/adjacency.h", "tier1/rulec/", "select/"};
    for (const std::string& path : receiver_sources()) {
        std::ifstream in(path, std::ios::binary);
        ITEST_TRUE(static_cast<bool>(in));
        std::stringstream text;
        text << in.rdbuf();
        const std::string body = text.str();
        ITEST_TRUE(!body.empty());
        for (const char* f : forbidden) {
            const bool includes = body.find(std::string("#include \"") + f) != std::string::npos;
            ITEST_TRUE(!includes);
            if (includes) std::printf("  %s includes %s\n", path.c_str(), f);
        }
    }
}

int main(int argc, char** argv) {
    std::string packs, fixtures;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--packs") == 0 && a + 1 < argc) packs = argv[++a];
        else if (std::strcmp(argv[a], "--fixtures") == 0 && a + 1 < argc) fixtures = argv[++a];
        else if (std::strcmp(argv[a], "--receiver-sources") == 0) {
            while (a + 1 < argc && std::strncmp(argv[a + 1], "--", 2) != 0) receiver_sources().push_back(argv[++a]);
        }
    }
    if (!fx().load(packs, fixtures)) {
        std::printf("fixture not loaded: %s\n", fx().error.c_str());
        return 1;
    }
    return ::itest::run_all("unit.receiver");
}
