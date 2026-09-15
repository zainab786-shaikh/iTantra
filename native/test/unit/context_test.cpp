// Unit tests — context/. Implementation plan Phase 5.
//
//   recent[] FIFO exactly per §4.2
//   ver increments on write, not on same-value write
//   INHERIT and REF do NOT reset age
//   TIME never inherits
//   NEGATION is absent from the slot table
//   initial state: all zero, hash of all-zero table
//   commit() is ONE function — sender and receiver call the same symbol
//
// Plus: LAST_REF null representation, age saturation, atomic rejection, the
// §5.2 hash exclusions on the real Context (promised by hash_test.cpp), and a
// 100,000-commit comparison against an independent model of §4.2–§4.4.

#include "common/hash.h"
#include "context/context.h"
#include "packet/metadata.h"
#include "itest.h"

#include <cstring>
#include <type_traits>

using namespace itantra;

namespace itantra_test {
using CommitFn = CommitResult (*)(Context&, const CommitPayload&) noexcept;
CommitFn commit_seen_from_another_translation_unit();
}  // namespace itantra_test

namespace {

struct XorShift32 {
    u32 s;
    u32 next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    u32 below(u32 n) { return n == 0u ? 0u : next() % n; }
};

Context fresh() {
    Context c;
    std::memset(&c, 0xFF, sizeof c);   // prove init writes every field
    init_context(c);
    return c;
}

CommitPayload nothing(u8 seq = 0u) {
    CommitPayload p;
    for (u32 s = 0u; s < kSlotCount; ++s) p.slots[s] = SlotUpdate{SlotOp::Absent, 0u};
    p.seq = seq;
    return p;
}

CommitPayload write(SlotId slot, u16 value, u8 seq = 0u) {
    CommitPayload p = nothing(seq);
    p.slots[slot] = SlotUpdate{SlotOp::Write, value};
    return p;
}

CommitPayload with(SlotId slot, SlotOp op, u8 seq = 0u) {
    CommitPayload p = nothing(seq);
    p.slots[slot] = SlotUpdate{op, 0u};
    return p;
}

bool same_slot(const Slot& a, const Slot& b) {
    return a.current == b.current && a.recent[0] == b.recent[0] && a.recent[1] == b.recent[1] &&
           a.ver == b.ver && a.age == b.age;
}

bool same_context(const Context& a, const Context& b) {
    for (u32 s = 0u; s < kSlotCount; ++s) {
        if (!same_slot(a.slots[s], b.slots[s])) return false;
    }
    return a.context_id == b.context_id && a.hash == b.hash && a.seq == b.seq;
}

}  // namespace

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

ITEST(slot_table_is_exactly_the_eight_spec_slots_and_has_no_negation) {
    // context §2.1 / Appendix B, in wire order. NEGATION is not among them
    // (§2.3): the enum stops at LAST_REF = 7 and SLOT_COUNT = 8.
    ITEST_EQ(SLOT_ACTOR, 0u);
    ITEST_EQ(SLOT_OBJECT, 1u);
    ITEST_EQ(SLOT_LOCATION, 2u);
    ITEST_EQ(SLOT_SEVERITY, 3u);
    ITEST_EQ(SLOT_QUANTITY, 4u);
    ITEST_EQ(SLOT_TIME, 5u);
    ITEST_EQ(SLOT_STATE, 6u);
    ITEST_EQ(SLOT_LAST_REF, 7u);
    ITEST_EQ(SLOT_COUNT, 8u);
    ITEST_EQ(sizeof(Context::slots) / sizeof(Slot), 8u);
    ITEST_EQ(sizeof(CommitPayload::slots) / sizeof(SlotUpdate), 8u);

    // Appendix B field types.
    ITEST_TRUE((std::is_same<decltype(Slot::current), u16>::value));
    ITEST_TRUE((std::is_same<decltype(Slot::recent), u16[2]>::value));
    ITEST_TRUE((std::is_same<decltype(Slot::ver), u8>::value));
    ITEST_TRUE((std::is_same<decltype(Slot::age), u8>::value));
    ITEST_TRUE((std::is_same<decltype(Context::context_id), u16>::value));
    ITEST_TRUE((std::is_same<decltype(Context::hash), u16>::value));
    ITEST_TRUE((std::is_same<decltype(Context::seq), u8>::value));
}

