#include "tier1/frame.h"

#include <algorithm>

namespace itantra {

namespace {

constexpr u32 kModeFrequency[kSlotModeCount] = {3u, 4u, 1u, 4u, 1u};   // Absent Inherit Ref Id Literal

bool mode_allowed(u32 slot, bool required, u32 mode) noexcept {
    if (mode >= kSlotModeCount) return false;
    if (required && mode == static_cast<u32>(SlotMode::Absent)) return false;
    if (slot == SLOT_TIME &&
        (mode == static_cast<u32>(SlotMode::Inherit) || mode == static_cast<u32>(SlotMode::Ref))) {
        return false;
    }
    return true;
}

std::vector<std::vector<u16>> concepts_by_slot(const CommonPack& common) {
    std::vector<std::vector<u16>> by_slot(kConceptSlotCount);
    for (const ConceptInfo& c : common.concepts()) {   // ascending IDs
        if (c.slot < kConceptSlotCount) by_slot[c.slot].push_back(c.id);
    }
    return by_slot;
}

bool find_intent(const CommonPack& common, u16 id, u32& index) noexcept {
    const std::vector<IntentInfo>& intents = common.intents();
    const auto it = std::lower_bound(intents.begin(), intents.end(), id,
                                     [](const IntentInfo& i, u16 v) { return i.id < v; });
    if (it == intents.end() || it->id != id) return false;
    index = static_cast<u32>(it - intents.begin());
    return true;
}

u32 bit_length(u32 v) noexcept {
    u32 n = 0u;
    while (v != 0u) {
        ++n;
        v >>= 1;
    }
    return n;
}

enum class Need : u8 { Intent, Mode, ConceptValue, NumberClass, NumberBit, LiteralLength, LiteralToken, Done };

// Walks a symbol sequence through the frame layout, one symbol at a time.
// `tokens` (optional) collects each literal's tokens; the model path passes
// null so it never allocates.
class FrameWalker {
public:
    FrameWalker(const CommonPack& common, const std::vector<std::vector<u16>>& slot_concepts, u32 vocab_size,
                std::vector<Symbol>* tokens) noexcept
        : common_(common), slot_concepts_(slot_concepts), vocab_size_(vocab_size), tokens_(tokens) {}

    Need need() const noexcept { return need_; }
    bool failed() const noexcept { return failed_; }
    u32  slot() const noexcept { return slot_; }
    bool slot_required() const noexcept { return ((required_ >> slot_) & 1u) != 0u; }
    u32  literal_start() const noexcept { return literal_start_; }
    u32  literal_done() const noexcept { return literal_done_; }
    const Tier1Frame& frame() const noexcept { return frame_; }

    bool push(Symbol s, u32 position) {
        if (failed_) return false;
        switch (need_) {
            case Need::Intent: {
                const std::vector<IntentInfo>& intents = common_.intents();
                if (s >= intents.size()) return fail();
                frame_.intent = intents[s].id;
                expected_     = intents[s].expected_slots;
                required_     = intents[s].required_slots;
                next_mode_slot(0u);
                return true;
            }
            case Need::Mode:
                if (!mode_allowed(slot_, slot_required(), s)) return fail();
                frame_.slots[slot_].mode = static_cast<SlotMode>(s);
                next_mode_slot(slot_ + 1u);
                return true;
            case Need::ConceptValue: {
                const std::vector<u16>& ids = slot_concepts_[slot_];
                if (s > ids.size()) return fail();
                if (s < ids.size()) {
                    frame_.slots[slot_].value = ids[s];
                    next_value_slot(slot_ + 1u);
                    return true;
                }
                need_ = Need::NumberClass;   // ESCAPE
                return true;
            }
            case Need::NumberClass:
                if (s >= kNumberClassCount) return fail();
                number_    = 1u;   // the leading 1 of a bit length s + 1
                bits_left_ = s;
                if (bits_left_ == 0u) return finish_number();
                need_ = Need::NumberBit;
                return true;
            case Need::NumberBit:
                if (s > 1u) return fail();
                number_ = (number_ << 1) | s;
                if (--bits_left_ == 0u) return finish_number();
                return true;
            case Need::LiteralLength:
                if (s == 0u || s > kMaxLiteralTokens) return fail();
                literal_left_  = s;
                literal_done_  = 0u;
                literal_start_ = position + 1u;
                need_          = Need::LiteralToken;
                return true;
            case Need::LiteralToken:
                if (s >= vocab_size_) return fail();
                if (tokens_ != nullptr) tokens_[slot_].push_back(s);
                ++literal_done_;
                if (--literal_left_ == 0u) next_literal_slot(slot_ + 1u);
                return true;
            case Need::Done:
            default:
                return fail();   // trailing symbol
        }
    }

private:
    bool fail() noexcept {
        failed_ = true;
        return false;
    }

