#pragma once

// Golden vector file — contract §2.2, packet §6.10.1, implementation plan
// Phase 3. Shared by the generator (run once) and the conformance tests.
//
// The file is SELF-CONTAINED: it carries the probability tables as well as the
// input symbols, the metadata and the expected bytes, so the contract cannot
// drift through a table defined in code.
//
// FILE FORMAT, version 1. Every integer big-endian; nothing depends on host
// layout or byte order.
//
//   magic                      4   "ITGV"
//   file_version              u16  1
//   packet_format_version      u8  metadata.h kPacketFormatVersion
//   coder_version              u8  coder.h kCoderVersion
//   model_count               u16
//   vector_count              u16
//   model × model_count
//     alphabet_size           u32
//     frequency               u32 × alphabet_size
//   vector × vector_count
//     name_length              u8,  name (ASCII)
//     tier                     u8   1 | 2
//     symbol_count            u16
//     seq                      u8
//     hash_present             u8   0 | 1
//     priority                 u8   0 NORMAL | 1 CRITICAL
//     negation                 u8   0 | 1
//     language                 u8
//     context_hash            u16   12-bit wire value
//     schedule_mode            u8   0 cyclic | 1 last entry repeats
//     schedule_length          u8   >= 1
//     model_index             u16 × schedule_length
//     symbol                  u32 × symbol_count
//     metadata_bits           u16
//     payload_length          u16,  payload bytes (plaintext, pre-AEAD)
//   crc32                     u32   CRC-32/ISO-HDLC of every preceding byte
//
// Model for position i:  cyclic → schedule[i % length]
//                        last   → schedule[min(i, length − 1)]

#include "coder/coder.h"
#include "coder/static_model.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "packet/parse.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace golden {

using itantra::u16;
using itantra::u32;
using itantra::u8;

constexpr u16 kFileVersion          = 1u;
constexpr u8  kScheduleCyclic       = 0u;
constexpr u8  kScheduleLastRepeats  = 1u;
constexpr u8  kMagic[4]             = {'I', 'T', 'G', 'V'};

struct GoldenModel {
    std::vector<u32>     frequencies;
    itantra::StaticModel table;
};

struct GoldenVector {
    std::string               name;
    itantra::Metadata         metadata;
    u8                        schedule_mode = kScheduleCyclic;
    std::vector<u16>          schedule;
    std::vector<itantra::Symbol> symbols;
    u16                       metadata_bits = 0u;
    std::vector<u8>           payload;
};

struct GoldenFile {
    u16                       file_version          = kFileVersion;
    u8                        packet_format_version = itantra::kPacketFormatVersion;
    u8                        coder_version         = itantra::kCoderVersion;
    std::vector<GoldenModel>  models;
    std::vector<GoldenVector> vectors;
};

