#pragma once

// Tier 1 / Tier 2 golden vectors — contract §2.2, packet §6.10.1 (implementation
// resolutions, Phases 7–8). Implementation plan Phase 8, end.
//
// A SEPARATE frozen artifact: native/test/golden/tier_vectors.bin. The Phase 3
// file native/test/golden/vectors.bin is never extended or regenerated.
//
// Where vectors.bin pins the coder and the packet format with plain frequency
// tables, this file pins the Tier-specific determinism surface C-01 must also
// cover: the Tier 2 tokenizer, the Tier 2 model (floor, backoff, boost), the
// Tier 1 frame layout and static models, and literal coding.
//
// SELF-CONTAINED, like vectors.bin: it embeds the exact table files the vectors
// were built from (common/concepts.bin, intents.bin, schema_version;
// tier2/subwords.bin, ngram.bin, boost.bin). Those are the SYNTHETIC FIXTURE
// tables — so these vectors are a determinism check, not production data.
// Production tables get vectors of their own.
//
// FILE FORMAT, version 1. Every integer big-endian.
//
//   magic                     4   "ITTV"
//   file_version             u16  1
//   packet_format_version     u8  metadata.h kPacketFormatVersion
//   coder_version             u8  coder.h kCoderVersion
//   tokenizer_version         u8  tier2/subword.h kTokenizerVersion
//   ngram_table_version       u8  tier2/ngram.h kNgramTableVersion
//   boost_table_version       u8  tier2/boost.h kBoostTableVersion
//   tier1_table_version       u8  tier1/frame.h kTier1TableVersion
//   table_count              u16
//   table × table_count       u8 name_length, name (ASCII), u32 length, bytes
//   vector_count             u16
//   vector × vector_count
//     name_length             u8, name (ASCII)
//     tier                    u8  1 | 2
//     seq                     u8
//     hash_present            u8  0 | 1
//     priority                u8  0 NORMAL | 1 CRITICAL
//     negation                u8  0 | 1
//     language                u8  LangId
//     context_hash           u16  12-bit wire value
//     Tier 2 input:           u32 text_length, text bytes
//                             u8 boosted; if 1: 8 × (u16 current, u8 ver) — the
//                             pre-message context the boost and hash come from
//     Tier 1 input:           u16 intent
//                             7 × (u8 mode, u16 value, u32 literal_length, literal)
//     symbol_count           u16
//     symbol                 u32 × symbol_count
//     metadata_bits          u16
//     payload_length         u16, payload bytes (plaintext, pre-AEAD)
//   crc32                    u32  CRC-32/ISO-HDLC of every preceding byte

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "coder/coder.h"
#include "context/context.h"
#include "golden/golden_format.h"
#include "lang/pack.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "tier1/frame.h"
#include "tier2/boost.h"
#include "tier2/ngram.h"
#include "tier2/subword.h"
#include "tier2/tables.h"

