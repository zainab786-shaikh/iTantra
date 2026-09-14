// Conformance — C-32. Implementation plan Phase 5.
//
//   C-32 [H]  Inherit the same slot 50 consecutive times
//             → age increments every time; staleness eventually fires.
//
// Guards context §4.3: "INHERIT and REF must NOT reset age." A regression test
// for a bug in an earlier spec revision: if INHERIT reset age, a repeatedly
// inherited value would sit at age 0 forever, the sender's staleness check
// would never fire, and a location could be inherited indefinitely while the
// team moved away from it. Keep it.
//
// Staleness itself is a sender-only heuristic with no shared threshold
// (context §13.1, §13.2); Tier 1 slot resolution (Phase 8) owns it and it is
// not pinned here. So "eventually fires" is proven for EVERY age threshold a
// heuristic could choose within the 50 messages: age >= T becomes true at
// exactly the T-th inheritance, never earlier.

#include "context/context.h"
#include "itest.h"

using namespace itantra;

namespace {

constexpr u32 kInheritances = 50u;

CommitPayload only(SlotId slot, SlotOp op, u16 value, u8 seq) {
    CommitPayload p;
    for (u32 s = 0u; s < kSlotCount; ++s) p.slots[s] = SlotUpdate{SlotOp::Absent, 0u};
    p.slots[slot] = SlotUpdate{op, value};
    p.seq = seq;
    return p;
}

// The age a sender reads before encoding message `n`, after asserting LOCATION
// once and then inheriting it `n` times.
void run(SlotOp op, u8 ages[kInheritances + 1u], bool& all_ok) {
    Context ctx;
    init_context(ctx);
    all_ok = commit(ctx, only(SLOT_LOCATION, SlotOp::Write, 312u, 1u)) == CommitResult::Ok;
    ages[0] = ctx.slots[SLOT_LOCATION].age;
    for (u32 n = 1u; n <= kInheritances; ++n) {
        all_ok = all_ok && commit(ctx, only(SLOT_LOCATION, op, 0u, static_cast<u8>(1u + n))) == CommitResult::Ok;
        ages[n] = ctx.slots[SLOT_LOCATION].age;
    }
}

}  // namespace

ITEST(C32_inherit_the_same_slot_50_times_age_increments_every_time) {
    Context ctx;
    init_context(ctx);
    ITEST_TRUE(commit(ctx, only(SLOT_LOCATION, SlotOp::Write, 312u, 1u)) == CommitResult::Ok);
    ITEST_EQ(ctx.slots[SLOT_LOCATION].age, 0u);
    const u8 ver = ctx.slots[SLOT_LOCATION].ver;

    for (u32 n = 1u; n <= kInheritances; ++n) {
        const u8 before = ctx.slots[SLOT_LOCATION].age;
        ITEST_TRUE(commit(ctx, only(SLOT_LOCATION, SlotOp::Inherit, 0u, static_cast<u8>(1u + n))) ==
                   CommitResult::Ok);
        const Slot& loc = ctx.slots[SLOT_LOCATION];
        ITEST_EQ(loc.age, before + 1u);    // increments every time
        ITEST_EQ(loc.age, n);
        ITEST_EQ(loc.current, 312u);       // the value is still inheritable
        ITEST_EQ(loc.ver, ver);            // inheriting is not a write
    }
}

ITEST(C32_ref_the_same_slot_50_times_age_increments_every_time) {
    u8 ages[kInheritances + 1u] = {};
    bool ok = false;
    run(SlotOp::Ref, ages, ok);
    ITEST_TRUE(ok);
    for (u32 n = 0u; n <= kInheritances; ++n) ITEST_EQ(ages[n], n);
}

ITEST(C32_staleness_eventually_fires_for_every_age_threshold) {
    const SlotOp ops[2] = {SlotOp::Inherit, SlotOp::Ref};
    for (SlotOp op : ops) {
        u8 ages[kInheritances + 1u] = {};
        bool ok = false;
        run(op, ages, ok);
        ITEST_TRUE(ok);
        for (u32 threshold = 1u; threshold <= kInheritances; ++threshold) {
            u32 fired_at = 0u;
            for (u32 n = 0u; n <= kInheritances; ++n) {
                if (ages[n] >= threshold) {
                    fired_at = n;
                    break;
                }
            }
            ITEST_EQ(fired_at, threshold);   // fires, and not before the T-th inheritance
        }
    }
}

ITEST(C32_only_an_explicit_write_makes_the_slot_fresh_again) {
    Context ctx;
    init_context(ctx);
    ITEST_TRUE(commit(ctx, only(SLOT_LOCATION, SlotOp::Write, 312u, 1u)) == CommitResult::Ok);
    for (u32 n = 1u; n <= kInheritances; ++n) {
        ITEST_TRUE(commit(ctx, only(SLOT_LOCATION, SlotOp::Inherit, 0u, 2u)) == CommitResult::Ok);
    }
    ITEST_EQ(ctx.slots[SLOT_LOCATION].age, kInheritances);
    // Re-asserting the same value — "sent explicitly" (context §13.2) — resets it.
    ITEST_TRUE(commit(ctx, only(SLOT_LOCATION, SlotOp::Write, 312u, 3u)) == CommitResult::Ok);
    ITEST_EQ(ctx.slots[SLOT_LOCATION].age, 0u);
}

ITEST_MAIN("conformance.c32")
