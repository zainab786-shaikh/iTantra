#include "lang/render.h"

#include <vector>

#include "lang/utf8.h"

namespace itantra {

RenderStatus render_frame(const LanguagePack& pack, u16 intent_id, const RenderSlot slots[kConceptSlotCount],
                          std::string& out, u8& missing_slots) {
    out.clear();
    missing_slots = 0u;

    std::string_view text;
    if (!pack.template_text(intent_id, text)) return RenderStatus::NoTemplate;
    std::vector<TemplatePiece> pieces;
    if (!parse_template(text, pieces)) return RenderStatus::NoTemplate;   // unreachable: validated at load

    for (const TemplatePiece& p : pieces) {
        if (p.placeholder && slots[p.slot].kind == RenderKind::Absent) {
            missing_slots = static_cast<u8>(missing_slots | (1u << p.slot));
        }
    }
    if (missing_slots != 0u) return RenderStatus::MissingSlot;

    std::string sentence;
    for (const TemplatePiece& p : pieces) {
        if (!p.placeholder) {
            sentence.append(p.text.data(), p.text.size());
            continue;
        }
        const RenderSlot& slot = slots[p.slot];
        const bool number_form = p.text == "digits" || p.text == "native";
        switch (slot.kind) {
            case RenderKind::Literal:
                sentence += slot.literal;
                break;
            case RenderKind::Concept: {
                if (number_form) return RenderStatus::WrongForm;
                std::string_view form;
                if (!pack.form(slot.concept_id, p.text, form)) return RenderStatus::MissingForm;
                sentence.append(form.data(), form.size());
                break;
            }
            case RenderKind::Number: {
                if (!number_form) return RenderStatus::WrongForm;
                const std::string digits = std::to_string(slot.number);
                if (p.text == "digits") {
                    sentence += digits;
                } else {
                    const std::vector<u32>& native = pack.rules().primary_digits;
                    if (native.size() != 10u) return RenderStatus::WrongForm;
                    for (char d : digits) utf8::append(sentence, native[static_cast<std::size_t>(d - '0')]);
                }
                break;
            }
            case RenderKind::Absent:
            default:
                return RenderStatus::MissingSlot;   // unreachable: checked above
        }
    }
    out = std::move(sentence);
    return RenderStatus::Ok;
}

}  // namespace itantra