namespace tiergolden {

using itantra::u16;
using itantra::u32;
using itantra::u8;

constexpr u16 kFileVersion = 1u;
constexpr u8  kMagic[4]    = {'I', 'T', 'T', 'V'};

struct TableFile {
    std::string     name;    // e.g. "tier2/ngram.bin"
    std::vector<u8> bytes;
};

struct ContextSnapshot {
    u16 current[itantra::kSlotCount] = {};
    u8  ver[itantra::kSlotCount]     = {};
};

inline ContextSnapshot snapshot_of(const itantra::Context& c) {
    ContextSnapshot s;
    for (u32 i = 0u; i < itantra::kSlotCount; ++i) {
        s.current[i] = c.slots[i].current;
        s.ver[i]     = c.slots[i].ver;
    }
    return s;
}

// A context holding exactly what the hash and the boost read.
inline itantra::Context context_of(const ContextSnapshot& s) {
    itantra::Context c;
    itantra::init_context(c);
    for (u32 i = 0u; i < itantra::kSlotCount; ++i) {
        c.slots[i].current = s.current[i];
        c.slots[i].ver     = s.ver[i];
    }
    c.hash = itantra::context_hash(c);
    return c;
}

struct TierVector {
    std::string                  name;
    itantra::Metadata            metadata;
    std::string                  text;          // Tier 2
    bool                         boosted = false;
    ContextSnapshot              context;       // Tier 2, boosted only
    itantra::Tier1Frame          frame;         // Tier 1
    std::vector<itantra::Symbol> symbols;
    u16                          metadata_bits = 0u;
    std::vector<u8>              payload;
};

struct TierGoldenFile {
    u16                     file_version          = kFileVersion;
    u8                      packet_format_version = itantra::kPacketFormatVersion;
    u8                      coder_version         = itantra::kCoderVersion;
    u8                      tokenizer_version     = static_cast<u8>(itantra::kTokenizerVersion);
    u8                      ngram_version         = static_cast<u8>(itantra::kNgramTableVersion);
    u8                      boost_version         = static_cast<u8>(itantra::kBoostTableVersion);
    u8                      tier1_version         = static_cast<u8>(itantra::kTier1TableVersion);
    std::vector<TableFile>  tables;
    std::vector<TierVector> vectors;
};

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

struct LoadedTables {
    itantra::CommonPack  common;
    itantra::Tier2Tables tier2;