ITEST(initial_state_is_all_zero_with_the_hash_of_the_all_zero_table) {
    const Context c = fresh();
    for (u32 s = 0u; s < kSlotCount; ++s) {
        ITEST_EQ(c.slots[s].current, 0u);
        ITEST_EQ(c.slots[s].recent[0], 0u);
        ITEST_EQ(c.slots[s].recent[1], 0u);
        ITEST_EQ(c.slots[s].ver, 0u);
        ITEST_EQ(c.slots[s].age, 0u);
    }
    ITEST_EQ(c.context_id, 0u);
    ITEST_EQ(c.seq, 0u);
    ITEST_EQ(c.hash, context_hash(ContextHashInput{}));
    ITEST_EQ(c.hash, 0xEF8Au);                      // Phase 1 frozen value
    ITEST_EQ(wire_context_hash(c.hash), 0xF8Au);    // Phase 3 frozen mapping

    SlotId target = SLOT_ACTOR;
    ITEST_TRUE(!decode_last_ref(c.slots[SLOT_LAST_REF].current, target));   // no reference active
}

// ---------------------------------------------------------------------------
// §4.2 recent[] FIFO
// ---------------------------------------------------------------------------

ITEST(recent_fifo_follows_section_4_2_exactly) {
    Context c = fresh();
    const Slot& loc = c.slots[SLOT_LOCATION];

    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 312u)) == CommitResult::Ok);   // first write: nothing pushed
    ITEST_EQ(loc.current, 312u);
    ITEST_EQ(loc.recent[0], 0u);
    ITEST_EQ(loc.recent[1], 0u);

    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 400u)) == CommitResult::Ok);
    ITEST_EQ(loc.current, 400u);
    ITEST_EQ(loc.recent[0], 312u);
    ITEST_EQ(loc.recent[1], 0u);

    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 500u)) == CommitResult::Ok);
    ITEST_EQ(loc.current, 500u);
    ITEST_EQ(loc.recent[0], 400u);
    ITEST_EQ(loc.recent[1], 312u);

    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 600u)) == CommitResult::Ok);   // depth 2: 312 discarded
    ITEST_EQ(loc.current, 600u);
    ITEST_EQ(loc.recent[0], 500u);
    ITEST_EQ(loc.recent[1], 400u);

    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 600u)) == CommitResult::Ok);   // same value: recent unchanged
    ITEST_EQ(loc.recent[0], 500u);
    ITEST_EQ(loc.recent[1], 400u);

    // Writing a value that is already in recent[] follows the rule literally.
    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 400u)) == CommitResult::Ok);
    ITEST_EQ(loc.current, 400u);
    ITEST_EQ(loc.recent[0], 600u);
    ITEST_EQ(loc.recent[1], 500u);

    // Other slots untouched throughout.
    ITEST_EQ(c.slots[SLOT_ACTOR].current, 0u);
    ITEST_EQ(c.slots[SLOT_ACTOR].recent[0], 0u);
}

// ---------------------------------------------------------------------------
// §4.4 ver, §4.3 age
// ---------------------------------------------------------------------------

ITEST(ver_increments_on_a_write_and_not_on_a_same_value_write) {
    Context c = fresh();
    const Slot& obj = c.slots[SLOT_OBJECT];
    ITEST_TRUE(commit(c, write(SLOT_OBJECT, 205u)) == CommitResult::Ok);
    ITEST_EQ(obj.ver, 1u);
    ITEST_TRUE(commit(c, write(SLOT_OBJECT, 205u)) == CommitResult::Ok);
    ITEST_EQ(obj.ver, 1u);
    ITEST_TRUE(commit(c, write(SLOT_OBJECT, 206u)) == CommitResult::Ok);
    ITEST_EQ(obj.ver, 2u);
    ITEST_TRUE(commit(c, with(SLOT_OBJECT, SlotOp::Inherit)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, with(SLOT_OBJECT, SlotOp::Ref)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, with(SLOT_OBJECT, SlotOp::Literal)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, nothing()) == CommitResult::Ok);
    ITEST_EQ(obj.ver, 2u);
}