inline u32 crc32(const u8* data, std::size_t length) {
    u32 crc = 0xFFFFFFFFu;
    for (std::size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (u32 k = 0u; k < 8u; ++k) crc = (crc & 1u) != 0u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

class ScheduledModel final : public itantra::PayloadModel {
public:
    ScheduledModel(const std::vector<GoldenModel>& models, const GoldenVector& vector)
        : models_(&models), vector_(&vector) {}

    const itantra::Model& model_at(u32 position, const itantra::Symbol*) const noexcept override {
        const std::size_t n = vector_->schedule.size();
        const std::size_t k = vector_->schedule_mode == kScheduleCyclic
                                  ? position % n
                                  : (position < n ? position : n - 1u);
        return (*models_)[vector_->schedule[k]].table;
    }

private:
    const std::vector<GoldenModel>* models_;
    const GoldenVector*             vector_;
};

class FixedSelector final : public itantra::ModelSelector {
public:
    explicit FixedSelector(const itantra::PayloadModel* model) : model_(model) {}
    const itantra::PayloadModel* select(const itantra::Metadata&) const noexcept override {
        return model_;
    }

private:
    const itantra::PayloadModel* model_;
};

inline itantra::AssemblyInput input_of(const GoldenVector& v, const itantra::PayloadModel& model) {
    itantra::AssemblyInput in;
    in.tier         = v.metadata.tier;
    in.symbols      = v.symbols.empty() ? nullptr : v.symbols.data();
    in.symbol_count = static_cast<u16>(v.symbols.size());
    in.model        = &model;
    in.seq          = v.metadata.seq;
    in.priority     = v.metadata.priority;
    in.negation     = v.metadata.negation;
    in.language     = v.metadata.language;
    in.hash_present = v.metadata.hash_present;
    in.context_hash = v.metadata.context_hash;
    return in;
}

// Bits through the end of the flush; everything after is padding.
inline u32 coded_bit_length(const itantra::AssemblyInput& in) {
    std::vector<u8> scratch(itantra::kMaxPayloadBytes);
    itantra::BitWriter w(scratch.data(), itantra::kMaxPayloadBytes);
    itantra::write_metadata(w, itantra::metadata_of(in));
    itantra::ArithmeticEncoder enc(w);
    for (u32 i = 0u; i < in.symbol_count; ++i) enc.encode(in.model->model_at(i, in.symbols), in.symbols[i]);
    enc.finish();
    return w.bit_length();
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

namespace detail {

struct Out {
    std::vector<u8> bytes;
    void put8(u32 v) { bytes.push_back(static_cast<u8>(v & 0xFFu)); }
    void put16(u32 v) { put8(v >> 8); put8(v); }
    void put32(u32 v) { put16(v >> 16); put16(v & 0xFFFFu); }
};

struct In {
    const std::vector<u8>& bytes;
    std::size_t            pos = 0u;
    bool                   ok  = true;

    explicit In(const std::vector<u8>& b) : bytes(b) {}
    bool has(std::size_t n) const { return ok && bytes.size() - pos >= n && pos <= bytes.size(); }
    u32 get8() {
        if (!has(1u)) { ok = false; return 0u; }
        return bytes[pos++];
    }
    u32 get16() { const u32 hi = get8(); return (hi << 8) | get8(); }
    u32 get32() { const u32 hi = get16(); return (hi << 16) | get16(); }
};

}  // namespace detail

inline std::vector<u8> serialize(const GoldenFile& f) {
    detail::Out o;
    for (u8 m : kMagic) o.put8(m);
    o.put16(f.file_version);
    o.put8(f.packet_format_version);
    o.put8(f.coder_version);
    o.put16(static_cast<u32>(f.models.size()));
    o.put16(static_cast<u32>(f.vectors.size()));
    for (const GoldenModel& m : f.models) {
        o.put32(static_cast<u32>(m.frequencies.size()));
        for (u32 x : m.frequencies) o.put32(x);
    }
    for (const GoldenVector& v : f.vectors) {
        o.put8(static_cast<u32>(v.name.size()));
        for (char c : v.name) o.put8(static_cast<u8>(c));
        o.put8(static_cast<u32>(v.metadata.tier));
        o.put16(static_cast<u32>(v.symbols.size()));
        o.put8(v.metadata.seq);
        o.put8(v.metadata.hash_present ? 1u : 0u);
        o.put8(static_cast<u32>(v.metadata.priority));
        o.put8(v.metadata.negation ? 1u : 0u);
        o.put8(v.metadata.language);
        o.put16(v.metadata.context_hash);
        o.put8(v.schedule_mode);
        o.put8(static_cast<u32>(v.schedule.size()));
        for (u16 idx : v.schedule) o.put16(idx);
        for (itantra::Symbol s : v.symbols) o.put32(s);
        o.put16(v.metadata_bits);
        o.put16(static_cast<u32>(v.payload.size()));
        for (u8 b : v.payload) o.put8(b);
    }
    o.put32(crc32(o.bytes.data(), o.bytes.size()));
    return o.bytes;
}

inline bool deserialize(const std::vector<u8>& bytes, GoldenFile& f, std::string& error) {
    f = GoldenFile{};
    if (bytes.size() < 4u + 4u) { error = "file too short"; return false; }
    const std::size_t body = bytes.size() - 4u;
    const u32 stored = (u32{bytes[body]} << 24) | (u32{bytes[body + 1u]} << 16) |
                       (u32{bytes[body + 2u]} << 8) | u32{bytes[body + 3u]};
    if (crc32(bytes.data(), body) != stored) { error = "CRC-32 mismatch: file corrupted or edited"; return false; }

    const std::vector<u8> content(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(body));
    detail::In in(content);
    for (u8 m : kMagic) {
        if (in.get8() != m) { error = "bad magic"; return false; }
    }
    f.file_version          = static_cast<u16>(in.get16());
    f.packet_format_version = static_cast<u8>(in.get8());
    f.coder_version         = static_cast<u8>(in.get8());
    const u32 model_count   = in.get16();
    const u32 vector_count  = in.get16();

    for (u32 i = 0u; i < model_count && in.ok; ++i) {
        const u32 n = in.get32();
        if (n == 0u || n > itantra::kModelMaxTotal || !in.has(4u * static_cast<std::size_t>(n))) {
            error = "bad model " + std::to_string(i); return false;
        }
        GoldenModel m;
        m.frequencies.resize(n);
        for (u32& x : m.frequencies) x = in.get32();
        if (!m.table.assign(m.frequencies.data(), n)) {
            error = "invalid frequency table " + std::to_string(i); return false;
        }
        f.models.push_back(std::move(m));
    }

    for (u32 i = 0u; i < vector_count && in.ok; ++i) {
        GoldenVector v;
        const u32 name_length = in.get8();
        for (u32 k = 0u; k < name_length; ++k) v.name.push_back(static_cast<char>(in.get8()));
        const std::string where = "vector " + std::to_string(i) + " (" + v.name + ")";

        const u32 tier  = in.get8();
        const u32 count = in.get16();
        v.metadata.tier         = static_cast<itantra::Tier>(tier);
        v.metadata.symbol_count = static_cast<u16>(count);
        v.metadata.seq          = static_cast<u8>(in.get8());
        const u32 hp            = in.get8();
        const u32 priority      = in.get8();
        const u32 negation      = in.get8();
        v.metadata.hash_present = hp == 1u;
        v.metadata.priority     = static_cast<itantra::Priority>(priority);
        v.metadata.negation     = negation == 1u;
        v.metadata.language     = static_cast<u8>(in.get8());
        v.metadata.context_hash = static_cast<u16>(in.get16());
        if (hp > 1u || negation > 1u ||
            itantra::validate_metadata(v.metadata) != itantra::MetadataFault::None) {
            error = where + ": invalid metadata"; return false;
        }

        v.schedule_mode = static_cast<u8>(in.get8());
        const u32 schedule_length = in.get8();
        if (v.schedule_mode > kScheduleLastRepeats || schedule_length == 0u) {
            error = where + ": invalid schedule"; return false;
        }
        for (u32 k = 0u; k < schedule_length; ++k) {
            const u32 idx = in.get16();
            if (idx >= f.models.size()) { error = where + ": model index out of range"; return false; }
            v.schedule.push_back(static_cast<u16>(idx));
        }

        if (!in.has(4u * static_cast<std::size_t>(count))) { error = where + ": truncated symbols"; return false; }
        v.symbols.resize(count);
        for (itantra::Symbol& s : v.symbols) s = in.get32();

        v.metadata_bits = static_cast<u16>(in.get16());
        const u32 payload_length = in.get16();
        if (payload_length == 0u || payload_length > itantra::kMaxPayloadBytes || !in.has(payload_length)) {
            error = where + ": invalid payload length"; return false;
        }
        v.payload.resize(payload_length);
        for (u8& b : v.payload) b = static_cast<u8>(in.get8());
        f.vectors.push_back(std::move(v));
    }

    if (!in.ok) { error = "truncated file"; return false; }
    if (in.pos != content.size()) { error = "trailing bytes after the last vector"; return false; }
    return true;
}

inline bool read_file(const char* path, std::vector<u8>& bytes) {
    bytes.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return false;
    bytes.reserve(raw.size());
    for (char c : raw) bytes.push_back(static_cast<u8>(c));
    return true;
}

inline bool write_file(const char* path, const std::vector<u8>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (u8 b : bytes) out.put(static_cast<char>(b));
    out.flush();
    return static_cast<bool>(out);
}

inline bool file_exists(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

}  // namespace golden