    bool load(const TierGoldenFile& f, std::string& error) {
        itantra::PackFiles common_files;
        itantra::PackFiles tier2_files;
        for (const TableFile& t : f.tables) {
            if (t.name.rfind("common/", 0u) == 0u) common_files.emplace_back(t.name.substr(7u), t.bytes);
            if (t.name.rfind("tier2/", 0u) == 0u) tier2_files.emplace_back(t.name.substr(6u), t.bytes);
        }
        return common.load(std::move(common_files), error) && tier2.load(tier2_files, error);
    }
};

// The vector's symbols, derived from its INPUT (text or frame).
inline bool derive_symbols(const LoadedTables& t, const TierVector& v, std::vector<itantra::Symbol>& out) {
    out.clear();
    if (v.metadata.tier == itantra::Tier::Tier2) {
        t.tier2.vocabulary().tokenize(reinterpret_cast<const u8*>(v.text.data()), v.text.size(), out);
        return true;
    }
    return itantra::frame_to_symbols(t.common, t.tier2.vocabulary(), v.frame, out) == itantra::FrameFault::None;
}

// Assembles `symbols` under the vector's own model and metadata.
inline bool encode_symbols(const LoadedTables& t, const TierVector& v, const std::vector<itantra::Symbol>& symbols,
                           std::vector<u8>& payload, u16& metadata_bits) {
    payload.clear();
    const itantra::Context context = context_of(v.context);
    itantra::ContextBoost boost;
    if (v.boosted) boost.build(t.tier2.boost(), context);
    const itantra::Tier2Model tier2(t.tier2.ngram(), v.boosted ? &boost : nullptr);
    const itantra::Tier1Model tier1(t.common, t.tier2.ngram());

    itantra::AssemblyInput in;
    in.tier         = v.metadata.tier;
    in.symbols      = symbols.empty() ? nullptr : symbols.data();
    in.symbol_count = static_cast<u16>(symbols.size());
    in.model        = v.metadata.tier == itantra::Tier::Tier2 ? static_cast<const itantra::PayloadModel*>(&tier2)
                                                              : static_cast<const itantra::PayloadModel*>(&tier1);
    in.seq          = v.metadata.seq;
    in.priority     = v.metadata.priority;
    in.negation     = v.metadata.negation;
    in.language     = v.metadata.language;
    in.hash_present = v.metadata.hash_present;
    in.context_hash = v.metadata.context_hash;
    const auto out = std::make_unique<itantra::NativePayload>();
    if (itantra::assemble(in, *out) != itantra::AsmResult::Ok) return false;
    payload.assign(out->bytes, out->bytes + out->len);
    metadata_bits = out->metadata_bits;
    return true;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

inline std::vector<u8> serialize(const TierGoldenFile& f) {
    golden::detail::Out o;
    for (u8 m : kMagic) o.put8(m);
    o.put16(f.file_version);
    o.put8(f.packet_format_version);
    o.put8(f.coder_version);
    o.put8(f.tokenizer_version);
    o.put8(f.ngram_version);
    o.put8(f.boost_version);
    o.put8(f.tier1_version);
    o.put16(static_cast<u32>(f.tables.size()));
    for (const TableFile& t : f.tables) {
        o.put8(static_cast<u32>(t.name.size()));
        for (char c : t.name) o.put8(static_cast<u8>(c));
        o.put32(static_cast<u32>(t.bytes.size()));
        for (u8 b : t.bytes) o.put8(b);
    }
    o.put16(static_cast<u32>(f.vectors.size()));
    for (const TierVector& v : f.vectors) {
        o.put8(static_cast<u32>(v.name.size()));
        for (char c : v.name) o.put8(static_cast<u8>(c));
        o.put8(static_cast<u32>(v.metadata.tier));
        o.put8(v.metadata.seq);
        o.put8(v.metadata.hash_present ? 1u : 0u);
        o.put8(static_cast<u32>(v.metadata.priority));
        o.put8(v.metadata.negation ? 1u : 0u);
        o.put8(v.metadata.language);
        o.put16(v.metadata.context_hash);
        if (v.metadata.tier == itantra::Tier::Tier2) {
            o.put32(static_cast<u32>(v.text.size()));
            for (char c : v.text) o.put8(static_cast<u8>(c));
            o.put8(v.boosted ? 1u : 0u);
            if (v.boosted) {
                for (u32 i = 0u; i < itantra::kSlotCount; ++i) {
                    o.put16(v.context.current[i]);
                    o.put8(v.context.ver[i]);
                }
            }
        } else {
            o.put16(v.frame.intent);
            for (const itantra::FrameSlot& fs : v.frame.slots) {
                o.put8(static_cast<u32>(fs.mode));
                o.put16(fs.value);
                o.put32(static_cast<u32>(fs.literal.size()));
                for (char c : fs.literal) o.put8(static_cast<u8>(c));
            }
        }
        o.put16(static_cast<u32>(v.symbols.size()));
        for (itantra::Symbol s : v.symbols) o.put32(s);
        o.put16(v.metadata_bits);
        o.put16(static_cast<u32>(v.payload.size()));
        for (u8 b : v.payload) o.put8(b);
    }
    o.put32(golden::crc32(o.bytes.data(), o.bytes.size()));
    return o.bytes;
}

inline bool deserialize(const std::vector<u8>& bytes, TierGoldenFile& f, std::string& error) {
    f = TierGoldenFile{};
    if (bytes.size() < 8u) {
        error = "file too short";
        return false;
    }
    const std::size_t body = bytes.size() - 4u;
    const u32 stored = (u32{bytes[body]} << 24) | (u32{bytes[body + 1u]} << 16) | (u32{bytes[body + 2u]} << 8) |
                       u32{bytes[body + 3u]};
    if (golden::crc32(bytes.data(), body) != stored) {
        error = "CRC-32 mismatch: file corrupted or edited";
        return false;
    }
    const std::vector<u8> content(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(body));
    golden::detail::In in(content);
    for (u8 m : kMagic) {
        if (in.get8() != m) {
            error = "bad magic";
            return false;
        }
    }
    f.file_version          = static_cast<u16>(in.get16());
    f.packet_format_version = static_cast<u8>(in.get8());
    f.coder_version         = static_cast<u8>(in.get8());
    f.tokenizer_version     = static_cast<u8>(in.get8());
    f.ngram_version         = static_cast<u8>(in.get8());
    f.boost_version         = static_cast<u8>(in.get8());
    f.tier1_version         = static_cast<u8>(in.get8());

    const u32 table_count = in.get16();
    for (u32 i = 0u; i < table_count && in.ok; ++i) {
        TableFile t;
        const u32 name_length = in.get8();
        for (u32 k = 0u; k < name_length; ++k) t.name.push_back(static_cast<char>(in.get8()));
        const u32 length = in.get32();
        if (!in.has(length)) {
            error = "truncated table " + t.name;
            return false;
        }
        t.bytes.resize(length);
        for (u8& b : t.bytes) b = static_cast<u8>(in.get8());
        f.tables.push_back(std::move(t));
    }

    const u32 vector_count = in.get16();
    for (u32 i = 0u; i < vector_count && in.ok; ++i) {
        TierVector v;
        const u32 name_length = in.get8();
        for (u32 k = 0u; k < name_length; ++k) v.name.push_back(static_cast<char>(in.get8()));
        const std::string where = "vector " + std::to_string(i) + " (" + v.name + ")";
        const u32 tier = in.get8();
        if (tier != 1u && tier != 2u) {
            error = where + ": invalid tier";
            return false;
        }
        v.metadata.tier = static_cast<itantra::Tier>(tier);
        v.metadata.seq  = static_cast<u8>(in.get8());
        const u32 hp       = in.get8();
        const u32 priority = in.get8();
        const u32 negation = in.get8();
        v.metadata.language     = static_cast<u8>(in.get8());
        v.metadata.context_hash = static_cast<u16>(in.get16());
        if (hp > 1u || priority > 1u || negation > 1u) {
            error = where + ": invalid flag";
            return false;
        }
        v.metadata.hash_present = hp == 1u;
        v.metadata.priority     = static_cast<itantra::Priority>(priority);
        v.metadata.negation     = negation == 1u;
        if (v.metadata.tier == itantra::Tier::Tier2) {
            const u32 length = in.get32();
            if (!in.has(length)) {
                error = where + ": truncated text";
                return false;
            }
            for (u32 k = 0u; k < length; ++k) v.text.push_back(static_cast<char>(in.get8()));
            const u32 boosted = in.get8();
            if (boosted > 1u) {
                error = where + ": invalid boosted flag";
                return false;
            }
            v.boosted = boosted == 1u;
            if (v.boosted) {
                for (u32 s = 0u; s < itantra::kSlotCount; ++s) {
                    v.context.current[s] = static_cast<u16>(in.get16());
                    v.context.ver[s]     = static_cast<u8>(in.get8());
                }
            }
        } else {
            v.frame.intent = static_cast<u16>(in.get16());
            for (itantra::FrameSlot& fs : v.frame.slots) {
                const u32 mode = in.get8();
                if (mode >= itantra::kSlotModeCount) {
                    error = where + ": invalid slot mode";
                    return false;
                }
                fs.mode  = static_cast<itantra::SlotMode>(mode);
                fs.value = static_cast<u16>(in.get16());
                const u32 length = in.get32();
                if (!in.has(length)) {
                    error = where + ": truncated literal";
                    return false;
                }
                for (u32 k = 0u; k < length; ++k) fs.literal.push_back(static_cast<char>(in.get8()));
            }
        }
        const u32 count = in.get16();
        v.metadata.symbol_count = static_cast<u16>(count);
        if (itantra::validate_metadata(v.metadata) != itantra::MetadataFault::None) {
            error = where + ": invalid metadata";
            return false;
        }
        if (!in.has(4u * static_cast<std::size_t>(count))) {
            error = where + ": truncated symbols";
            return false;
        }
        v.symbols.resize(count);
        for (itantra::Symbol& s : v.symbols) s = in.get32();
        v.metadata_bits = static_cast<u16>(in.get16());
        const u32 payload_length = in.get16();
        if (payload_length == 0u || payload_length > itantra::kMaxPayloadBytes || !in.has(payload_length)) {
            error = where + ": invalid payload length";
            return false;
        }
        v.payload.resize(payload_length);
        for (u8& b : v.payload) b = static_cast<u8>(in.get8());
        f.vectors.push_back(std::move(v));
    }
    if (!in.ok) {
        error = "truncated file";
        return false;
    }
    if (in.pos != content.size()) {
        error = "trailing bytes after the last vector";
        return false;
    }
    return true;
}

}  // namespace tiergolden