ITEST(ver_wraps_at_256) {
    Context c = fresh();
    for (u32 i = 0u; i < 300u; ++i) {
        ITEST_TRUE(commit(c, write(SLOT_STATE, static_cast<u16>(1u + (i & 1u)))) == CommitResult::Ok);
    }
    ITEST_EQ(c.slots[SLOT_STATE].ver, 300u % 256u);
}

ITEST(age_is_reset_by_writes_only_and_saturates) {
    Context c = fresh();
    const Slot& sev = c.slots[SLOT_SEVERITY];
    ITEST_TRUE(commit(c, write(SLOT_SEVERITY, 3u)) == CommitResult::Ok);
    ITEST_EQ(sev.age, 0u);
    ITEST_TRUE(commit(c, nothing()) == CommitResult::Ok);
    ITEST_EQ(sev.age, 1u);
    ITEST_TRUE(commit(c, with(SLOT_SEVERITY, SlotOp::Literal)) == CommitResult::Ok);   // literals are never stored
    ITEST_EQ(sev.age, 2u);
    ITEST_TRUE(commit(c, write(SLOT_SEVERITY, 3u)) == CommitResult::Ok);               // re-asserted: fresh
    ITEST_EQ(sev.age, 0u);

    for (u32 i = 1u; i <= 400u; ++i) {
        ITEST_TRUE(commit(c, with(SLOT_SEVERITY, SlotOp::Inherit)) == CommitResult::Ok);
        ITEST_EQ(sev.age, i < kAgeMax ? i : kAgeMax);   // never wraps back to fresh
    }
    ITEST_EQ(sev.current, 3u);
}

ITEST(inherit_and_ref_do_not_reset_age) {
    const SlotOp ops[2] = {SlotOp::Inherit, SlotOp::Ref};
    for (SlotOp op : ops) {
        Context c = fresh();
        ITEST_TRUE(commit(c, write(SLOT_ACTOR, 77u)) == CommitResult::Ok);
        for (u32 i = 1u; i <= 10u; ++i) {
            ITEST_TRUE(commit(c, with(SLOT_ACTOR, op)) == CommitResult::Ok);
            ITEST_EQ(c.slots[SLOT_ACTOR].age, i);
        }
        // alternating INHERIT and REF still never resets it
        ITEST_TRUE(commit(c, with(SLOT_ACTOR, op == SlotOp::Inherit ? SlotOp::Ref : SlotOp::Inherit)) ==
                   CommitResult::Ok);
        ITEST_EQ(c.slots[SLOT_ACTOR].age, 11u);
    }
}

ITEST(inherit_and_ref_change_nothing_but_age) {
    Context c = fresh();
    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 312u)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 400u)) == CommitResult::Ok);
    const Context before = c;

    CommitPayload p = nothing(9u);
    p.slots[SLOT_LOCATION] = SlotUpdate{SlotOp::Inherit, 0u};
    p.slots[SLOT_ACTOR]    = SlotUpdate{SlotOp::Ref, 0u};
    ITEST_TRUE(commit(c, p) == CommitResult::Ok);

    for (u32 s = 0u; s < kSlotCount; ++s) {
        ITEST_EQ(c.slots[s].current, before.slots[s].current);
        ITEST_EQ(c.slots[s].recent[0], before.slots[s].recent[0]);
        ITEST_EQ(c.slots[s].recent[1], before.slots[s].recent[1]);
        ITEST_EQ(c.slots[s].ver, before.slots[s].ver);
        ITEST_EQ(c.slots[s].age, before.slots[s].age + 1u);
    }
    ITEST_EQ(c.hash, before.hash);   // nothing the hash covers moved
}

// ---------------------------------------------------------------------------
// TIME, LAST_REF
// ---------------------------------------------------------------------------

