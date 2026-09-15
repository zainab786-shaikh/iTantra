#include "context/context.h"

namespace itantra {

namespace {

u8 aged(u8 age) noexcept {
    return age == kAgeMax ? kAgeMax : static_cast<u8>(age + 1u);
}

CommitResult validate(const CommitPayload& payload) noexcept {
    for (u32 s = 0u; s < kSlotCount; ++s) {
        const SlotUpdate& u = payload.slots[s];
        switch (u.op) {
            case SlotOp::Write:
                if (u.value == 0u) return CommitResult::EmptyWrite;
                if (s == SLOT_LAST_REF && u.value > kLastRefMaxStored) return CommitResult::InvalidLastRef;
                break;
            case SlotOp::Absent:
            case SlotOp::Literal:
                if (u.value != 0u) return CommitResult::ValueOnNonWrite;
                break;
            case SlotOp::Inherit:
            case SlotOp::Ref:
                if (u.value != 0u) return CommitResult::ValueOnNonWrite;
                if (s == SLOT_TIME) return CommitResult::TimeInherited;
                break;
            default:
                return CommitResult::InvalidOp;
        }
    }
    return CommitResult::Ok;
}

}  // namespace

void init_context(Context& ctx) noexcept {
    for (u32 s = 0u; s < kSlotCount; ++s) {
        Slot& slot     = ctx.slots[s];
        slot.current   = 0u;
        slot.recent[0] = 0u;
        slot.recent[1] = 0u;
        slot.ver       = 0u;
        slot.age       = 0u;
    }
    ctx.context_id = 0u;
    ctx.seq        = 0u;
    ctx.hash       = context_hash(ctx);
}

CommitResult commit(Context& ctx, const CommitPayload& payload) noexcept {
    const CommitResult valid = validate(payload);
    if (valid != CommitResult::Ok) return valid;

    for (u32 s = 0u; s < kSlotCount; ++s) {
        Slot&             slot   = ctx.slots[s];
        const SlotUpdate& update = payload.slots[s];

        if (update.op != SlotOp::Write) {
            // INHERIT, REF, LITERAL, absent: not a write (§4.3).
            slot.age = aged(slot.age);
            continue;
        }

        const u16 v = update.value;
        if (slot.current == v) {
            // §4.2 "On writing V where S.current == V: S.age = 0;
            //        recent[] and ver unchanged"
            slot.age = 0u;
            continue;
        }

        // §4.2 "On writing a new value V to slot S where S.current != V and
        //        S.current != 0" — the FIFO push. A first write to an empty
        //        slot pushes nothing: 0 is not a value.
        if (slot.current != 0u) {
            slot.recent[1] = slot.recent[0];
            slot.recent[0] = slot.current;
        }
        slot.current = v;
        slot.ver     = static_cast<u8>(slot.ver + 1u);   // wraps at 256 (§4.4)
        slot.age     = 0u;
    }

    ctx.seq  = payload.seq;
    ctx.hash = context_hash(ctx);   // the pre-message hash for the next message
    return CommitResult::Ok;
}

}  // namespace itantra
