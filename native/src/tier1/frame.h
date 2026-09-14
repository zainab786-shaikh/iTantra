#pragma once

// Tier 1 semantic frame — tier §5.5–5.7, §5.9; receiver §3⑧ ⑨, §4.
//
// SHARED by sender and receiver. Nothing in this file reads the rule table,
// head precedence, concept class, category, read-back thresholds or staleness
// (sender-only, tier §3.4; handoff: "the receiver must not link against rule
// tables, concept classes or category masks"). Of concepts.bin, only the slot
// type is read here.
//
// ---------------------------------------------------------------------------
// FROZEN in Phase 8 — Tier 1 wire contract, Tier 1 table version 1
// (recorded in the tier spec implementation resolutions; pinned by the Tier 1 /
// Tier 2 golden-vector artifact)
// ---------------------------------------------------------------------------
//
// A frame is an intent plus one mode per concept slot (ACTOR … STATE):
//
//   Absent    the message says nothing about the slot
//   Inherit   filled by the receiver from context.current (tier §5.6)
//   Ref       "same as the previous message": also context.current — see below
//   Id        an explicit value: a concept ID, or a number
//   Literal   the speaker's exact bytes, for a concept not in the codebook (§5.7)
//
// NEGATION is not a slot: it is the packet's duplicated negation bit (packet
// §3.2). LAST_REF is never part of a frame.
//
// Symbol order — tier §5.9 "[ intent, slot presence/mode mask, slot values,
// literal subwords ]", in exactly that grouping:
//
//   1  intent           index of the intent among intents.bin IDs, ascending
//   2  modes            one per slot the intent EXPECTS, in SlotId order
//                       (intents.bin is shared data, so both phones know which)
//   3  values           for each Id slot, in SlotId order:
//                         concept index among the concepts whose slot type is
//                         that slot (ascending ID), or ESCAPE (= that count) and
//                         then a number: class index c − 1 for bit length
//                         c = 1 … 16 as one symbol, then the c − 1 bits below the
//                         leading 1, most significant first, one symbol each
//   4  literals         for each Literal slot, in SlotId order: token count
//                       L (1 … 255), then L Tier 2 subword tokens
//
// Value kind (Concept or Number) is never transmitted separately: in slot s a
// value v is a Concept iff concepts.bin has concept v with slot type s, and a
// Number otherwise. The sender refuses a number that collides with such a
// concept ID (tier1/slots.h); the decoder rejects one.
//
// Static models, no context boost (§5.9, T4a). Table version 1, code-defined
// from shared data. The frequencies are PROVISIONAL — to be replaced by trained
// tables under a new table version (tier §13.2); they change bytes, never the
// layout above:
//
//   intent       uniform over the intents in intents.bin
//   mode         Absent 3 · Inherit 4 · Ref 1 · Id 4 · Literal 1, with
//                Absent = 0 for a REQUIRED slot and Inherit = Ref = 0 for TIME,
//                so neither can be encoded or decoded (register #9)
//   concept      uniform over that slot's concepts, plus ESCAPE 1
//   class        c = 1 … 16 → 17 − c
//   bit          1 · 1
//   length       L = 0 → 0, L = 1 … 255 → 1
//   literal      the Tier 2 static n-gram table, UNBOOSTED, with its history
//                restarting at BOS at every literal (§5.7 "the same subword coder
//                Tier 2 uses") — no Tier 2 boost, history or metadata
//   past the end a one-symbol model (the decoder rejects trailing symbols)
//
// Ref (Phase 8): the wire mode exists and decodes, commits and resolves exactly
// like Inherit — receiver §4 "previous message's value", which is
// context.current, since every write sets current. The §5.6 sender decision list
// never selects Ref, so the Phase 8 sender NEVER emits it. Its final meaning is
// not otherwise resolved here (receiver pipeline, Phase 10). The golden vectors
// pin Ref's ENCODING, not its meaning: ANY future change to what Ref means
// requires a Tier 1 table / protocol version bump, so a receiver built on this
// meaning can never render a Ref from a sender that means something else.

#include <string>
#include <vector>

#include "coder/model.h"
#include "coder/static_model.h"
#include "context/context.h"
#include "lang/pack.h"
#include "lang/render.h"
#include "packet/assemble.h"
#include "tier2/ngram.h"
#include "tier2/subword.h"