    bool finish_number() noexcept {
        // A number that could be read as a concept of this slot is not a valid
        // frame value: the kind is inferred from (slot, value).
        if (value_kind(common_, static_cast<u8>(slot_), static_cast<u16>(number_)) == ValueKind::Concept) {
            return fail();
        }
        frame_.slots[slot_].value = static_cast<u16>(number_);
        next_value_slot(slot_ + 1u);
        return true;
    }

    void next_mode_slot(u32 from) noexcept {
        for (u32 s = from; s < kConceptSlotCount; ++s) {
            if (((expected_ >> s) & 1u) != 0u) {
                slot_ = s;
                need_ = Need::Mode;
                return;
            }
        }
        next_value_slot(0u);
    }

    void next_value_slot(u32 from) noexcept {
        for (u32 s = from; s < kConceptSlotCount; ++s) {
            if (frame_.slots[s].mode == SlotMode::Id) {
                slot_ = s;
                need_ = Need::ConceptValue;
                return;
            }
        }
        next_literal_slot(0u);
    }

    void next_literal_slot(u32 from) noexcept {
        for (u32 s = from; s < kConceptSlotCount; ++s) {
            if (frame_.slots[s].mode == SlotMode::Literal) {
                slot_ = s;
                need_ = Need::LiteralLength;
                return;
            }
        }
        need_ = Need::Done;
    }

    const CommonPack&                    common_;
    const std::vector<std::vector<u16>>& slot_concepts_;
    u32                                  vocab_size_;
    std::vector<Symbol>*                 tokens_;
    Need                                 need_          = Need::Intent;
    bool                                 failed_        = false;
    u32                                  slot_          = 0u;
    u8                                   expected_      = 0u;
    u8                                   required_      = 0u;
    u32                                  number_        = 0u;
    u32                                  bits_left_     = 0u;
    u32                                  literal_left_  = 0u;
    u32                                  literal_done_  = 0u;
    u32                                  literal_start_ = 0u;
    Tier1Frame                           frame_;
};

}  // namespace

bool operator==(const Tier1Frame& a, const Tier1Frame& b) noexcept {
    if (a.intent != b.intent) return false;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const FrameSlot& x = a.slots[s];
        const FrameSlot& y = b.slots[s];
        if (x.mode != y.mode || x.value != y.value || x.literal != y.literal) return false;
    }
    return true;
}

ValueKind value_kind(const CommonPack& common, u8 slot, u16 value) noexcept {
    const ConceptInfo* c = common.concept_info(value);
    return (c != nullptr && c->slot == slot) ? ValueKind::Concept : ValueKind::Number;
}