ITEST(time_never_inherits) {
    Context c = fresh();
    ITEST_TRUE(commit(c, write(SLOT_TIME, 1430u)) == CommitResult::Ok);   // a resolved absolute time may be written
    const Context before = c;

    ITEST_TRUE(commit(c, with(SLOT_TIME, SlotOp::Inherit)) == CommitResult::TimeInherited);
    ITEST_TRUE(same_context(c, before));
    ITEST_TRUE(commit(c, with(SLOT_TIME, SlotOp::Ref)) == CommitResult::TimeInherited);
    ITEST_TRUE(same_context(c, before));

    ITEST_TRUE(commit(c, with(SLOT_TIME, SlotOp::Literal)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, nothing()) == CommitResult::Ok);
    ITEST_EQ(c.slots[SLOT_TIME].age, 2u);

    // Every other slot may inherit.
    for (u32 s = 0u; s < kSlotCount; ++s) {
        if (s == SLOT_TIME) continue;
        Context d = fresh();
        ITEST_TRUE(commit(d, with(static_cast<SlotId>(s), SlotOp::Inherit)) == CommitResult::Ok);
        ITEST_TRUE(commit(d, with(static_cast<SlotId>(s), SlotOp::Ref)) == CommitResult::Ok);
    }
}

ITEST(last_ref_is_a_pointer_with_zero_as_null) {
    ITEST_EQ(kLastRefNull, 0u);
    for (u32 t = SLOT_ACTOR; t <= SLOT_STATE; ++t) {
        const u16 stored = encode_last_ref(static_cast<SlotId>(t));
        ITEST_EQ(stored, t + 1u);
        SlotId back = SLOT_LAST_REF;
        ITEST_TRUE(decode_last_ref(stored, back));
        ITEST_EQ(back, t);

        Context c = fresh();
        ITEST_TRUE(commit(c, write(SLOT_LAST_REF, stored)) == CommitResult::Ok);
        ITEST_EQ(c.slots[SLOT_LAST_REF].current, t + 1u);
        ITEST_EQ(c.slots[SLOT_LAST_REF].ver, 1u);
    }

    // Pointing at ACTOR is distinguishable from "no reference".
    SlotId target = SLOT_STATE;
    ITEST_TRUE(!decode_last_ref(kLastRefNull, target));
    ITEST_TRUE(decode_last_ref(encode_last_ref(SLOT_ACTOR), target));
    ITEST_EQ(target, SLOT_ACTOR);

    // LAST_REF cannot point at itself, or past the table.
    ITEST_EQ(encode_last_ref(SLOT_LAST_REF), kLastRefNull);
    ITEST_TRUE(!decode_last_ref(8u, target));
    Context c = fresh();
    ITEST_TRUE(commit(c, write(SLOT_LAST_REF, 8u)) == CommitResult::InvalidLastRef);
    ITEST_TRUE(commit(c, write(SLOT_LAST_REF, 0xFFFFu)) == CommitResult::InvalidLastRef);
    ITEST_TRUE(commit(c, write(SLOT_LAST_REF, 0u)) == CommitResult::EmptyWrite);

    // The pointer follows the same §4.2–§4.4 rules as any slot.
    ITEST_TRUE(commit(c, write(SLOT_LAST_REF, encode_last_ref(SLOT_LOCATION))) == CommitResult::Ok);
    ITEST_TRUE(commit(c, write(SLOT_LAST_REF, encode_last_ref(SLOT_OBJECT))) == CommitResult::Ok);
    ITEST_EQ(c.slots[SLOT_LAST_REF].recent[0], encode_last_ref(SLOT_LOCATION));
    ITEST_TRUE(commit(c, with(SLOT_LAST_REF, SlotOp::Inherit)) == CommitResult::Ok);
    ITEST_EQ(c.slots[SLOT_LAST_REF].age, 1u);
}

// ---------------------------------------------------------------------------
// Rejection is atomic
// ---------------------------------------------------------------------------