namespace itantra {

constexpr u32 kTier1TableVersion = 1u;

enum class SlotMode : u8 {
    Absent  = 0,
    Inherit = 1,
    Ref     = 2,
    Id      = 3,
    Literal = 4,
};
constexpr u32 kSlotModeCount = 5u;

struct FrameSlot {
    SlotMode    mode  = SlotMode::Absent;
    u16         value = 0u;       // Id only
    std::string literal;          // Literal only: the speaker's exact bytes
};

struct Tier1Frame {
    u16       intent = 0u;
    FrameSlot slots[kConceptSlotCount];
};

bool operator==(const Tier1Frame& a, const Tier1Frame& b) noexcept;
inline bool operator!=(const Tier1Frame& a, const Tier1Frame& b) noexcept { return !(a == b); }

constexpr u32 kMaxLiteralTokens = 255u;
constexpr u32 kNumberClassCount = 16u;   // numbers 1 … 65535

enum class ValueKind : u8 { Concept, Number };

// The kind of `value` in `slot` (see above).
ValueKind value_kind(const CommonPack& common, u8 slot, u16 value) noexcept;

enum class FrameFault : u8 {
    None,
    UnknownIntent,
    InvalidMode,
    SlotNotExpected,      // a non-Absent mode on a slot the intent does not expect
    RequiredSlotAbsent,
    TimeInherited,        // Inherit or Ref on TIME
    ZeroValue,            // Id of 0 (0 means empty)
    StrayValue,           // a value or literal on a mode that carries none
    EmptyLiteral,
    LiteralTooLong,       // more than kMaxLiteralTokens tokens
};

// The symbols of `frame`, in the order above. `out` is valid only for None.
FrameFault frame_to_symbols(const CommonPack& common, const SubwordVocabulary& vocabulary, const Tier1Frame& frame,
                            std::vector<Symbol>& out);

// The frame that symbols[0, count) encode. False unless they are exactly one
// valid frame — nothing missing, nothing trailing.
bool symbols_to_frame(const CommonPack& common, const SubwordVocabulary& vocabulary, const Symbol* symbols, u32 count,
                      Tier1Frame& out);

// Inherit or Ref anywhere: the message relies on context, so hash_present = 1
// (context §17.1 "Hash is sent on every message that uses inheritance").
bool frame_uses_context(const Tier1Frame& frame) noexcept;

// The ONE commit payload (context §10, tier §11.2): Id → Write(value),
// Literal → Literal (never stored, §5.7), Inherit → Inherit, Ref → Ref,
// Absent → Absent, LAST_REF → Absent.
CommitPayload frame_commit_payload(const Tier1Frame& frame, u8 seq) noexcept;

// Slot values for rendering (receiver §4; the sender's read-back resolves the
// same way, §5.8). Inherit / Ref resolve to context.current only when
// `context_trusted` (the hash matched) and the value is not empty; otherwise the
// slot is unresolved: left Absent, its bit set in `unresolved_slots`, never
// given a value (receiver §6.3, §7.1).
struct ResolvedFrame {
    RenderSlot slots[kConceptSlotCount];
    u8         unresolved_slots = 0u;
};

ResolvedFrame resolve_frame(const CommonPack& common, const Tier1Frame& frame, const Context* context,
                            bool context_trusted);

// The Tier 1 PayloadModel (packet/assemble.h P1–P6): position-dependent static
// tables, no boost. Holds references to `common` and `ngram`, which must
// outlive it. One coder at a time (the literal model's scratch table).
class Tier1Model final : public PayloadModel {
public:
    Tier1Model(const CommonPack& common, const NgramTable& ngram);

    bool valid() const noexcept { return valid_; }

    const Model& model_at(u32 position, const Symbol* preceding) const noexcept override;

private:
    const CommonPack&             common_;
    u32                           vocab_size_ = 0u;
    std::vector<std::vector<u16>> slot_concepts_;   // per slot, ascending IDs
    StaticModel                   intent_model_;
    StaticModel                   mode_models_[kConceptSlotCount][2];   // [slot][required]
    StaticModel                   concept_models_[kConceptSlotCount];
    StaticModel                   class_model_;
    StaticModel                   bit_model_;
    StaticModel                   length_model_;
    StaticModel                   certain_model_;
    Tier2Model                    literal_model_;
    bool                          valid_ = false;
};

}  // namespace itantra