FrameFault frame_to_symbols(const CommonPack& common, const SubwordVocabulary& vocabulary, const Tier1Frame& frame,
                            std::vector<Symbol>& out) {
    out.clear();
    u32 intent_index = 0u;
    if (!find_intent(common, frame.intent, intent_index)) return FrameFault::UnknownIntent;
    const IntentInfo& intent = common.intents()[intent_index];

    std::vector<Symbol> literal_tokens[kConceptSlotCount];
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const FrameSlot& fs = frame.slots[s];
        const u32 mode = static_cast<u32>(fs.mode);
        const bool expected = ((intent.expected_slots >> s) & 1u) != 0u;
        const bool required = ((intent.required_slots >> s) & 1u) != 0u;
        if (mode >= kSlotModeCount) return FrameFault::InvalidMode;
        if (!expected && fs.mode != SlotMode::Absent) return FrameFault::SlotNotExpected;
        if (required && fs.mode == SlotMode::Absent) return FrameFault::RequiredSlotAbsent;
        if (s == SLOT_TIME && (fs.mode == SlotMode::Inherit || fs.mode == SlotMode::Ref)) {
            return FrameFault::TimeInherited;
        }
        switch (fs.mode) {
            case SlotMode::Id:
                if (fs.value == 0u) return FrameFault::ZeroValue;
                if (!fs.literal.empty()) return FrameFault::StrayValue;
                break;
            case SlotMode::Literal:
                if (fs.value != 0u) return FrameFault::StrayValue;
                if (fs.literal.empty()) return FrameFault::EmptyLiteral;
                vocabulary.tokenize(reinterpret_cast<const u8*>(fs.literal.data()), fs.literal.size(),
                                    literal_tokens[s]);
                if (literal_tokens[s].size() > kMaxLiteralTokens) return FrameFault::LiteralTooLong;
                break;
            default:
                if (fs.value != 0u || !fs.literal.empty()) return FrameFault::StrayValue;
                break;
        }
    }

    const std::vector<std::vector<u16>> slot_concepts = concepts_by_slot(common);
    out.push_back(intent_index);
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (((intent.expected_slots >> s) & 1u) != 0u) out.push_back(static_cast<Symbol>(frame.slots[s].mode));
    }
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const FrameSlot& fs = frame.slots[s];
        if (fs.mode != SlotMode::Id) continue;
        const std::vector<u16>& ids = slot_concepts[s];
        if (value_kind(common, static_cast<u8>(s), fs.value) == ValueKind::Concept) {
            out.push_back(static_cast<Symbol>(std::lower_bound(ids.begin(), ids.end(), fs.value) - ids.begin()));
            continue;
        }
        out.push_back(static_cast<Symbol>(ids.size()));   // ESCAPE
        const u32 c = bit_length(fs.value);
        out.push_back(c - 1u);
        for (u32 b = c - 1u; b > 0u; --b) out.push_back((u32{fs.value} >> (b - 1u)) & 1u);
    }
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (frame.slots[s].mode != SlotMode::Literal) continue;
        out.push_back(static_cast<Symbol>(literal_tokens[s].size()));
        out.insert(out.end(), literal_tokens[s].begin(), literal_tokens[s].end());
    }
    return FrameFault::None;
}

bool symbols_to_frame(const CommonPack& common, const SubwordVocabulary& vocabulary, const Symbol* symbols, u32 count,
                      Tier1Frame& out) {
    out = Tier1Frame{};
    if (count != 0u && symbols == nullptr) return false;
    const std::vector<std::vector<u16>> slot_concepts = concepts_by_slot(common);
    std::vector<Symbol> tokens[kConceptSlotCount];
    FrameWalker walker(common, slot_concepts, vocabulary.size(), tokens);
    for (u32 i = 0u; i < count; ++i) {
        if (!walker.push(symbols[i], i)) return false;
    }
    if (walker.failed() || walker.need() != Need::Done) return false;

    Tier1Frame frame = walker.frame();
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (frame.slots[s].mode != SlotMode::Literal) continue;
        if (!vocabulary.detokenize(tokens[s].data(), tokens[s].size(), frame.slots[s].literal)) return false;
    }
    out = std::move(frame);
    return true;
}

bool frame_uses_context(const Tier1Frame& frame) noexcept {
    for (const FrameSlot& fs : frame.slots) {
        if (fs.mode == SlotMode::Inherit || fs.mode == SlotMode::Ref) return true;
    }
    return false;
}

CommitPayload frame_commit_payload(const Tier1Frame& frame, u8 seq) noexcept {
    CommitPayload p{};
    p.seq = seq;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const FrameSlot& fs = frame.slots[s];
        switch (fs.mode) {
            case SlotMode::Id:
                p.slots[s] = SlotUpdate{SlotOp::Write, fs.value};
                break;
            case SlotMode::Literal:
                p.slots[s] = SlotUpdate{SlotOp::Literal, 0u};
                break;
            case SlotMode::Inherit:
                p.slots[s] = SlotUpdate{SlotOp::Inherit, 0u};
                break;
            case SlotMode::Ref:
                p.slots[s] = SlotUpdate{SlotOp::Ref, 0u};
                break;
            case SlotMode::Absent:
            default:
                p.slots[s] = SlotUpdate{SlotOp::Absent, 0u};
                break;
        }
    }
    p.slots[SLOT_LAST_REF] = SlotUpdate{SlotOp::Absent, 0u};
    return p;
}