ITEST(invalid_payloads_are_rejected_and_change_nothing) {
    Context c = fresh();
    ITEST_TRUE(commit(c, write(SLOT_ACTOR, 5u, 1u)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, write(SLOT_QUANTITY, 3u, 2u)) == CommitResult::Ok);
    const Context before = c;

    // Valid writes in slots 0–6, then a fault in LAST_REF: none of it applies.
    CommitPayload p = nothing(3u);
    for (u32 s = 0u; s < SLOT_LAST_REF; ++s) p.slots[s] = SlotUpdate{SlotOp::Write, static_cast<u16>(900u + s)};
    p.slots[SLOT_LAST_REF] = SlotUpdate{SlotOp::Write, 8u};
    ITEST_TRUE(commit(c, p) == CommitResult::InvalidLastRef);
    ITEST_TRUE(same_context(c, before));

    ITEST_TRUE(commit(c, write(SLOT_OBJECT, 0u)) == CommitResult::EmptyWrite);
    ITEST_TRUE(same_context(c, before));

    CommitPayload v = nothing();
    v.slots[SLOT_STATE] = SlotUpdate{SlotOp::Inherit, 12u};
    ITEST_TRUE(commit(c, v) == CommitResult::ValueOnNonWrite);
    v.slots[SLOT_STATE] = SlotUpdate{SlotOp::Absent, 1u};
    ITEST_TRUE(commit(c, v) == CommitResult::ValueOnNonWrite);
    v.slots[SLOT_STATE] = SlotUpdate{SlotOp::Literal, 1u};
    ITEST_TRUE(commit(c, v) == CommitResult::ValueOnNonWrite);
    ITEST_TRUE(same_context(c, before));

    CommitPayload bad = nothing();
    bad.slots[SLOT_ACTOR] = SlotUpdate{static_cast<SlotOp>(9u), 0u};
    ITEST_TRUE(commit(c, bad) == CommitResult::InvalidOp);
    ITEST_TRUE(same_context(c, before));
}

// ---------------------------------------------------------------------------
// §5.2 hash on the real Context
// ---------------------------------------------------------------------------

ITEST(hash_covers_only_the_current_ver_pairs_of_the_real_context) {
    Context c = fresh();
    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 312u)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, write(SLOT_LOCATION, 400u)) == CommitResult::Ok);
    ITEST_TRUE(commit(c, write(SLOT_ACTOR, 17u)) == CommitResult::Ok);
    const u16 h = context_hash(c);

    Context d = c;
    for (u32 s = 0u; s < kSlotCount; ++s) {
        d.slots[s].recent[0] = static_cast<u16>(0x1111u * (s + 1u));
        d.slots[s].recent[1] = static_cast<u16>(0xFFFFu - s);
        d.slots[s].age       = static_cast<u8>(200u + s);
    }
    d.context_id = 0xBEEFu;
    d.seq        = 0x7Fu;
    d.hash       = 0x1234u;
    ITEST_EQ(context_hash(d), h);

    // ...and equals the Phase 1 function over the pairs it copies.
    ContextHashInput pairs{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        pairs.current[s] = c.slots[s].current;
        pairs.ver[s]     = c.slots[s].ver;
    }
    ITEST_EQ(h, context_hash(pairs));

    for (u32 s = 0u; s < kSlotCount; ++s) {
        Context e = c;
        e.slots[s].current = static_cast<u16>(e.slots[s].current ^ 1u);
        ITEST_TRUE(context_hash(e) != h);
        Context f = c;
        f.slots[s].ver = static_cast<u8>(f.slots[s].ver ^ 1u);
        ITEST_TRUE(context_hash(f) != h);
    }
}

ITEST(ctx_hash_is_always_the_pre_message_hash_for_the_next_message) {
    XorShift32 rng{0xC0DEC0DEu};
    Context c = fresh();
    for (u32 i = 0u; i < 2000u; ++i) {
        const u16 before = c.hash;                       // what the next packet's hash_present check uses
        ITEST_EQ(before, context_hash(c));
        CommitPayload p = nothing(static_cast<u8>(i));
        const u32 s = rng.below(kSlotCount);
        if (s != SLOT_LAST_REF) {
            p.slots[s] = SlotUpdate{SlotOp::Write, static_cast<u16>(1u + rng.below(50u))};
        }
        ITEST_TRUE(commit(c, p) == CommitResult::Ok);
        ITEST_EQ(c.hash, context_hash(c));
        ITEST_EQ(c.seq, static_cast<u8>(i));
    }
}

