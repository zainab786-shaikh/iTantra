// Context hash over the real Context — context §5.2.
//
// Uses the Phase 1 pinned function (common/hash.h, CRC-16/CCITT-FALSE over the
// 24-byte (current, ver) serialisation). Exactly the fields §5.2 covers are
// copied; recent[], age, seq and context_id never reach the hash.

#include "common/hash.h"
#include "context/context.h"

namespace itantra {

u16 context_hash(const Context& ctx) noexcept {
    ContextHashInput in{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        in.current[s] = ctx.slots[s].current;
        in.ver[s]     = ctx.slots[s].ver;
    }
    return context_hash(in);
}

}  // namespace itantra