ResolvedFrame resolve_frame(const CommonPack& common, const Tier1Frame& frame, const Context* context,
                            bool context_trusted) {
    ResolvedFrame r;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const FrameSlot& fs  = frame.slots[s];
        RenderSlot&      out = r.slots[s];
        u16 value = 0u;
        switch (fs.mode) {
            case SlotMode::Id:
                value = fs.value;
                break;
            case SlotMode::Literal:
                out.kind    = RenderKind::Literal;
                out.literal = fs.literal;
                continue;
            case SlotMode::Inherit:
            case SlotMode::Ref:
                if (context_trusted && context != nullptr) value = context->slots[s].current;
                if (value == 0u) {
                    r.unresolved_slots = static_cast<u8>(r.unresolved_slots | (1u << s));
                    continue;
                }
                break;
            case SlotMode::Absent:
            default:
                continue;
        }
        if (value_kind(common, static_cast<u8>(s), value) == ValueKind::Concept) {
            out.kind       = RenderKind::Concept;
            out.concept_id = value;
        } else {
            out.kind   = RenderKind::Number;
            out.number = value;
        }
    }
    return r;
}

Tier1Model::Tier1Model(const CommonPack& common, const NgramTable& ngram)
    : common_(common), vocab_size_(ngram.vocab_size()), literal_model_(ngram, nullptr) {
    slot_concepts_ = concepts_by_slot(common);
    bool ok = ngram.loaded() && !common.intents().empty();

    if (ok) {
        const std::vector<u32> uniform(common.intents().size(), 1u);
        ok = intent_model_.assign(uniform.data(), static_cast<u32>(uniform.size()));
    }
    for (u32 s = 0u; s < kConceptSlotCount && ok; ++s) {
        for (u32 req = 0u; req < 2u && ok; ++req) {
            u32 f[kSlotModeCount];
            for (u32 m = 0u; m < kSlotModeCount; ++m) f[m] = mode_allowed(s, req != 0u, m) ? kModeFrequency[m] : 0u;
            ok = mode_models_[s][req].assign(f, kSlotModeCount);
        }
        const std::vector<u32> concepts(slot_concepts_[s].size() + 1u, 1u);   // + ESCAPE
        ok = ok && concept_models_[s].assign(concepts.data(), static_cast<u32>(concepts.size()));
    }
    if (ok) {
        u32 classes[kNumberClassCount];
        for (u32 c = 0u; c < kNumberClassCount; ++c) classes[c] = kNumberClassCount - c;
        const u32 bits[2] = {1u, 1u};
        std::vector<u32> lengths(kMaxLiteralTokens + 1u, 1u);
        lengths[0] = 0u;
        const u32 certain[1] = {1u};
        ok = class_model_.assign(classes, kNumberClassCount) && bit_model_.assign(bits, 2u) &&
             length_model_.assign(lengths.data(), static_cast<u32>(lengths.size())) && certain_model_.assign(certain, 1u);
    }
    valid_ = ok;
}

const Model& Tier1Model::model_at(u32 position, const Symbol* preceding) const noexcept {
    FrameWalker walker(common_, slot_concepts_, vocab_size_, nullptr);
    for (u32 i = 0u; i < position; ++i) {
        if (!walker.push(preceding[i], i)) return certain_model_;
    }
    const bool required = walker.slot_required() ? true : false;
    switch (walker.need()) {
        case Need::Intent:
            return intent_model_;
        case Need::Mode:
            return mode_models_[walker.slot()][required ? 1u : 0u];
        case Need::ConceptValue:
            return concept_models_[walker.slot()];
        case Need::NumberClass:
            return class_model_;
        case Need::NumberBit:
            return bit_model_;
        case Need::LiteralLength:
            return length_model_;
        case Need::LiteralToken:
            // Literal-local history: position k within the literal, over the
            // literal's own tokens only (BOS at its first token).
            return literal_model_.model_at(walker.literal_done(), preceding + walker.literal_start());
        case Need::Done:
        default:
            return certain_model_;
    }
}

}  // namespace itantra
