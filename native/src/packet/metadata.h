#pragma once

// Native payload metadata — packet §3.
//
// FROZEN: packet format version 1. The layout below is part of the golden
// vector contract (native/test/golden/vectors.bin). It changes only with a
// deliberate format version bump recorded in the packet spec change log
// (contract §2.2, §7.2).
//
// packet §3    bit-packed, MSB-first, uncompressed, written first, ordered by
//              parse criticality; no byte alignment before the payload (§3.6)
// packet §3.4  parses with zero coder state
// packet §8.1  written field by field through BitWriter — no struct layout,
//              no bitfields, no memcpy
//
// Layout. Bit 0 is the most significant bit of byte 0. Fields follow each
// other in exactly this order with nothing between them:
//
//   field          bits  present when              wire values
//   ------------   ----  ------------------------  ------------------------------
//   tier             2   always                    01 Tier 1 · 10 Tier 2
//                                                  00, 11 rejected by the parser
//   symbol_count     5   always                    0…30 the count · 31 = escape
//   count_ext       11   symbol_count field = 31   count − 31   (counts 31…2078)
//   seq              8   always                    low 8 bits of the wide counter
//   hash_present     1   always                    0 · 1
//   priority         1   always                    0 NORMAL · 1 CRITICAL
//   negation         2   tier = 1                  00 false · 11 true
//                                                  01, 10 copies disagree → rejected
//   language         4   tier = 2                  opaque LangId 0…15
//   context_hash    12   hash_present = 1          wire_context_hash() of the hash
//
//   Totals without the escape: Tier 1 19 bits, 31 with hash;
//                              Tier 2 21 bits, 33 with hash.
//   The escape adds 11 bits. The coder payload (coder.h) starts on the next bit.
//
// Resolved in Phase 3, where packet §3 fixed widths but not values (recorded
// in the packet spec's implementation resolutions):
//   - tier wire values 01 / 10
//   - the escape: extension immediately follows the 5-bit field and carries
//     count − 31, so every count has exactly one encoding
//   - negation copies 00 / 11
//   - tier-specific field before context_hash
//   - the 16-bit → 12-bit context hash mapping (wire_context_hash)
//
// NOT resolved here: which language each LangId value denotes. This layer
// carries a 4-bit value; the mapping belongs to the language layer (Phase 6).

#include "common/bitio.h"
#include "common/types.h"

