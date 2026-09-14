#pragma once

// Output interface — receiver §7. What the receiver pipeline hands the output
// layer (Kotlin, Phase 11), per packet.
//
//   text          reconstructed message
//   language      which language that text is in (§7.2):
//                   Tier 1 → the RECEIVER's LangId (the frame carried concept IDs)
//                   Tier 2 → the SENDER's LangId  (the payload was the sender's text)
//                 The output layer selects the voice from this field, never from
//                 the receiver's setting.
//   mode          TIER_1 | TIER_2 | TIER_3 (TIER_3 is a stub; never produced)
//   priority      NORMAL | CRITICAL — two states only (§7.4, packet §11)
//   unresolved    slots that could not be resolved
//   status        ok | context_mismatch | integrity_fail | render_fail
//                 render_fail (Phase 10, receiver spec resolutions): the packet is
//                 authentic, decoded and its context committed, but the receiver's
//                 language pack cannot render it. Not an integrity problem —
//                 integrity_fail stays for authentication / integrity failures.
//
// CONTRACT (§7.1, C-31). A slot listed in `unresolved` MUST NOT be presented as a
// value by anything downstream — not spoken, not displayed, not defaulted. This
// layer carries no value for such a slot at all, and while any slot is
// unresolved `text` is empty (tier1_render refuses, tier spec Phase 8): there is
// nothing to leak. How the operator is told which slots are missing is the
// output layer's decision; it must use `unresolved`, never a guessed value.
//
// CRITICAL (§7.4) obliges the output layer to preempt NORMAL playback, use the
// highest practical volume, and vibrate with a critical visual indication. The
// pipeline never lowers a message's priority and emits outputs in arrival order;
// queueing by priority is the output layer's job.

#include <string>
#include <vector>

#include "common/types.h"
#include "packet/metadata.h"

namespace itantra {

enum class OutputMode : u8 {
    Tier1 = 1,
    Tier2 = 2,
    Tier3 = 3,   // stub
};

enum class OutputStatus : u8 {
    Ok,
    ContextMismatch,   // Tier 1 with unresolved slots, or boosted Tier 2 not decoded
    IntegrityFail,     // authenticated but unusable: negation disagreement, malformed payload
    RenderFail,        // authentic, decoded, committed; the receiver's pack cannot render it
};

struct ReceiverOutput {
    std::string     text;
    LangId          language = 0u;
    OutputMode      mode     = OutputMode::Tier2;
    Priority        priority = Priority::Normal;
    std::vector<u8> unresolved;                    // SlotId values, ascending
    OutputStatus    status   = OutputStatus::IntegrityFail;
};

}  // namespace itantra
