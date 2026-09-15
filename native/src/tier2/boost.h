#pragma once

// Tier 2 context boost — tier §6.4, §6.5, §12 T5.
//
// "Entities in the Context Manager have their subwords' counts raised before
// coding … counts boosted by a fixed integer amount." The concept → subword
// table is shared by all ten languages, so both phones compute an identical
// boost whoever is speaking (§6.4).
//
// DECIDED in Phase 7:
//
//   What is read   slots ACTOR … STATE, field `current` only, of the
//                  PRE-message context. That is exactly the slot state the
//                  context hash vouches for (context §5.2 hashes (current, ver)
//                  of every slot). recent[], age, seq and context_id are never
//                  read: they are not hashed, so a matching hash could not prove
//                  the two phones agree on them (T5). LAST_REF holds a slot
//                  index, not an entity, and is not read.
//
//   Table key      (slot, value). `current` is a concept ID for ACTOR, OBJECT,
//                  LOCATION, SEVERITY and STATE, but a scanned number for
//                  QUANTITY and TIME (language layer); keying by slot as well
//                  keeps quantity 13 from boosting concept 13. Which (slot,
//                  value) pairs have subwords is table data, not code.
//
//   Amount         one fixed magnitude B per table, carried in boost.bin. A
//                  token is boosted once however many slots reference it:
//                  freq += B for every token in the union (tier2/ngram.h).
//
//   Gate           applied if and only if the payload's hash_present = 1
//                  (§6.5). The encoder builds a boost exactly when it sets
//                  hash_present = 1 and writes that context's wire hash; the
//                  decoder builds one only for hash_present = 1, and only after
//                  its own pre-message hash matched (tier2/decode.h). With
//                  hash_present = 0 the context is not read at all, so the
//                  payload decodes whatever the receiver's context holds — the
//                  recovery path (§6.5, context §18.3).
//
// Mass bound, checked when the table loads: B × (sum over slots of that slot's
// largest entry) <= kBoostMassLimit. A slot has one `current` at a time, so no
// context can boost more (see the totals in tier2/ngram.h).
//
// boost.bin payload (inside the lang/pack.h container, kind Boost), big-endian:
//
//   u32 table_version     = kBoostTableVersion
//   u32 vocab_size        V — must equal the vocabulary's
//   u32 magnitude         B >= 1
//   u32 entry_count
//   entry × entry_count   u8 slot (ACTOR … STATE), u16 value (>= 1),
//                         u16 token_count (>= 1), u32 token × token_count
//                         — entries strictly ascending by (slot, value); tokens
//                         strictly ascending, each < V

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"
#include "context/context.h"

namespace itantra {

constexpr u32 kBoostTableVersion = 1u;
constexpr u32 kBoostMassLimit    = (u32{1} << 23) - (u32{1} << 16);
constexpr u32 kBoostSlotCount    = static_cast<u32>(SLOT_STATE) + 1u;   // ACTOR … STATE

class BoostTable {
public:
    bool load(const u8* payload, std::size_t length, u32 vocab_size, std::string& error);

    bool loaded() const noexcept { return magnitude_ != 0u; }
    u32  magnitude() const noexcept { return magnitude_; }
    u32  entry_count() const noexcept { return static_cast<u32>(entries_.size()); }

    // The subword tokens for (slot, value), ascending; nullptr and 0 if none.
    const u32* tokens(u8 slot, u16 value, u32& count) const noexcept;

private:
    struct Entry {
        u8  slot;
        u16 value;
        u32 first;
        u32 count;
    };

    u32                magnitude_ = 0u;
    std::vector<Entry> entries_;
    std::vector<u32>   tokens_;
};

// The boost for one message. Built once, before the first symbol is coded, and
// never changed while coding (packet/assemble.h P3).
class ContextBoost {
public:
    // Reads context.slots[ACTOR … STATE].current and nothing else.
    void build(const BoostTable& table, const Context& context);

    u32        magnitude() const noexcept { return magnitude_; }
    const u32* tokens() const noexcept { return tokens_.data(); }   // ascending, unique
    u32        size() const noexcept { return static_cast<u32>(tokens_.size()); }

private:
    u32              magnitude_ = 0u;
    std::vector<u32> tokens_;
};

// ---------------------------------------------------------------------------
// Build time (native/tools/packc.cpp)
// ---------------------------------------------------------------------------

struct BoostSource {
    struct Entry {
        u8               slot;
        u16              value;
        std::vector<u32> tokens;   // ascending, unique
    };

    u32                vocab_size = 0u;
    u32                magnitude  = 0u;
    std::vector<Entry> entries;    // ascending by (slot, value)
};

// A complete boost.bin container file.
std::vector<u8> serialize_boost(const BoostSource& source);

}  // namespace itantra
