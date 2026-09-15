#include "packet/metadata.h"

#include <cassert>

namespace itantra {

MetadataFault validate_metadata(const Metadata& m) noexcept {
    if (m.tier != Tier::Tier1 && m.tier != Tier::Tier2) return MetadataFault::Tier;
    if (m.symbol_count > kMaxSymbolCount) return MetadataFault::SymbolCount;
    if (m.priority != Priority::Normal && m.priority != Priority::Critical) {
        return MetadataFault::Priority;
    }
    if (m.tier == Tier::Tier1) {
        if (m.language != 0u) return MetadataFault::LanguageOnTier1;
    } else {
        if (m.negation) return MetadataFault::NegationOnTier2;
        if (m.language > kMaxLanguage) return MetadataFault::Language;
    }
    if (m.hash_present) {
        if (m.context_hash > kMaxWireContextHash) return MetadataFault::ContextHashTooWide;
    } else {
        if (m.context_hash != 0u) return MetadataFault::ContextHashWithoutFlag;
    }
    return MetadataFault::None;
}

u32 metadata_bit_length(const Metadata& m) noexcept {
    u32 bits = u32{kTierBits} + kSymbolCountBits + kSeqBits + kHashPresentBits + kPriorityBits;
    if (m.symbol_count >= kSymbolCountEscape) bits += kSymbolCountEscapeBits;
    bits += (m.tier == Tier::Tier1) ? kNegationBits : kLanguageBits;
    if (m.hash_present) bits += kContextHashBits;
    return bits;
}

// ---------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------

void put_tier(BitWriter& out, Tier tier) noexcept {
    assert(tier == Tier::Tier1 || tier == Tier::Tier2);
    out.write(static_cast<u32>(tier), kTierBits);
}

bool get_tier(BitReader& in, Tier& tier) noexcept {
    const u32 v = in.read(kTierBits);
    if (v == static_cast<u32>(Tier::Tier1)) {
        tier = Tier::Tier1;
        return true;
    }
    if (v == static_cast<u32>(Tier::Tier2)) {
        tier = Tier::Tier2;
        return true;
    }
    return false;
}

void put_symbol_count(BitWriter& out, u16 count) noexcept {
    assert(count <= kMaxSymbolCount);
    if (count < kSymbolCountEscape) {
        out.write(count, kSymbolCountBits);
        return;
    }
    out.write(kSymbolCountEscape, kSymbolCountBits);
    out.write(count - kSymbolCountEscape, kSymbolCountEscapeBits);
}

u16 get_symbol_count(BitReader& in) noexcept {
    const u32 v = in.read(kSymbolCountBits);
    if (v < kSymbolCountEscape) return static_cast<u16>(v);
    return static_cast<u16>(kSymbolCountEscape + in.read(kSymbolCountEscapeBits));
}

void put_seq(BitWriter& out, u8 seq) noexcept {
    out.write(seq, kSeqBits);
}

u8 get_seq(BitReader& in) noexcept {
    return static_cast<u8>(in.read(kSeqBits));
}

void put_hash_present(BitWriter& out, bool present) noexcept {
    out.write(present ? 1u : 0u, kHashPresentBits);
}

bool get_hash_present(BitReader& in) noexcept {
    return in.read(kHashPresentBits) != 0u;
}

void put_priority(BitWriter& out, Priority priority) noexcept {
    assert(priority == Priority::Normal || priority == Priority::Critical);
    out.write(static_cast<u32>(priority), kPriorityBits);
}

Priority get_priority(BitReader& in) noexcept {
    return in.read(kPriorityBits) != 0u ? Priority::Critical : Priority::Normal;
}

void put_negation(BitWriter& out, bool negation) noexcept {
    const u32 bit = negation ? 1u : 0u;
    out.write((bit << 1) | bit, kNegationBits);
}

bool get_negation(BitReader& in, bool& negation) noexcept {
    const u32 copies = in.read(kNegationBits);
    if (copies == 0x0u) {
        negation = false;
        return true;
    }
    if (copies == 0x3u) {
        negation = true;
        return true;
    }
    negation = false;
    return false;
}

void put_language(BitWriter& out, LangId language) noexcept {
    assert(language <= kMaxLanguage);
    out.write(language, kLanguageBits);
}

LangId get_language(BitReader& in) noexcept {
    return static_cast<LangId>(in.read(kLanguageBits));
}

void put_context_hash(BitWriter& out, u16 wire_hash) noexcept {
    assert(wire_hash <= kMaxWireContextHash);
    out.write(wire_hash, kContextHashBits);
}

u16 get_context_hash(BitReader& in) noexcept {
    return static_cast<u16>(in.read(kContextHashBits));
}

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------

bool write_metadata(BitWriter& out, const Metadata& m) noexcept {
    if (validate_metadata(m) != MetadataFault::None) return false;

    put_tier(out, m.tier);
    put_symbol_count(out, m.symbol_count);
    put_seq(out, m.seq);
    put_hash_present(out, m.hash_present);
    put_priority(out, m.priority);
    if (m.tier == Tier::Tier1) {
        put_negation(out, m.negation);
    } else {
        put_language(out, m.language);
    }
    if (m.hash_present) put_context_hash(out, m.context_hash);
    return out.ok();
}

MetadataStatus read_metadata(BitReader& in, Metadata& m) noexcept {
    m = Metadata{};

    if (!get_tier(in, m.tier)) {
        return in.overran() ? MetadataStatus::Truncated : MetadataStatus::BadTier;
    }
    m.symbol_count = get_symbol_count(in);
    m.seq          = get_seq(in);
    m.hash_present = get_hash_present(in);
    m.priority     = get_priority(in);

    bool negation_agrees = true;
    if (m.tier == Tier::Tier1) {
        negation_agrees = get_negation(in, m.negation);
    } else {
        m.language = get_language(in);
    }
    if (m.hash_present) m.context_hash = get_context_hash(in);

    if (in.overran()) return MetadataStatus::Truncated;
    return negation_agrees ? MetadataStatus::Ok : MetadataStatus::NegationMismatch;
}

u16 wire_context_hash(u16 context_hash16) noexcept {
    return static_cast<u16>(context_hash16 & kMaxWireContextHash);
}

}  // namespace itantra
