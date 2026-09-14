#include "tier1/head.h"

#include <algorithm>
#include <vector>

namespace itantra {

HeadSelection select_head(const ClauseExtraction& clause) {
    HeadSelection h;
    if (clause.concepts.empty()) return h;

    u8 top = static_cast<u8>(ConceptClass::Modifier);
    for (const ConceptMatch& m : clause.concepts) top = std::min<u8>(top, static_cast<u8>(m.concept_class));

    // Distinct concepts of the top class.
    std::vector<const ConceptMatch*> tops;
    for (const ConceptMatch& m : clause.concepts) {
        if (static_cast<u8>(m.concept_class) != top) continue;
        const bool seen = std::any_of(tops.begin(), tops.end(),
                                      [&m](const ConceptMatch* t) { return t->concept_id == m.concept_id; });
        if (!seen) tops.push_back(&m);
    }

    const bool predicate = top <= static_cast<u8>(ConceptClass::State);
    const ConceptMatch* chosen = nullptr;
    if (tops.size() == 1u) {
        chosen = tops[0];
    } else if (predicate) {
        h.status = HeadStatus::TwoTopClass;
        return h;
    } else {
        // ENTITY / MODIFIER: ties break by slot enum order (no slot sorts last).
        std::sort(tops.begin(), tops.end(), [](const ConceptMatch* a, const ConceptMatch* b) {
            return a->slot != b->slot ? a->slot < b->slot : a->concept_id < b->concept_id;
        });
        if (tops[0]->slot == tops[1]->slot) {
            h.status = HeadStatus::TwoTopClass;
            return h;
        }
        chosen = tops[0];
    }

    // A homograph reading of the head's own surface form.
    for (const ConceptMatch& m : clause.concepts) {
        if (m.concept_id != chosen->concept_id) continue;
        for (const ConceptMatch& n : clause.concepts) {
            if (n.group == m.group && n.concept_id != chosen->concept_id) {
                h.status = HeadStatus::Ambiguous;
                return h;
            }
        }
    }

    h.status        = HeadStatus::Found;
    h.concept_id    = chosen->concept_id;
    h.concept_class = chosen->concept_class;
    h.slot          = chosen->slot;
    return h;
}

}  // namespace itantra