// ---------------------------------------------------------------------------
// One function, both sides
// ---------------------------------------------------------------------------

ITEST(commit_is_one_function_that_sender_and_receiver_both_call) {
    // The same symbol, seen from two translation units.
    const itantra_test::CommitFn here = &commit;
    ITEST_TRUE(itantra_test::commit_seen_from_another_translation_unit() == here);

    // Two phones, initialised independently, fed the same payload sequence
    // through that one function, never diverge.
    XorShift32 rng{0x5EED1234u};
    Context sender;
    Context receiver;
    std::memset(&sender, 0x00, sizeof sender);
    std::memset(&receiver, 0xFF, sizeof receiver);
    init_context(sender);
    init_context(receiver);
    const itantra_test::CommitFn sender_commit   = &commit;
    const itantra_test::CommitFn receiver_commit = itantra_test::commit_seen_from_another_translation_unit();

    for (u32 i = 0u; i < 10000u; ++i) {
        CommitPayload p = nothing(static_cast<u8>(i));
        for (u32 s = 0u; s < kSlotCount; ++s) {
            const u32 r = rng.below(10u);
            if (r < 3u) {
                const u16 v = s == SLOT_LAST_REF ? static_cast<u16>(1u + rng.below(kLastRefMaxStored))
                                                 : static_cast<u16>(1u + rng.below(20u));
                p.slots[s] = SlotUpdate{SlotOp::Write, v};
            } else if (r < 5u && s != SLOT_TIME) {
                p.slots[s] = SlotUpdate{rng.below(2u) == 0u ? SlotOp::Inherit : SlotOp::Ref, 0u};
            } else if (r < 6u) {
                p.slots[s] = SlotUpdate{SlotOp::Literal, 0u};
            }
        }
        ITEST_TRUE(sender_commit(sender, p) == CommitResult::Ok);
        ITEST_TRUE(receiver_commit(receiver, p) == CommitResult::Ok);
        ITEST_TRUE(same_context(sender, receiver));
    }
}

ITEST(commit_matches_an_independent_model_of_sections_4_2_to_4_4) {
    struct Model {
        u32 current, r0, r1, ver, age;
    };
    Model model[SLOT_COUNT] = {};
    Context c = fresh();
    XorShift32 rng{0xA11CE5EDu};

    for (u32 i = 0u; i < 100000u; ++i) {
        CommitPayload p = nothing(static_cast<u8>(i));
        for (u32 s = 0u; s < kSlotCount; ++s) {
            const u32 r = rng.below(8u);
            if (r < 3u) {
                const u32 v = s == SLOT_LAST_REF ? 1u + rng.below(7u) : 1u + rng.below(6u);   // small: repeats happen
                p.slots[s] = SlotUpdate{SlotOp::Write, static_cast<u16>(v)};
            } else if (r == 3u && s != SLOT_TIME) {
                p.slots[s] = SlotUpdate{SlotOp::Inherit, 0u};
            } else if (r == 4u && s != SLOT_TIME) {
                p.slots[s] = SlotUpdate{SlotOp::Ref, 0u};
            }
        }
        ITEST_TRUE(commit(c, p) == CommitResult::Ok);

        for (u32 s = 0u; s < kSlotCount; ++s) {
            Model& m = model[s];
            if (p.slots[s].op == SlotOp::Write) {
                const u32 v = p.slots[s].value;
                if (m.current == v) {
                    m.age = 0u;
                } else {
                    if (m.current != 0u) {
                        m.r1 = m.r0;
                        m.r0 = m.current;
                    }
                    m.current = v;
                    m.ver     = (m.ver + 1u) % 256u;
                    m.age     = 0u;
                }
            } else if (m.age < 255u) {
                ++m.age;
            }
            const Slot& got = c.slots[s];
            const bool same = got.current == m.current && got.recent[0] == m.r0 && got.recent[1] == m.r1 &&
                              got.ver == m.ver && got.age == m.age;
            ITEST_TRUE(same);
        }
    }
}

ITEST_MAIN("unit.context")
