#pragma once

// Output path — language-layer-spec §9, §14.2.
//
//   frame → template for (intent, TARGET language) → fill slots via forms.bin
//         → sentence
//
// The target is whichever pack is passed in. For Tier 1 that is the
// RECEIVER's pack (§10.1): the frame carries only language-neutral IDs, so any
// pack renders it. Tier 2 text is never rendered — it stays in the sender's
// language (§10.1, §10.2); nothing here translates.
//
// Each placeholder {SLOT:form} takes the form of the concept required by that
// slot position ("{LOCATION:locative}") — §14.2. A number uses "digits" (ASCII)
// or "native" (the pack's first digit set). A literal is inserted exactly as
// spoken (§10.5), whatever the form.
//
// Missing information is never invented: a template slot with no value, a
// concept without the requested form, or a form that does not fit the value
// all fail the render, with no partial sentence. How an unresolved slot is
// shown to the operator is the receiver's contract (receiver §7.1, C-31,
// Phase 10), not decided here.

#include <string>

#include "common/types.h"
#include "lang/pack.h"

namespace itantra {

enum class RenderKind : u8 { Absent, Concept, Number, Literal };

struct RenderSlot {
    RenderKind  kind       = RenderKind::Absent;
    u16         concept_id = 0u;
    u32         number     = 0u;
    std::string literal;         // exact bytes
};

enum class RenderStatus : u8 {
    Ok,
    NoTemplate,     // the pack has no template for this intent
    MissingSlot,    // a placeholder's slot is Absent (see missing_slots)
    MissingForm,    // the concept has no such form in this pack
    WrongForm,      // e.g. "digits" for a concept, a named form for a number
};

RenderStatus render_frame(const LanguagePack& pack, u16 intent_id, const RenderSlot slots[kConceptSlotCount],
                          std::string& out, u8& missing_slots);

}  // namespace itantra