namespace itantra {

constexpr u8 kPacketFormatVersion = 1u;

enum class Tier : u8 {
    Tier1 = 1,
    Tier2 = 2,
};

// packet §11: two states. HIGH does not exist.
enum class Priority : u8 {
    Normal   = 0,
    Critical = 1,
};

using LangId = u8;

constexpr u8 kTierBits              = 2u;
constexpr u8 kSymbolCountBits       = 5u;
constexpr u8 kSymbolCountEscapeBits = 11u;
constexpr u8 kSeqBits               = 8u;
constexpr u8 kHashPresentBits       = 1u;
constexpr u8 kPriorityBits          = 1u;
constexpr u8 kNegationBits          = 2u;
constexpr u8 kLanguageBits          = 4u;
constexpr u8 kContextHashBits       = 12u;

constexpr u32 kSymbolCountEscape  = 31u;
constexpr u32 kMaxSymbolCount     = kSymbolCountEscape + ((1u << kSymbolCountEscapeBits) - 1u);
constexpr u32 kMaxLanguage        = (1u << kLanguageBits) - 1u;
constexpr u32 kMaxWireContextHash = (1u << kContextHashBits) - 1u;
constexpr u32 kMaxMetadataBits    = kTierBits + kSymbolCountBits + kSymbolCountEscapeBits + kSeqBits +
                                    kHashPresentBits + kPriorityBits + kLanguageBits + kContextHashBits;

static_assert(kMaxSymbolCount == 2078u, "symbol_count escape range");
static_assert(kMaxMetadataBits == 44u, "largest metadata block");

struct Metadata {
    Tier     tier         = Tier::Tier1;
    u16      symbol_count = 0u;
    u8       seq          = 0u;
    bool     hash_present = false;
    Priority priority     = Priority::Normal;
    bool     negation     = false;   // Tier 1 only; false on Tier 2
    LangId   language     = 0u;      // Tier 2 only; 0 on Tier 1
    u16      context_hash = 0u;      // 12-bit WIRE value; 0 unless hash_present
};

inline bool operator==(const Metadata& a, const Metadata& b) noexcept {
    return a.tier == b.tier && a.symbol_count == b.symbol_count && a.seq == b.seq &&
           a.hash_present == b.hash_present && a.priority == b.priority &&
           a.negation == b.negation && a.language == b.language &&
           a.context_hash == b.context_hash;
}

inline bool operator!=(const Metadata& a, const Metadata& b) noexcept {
    return !(a == b);
}

// Why a Metadata value cannot be written. Every writable Metadata has exactly
// one encoding, and parsing that encoding returns an equal Metadata.
enum class MetadataFault : u8 {
    None,
    Tier,                     // not Tier1 / Tier2
    SymbolCount,              // above kMaxSymbolCount
    Priority,                 // not Normal / Critical
    NegationOnTier2,          // negation is a Tier 1 field
    LanguageOnTier1,          // language is a Tier 2 field
    Language,                 // above kMaxLanguage
    ContextHashTooWide,       // hash_present, but above kMaxWireContextHash
    ContextHashWithoutFlag,   // context_hash != 0 while hash_present is 0
};

MetadataFault validate_metadata(const Metadata& m) noexcept;

// Bits write_metadata() emits for a valid `m`.
u32 metadata_bit_length(const Metadata& m) noexcept;

// ---------------------------------------------------------------------------
// One encoder and one decoder per wire field (handoff: "Every wire-format
// field has an explicit encoder and decoder").
//
// put_* preconditions (value in range) are asserted; callers validate first.
// get_* read exactly the field's bits; past the end of the buffer they read
// zeros and the reader's overran() reports it.
// ---------------------------------------------------------------------------

void put_tier(BitWriter& out, Tier tier) noexcept;
bool get_tier(BitReader& in, Tier& tier) noexcept;   // false: 00 or 11

void put_symbol_count(BitWriter& out, u16 count) noexcept;   // 5 or 16 bits
u16  get_symbol_count(BitReader& in) noexcept;

void put_seq(BitWriter& out, u8 seq) noexcept;
u8   get_seq(BitReader& in) noexcept;

void put_hash_present(BitWriter& out, bool present) noexcept;
bool get_hash_present(BitReader& in) noexcept;

void     put_priority(BitWriter& out, Priority priority) noexcept;
Priority get_priority(BitReader& in) noexcept;

// Two copies of the same bit (packet §3.2, receiver §3④).
void put_negation(BitWriter& out, bool negation) noexcept;
// Returns false when the copies disagree; `negation` is then false and must
// not be used — the packet is rejected, never decoded with a guessed value.
bool get_negation(BitReader& in, bool& negation) noexcept;

void   put_language(BitWriter& out, LangId language) noexcept;
LangId get_language(BitReader& in) noexcept;

void put_context_hash(BitWriter& out, u16 wire_hash) noexcept;   // 12 bits
u16  get_context_hash(BitReader& in) noexcept;

// ---------------------------------------------------------------------------
// Whole block
// ---------------------------------------------------------------------------

// Writes `m` in the frozen order. False, writing nothing, if `m` is invalid;
// otherwise returns out.ok().
bool write_metadata(BitWriter& out, const Metadata& m) noexcept;

enum class MetadataStatus : u8 {
    Ok,
    Truncated,          // the block runs past the end of the buffer
    BadTier,            // tier 00 or 11; nothing after it is read
    NegationMismatch,   // Tier 1 negation copies disagree (receiver §3④)
};

// Reads one metadata block. On NegationMismatch every other field is still
// filled in; on Truncated and BadTier the contents of `m` are unspecified.
MetadataStatus read_metadata(BitReader& in, Metadata& m) noexcept;

// ---------------------------------------------------------------------------
// Context hash on the wire — packet §3.3, context §5.2
// ---------------------------------------------------------------------------
//
// FROZEN (Phase 3): the 12-bit wire field is the LOW 12 BITS of the 16-bit
// CRC-16/CCITT-FALSE context hash (common/hash.h):
//
//     wire = hash16 & 0x0FFF
//
// Both phones apply it to their own pre-message hash and compare wire values.
//
// Chosen by measuring every candidate over the §5.2 input (Phase 3
// analysis, [H]). No 16 → 12 bit map can catch every change confined to one
// slot's `current` — 15 of the 65,535 alternatives per slot always collide —
// so candidates differ only on smaller changes. Low 12 bits:
//
//     every single-bit flip in the 192 input bits      detected
//     every change confined to one slot's `ver`         detected
//     two-bit flips                                     5 of 18,336 collide
//
// high 12 bits (h >> 4) and XOR folds matched or lost on every row.
u16 wire_context_hash(u16 context_hash16) noexcept;

}  // namespace itantra
