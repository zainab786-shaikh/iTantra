#include "tier2/boost.h"

#include <algorithm>

#include "lang/pack.h"
#include "tier2/wire.h"

namespace itantra {

namespace {

bool fail(std::string& error, const char* why) {
    error = why;
    return false;
}

u32 key_of(u32 slot, u32 value) noexcept {
    return (slot << 16) | value;
}

}  // namespace

bool BoostTable::load(const u8* payload, std::size_t length, u32 vocab_size, std::string& error) {
    *this = BoostTable{};
    tier2wire::Reader r(payload, length);

    u32 version   = 0u;
    u32 v         = 0u;
    u32 magnitude = 0u;
    u32 count     = 0u;
    if (!r.read32(version) || !r.read32(v) || !r.read32(magnitude) || !r.read32(count)) {
        return fail(error, "truncated header");
    }
    if (version != kBoostTableVersion) return fail(error, "unsupported table version");
    if (v != vocab_size) return fail(error, "vocabulary size does not match the subword vocabulary");
    if (magnitude == 0u) return fail(error, "boost magnitude must be at least 1");
    if (count > r.remaining() / 9u) return fail(error, "truncated entries");   // each entry >= 9 bytes

    std::vector<Entry> entries;
    std::vector<u32>   tokens;
    entries.reserve(count);
    u32 largest[kBoostSlotCount] = {};
    for (u32 k = 0u; k < count; ++k) {
        u8  slot  = 0u;
        u16 value = 0u;
        u16 n     = 0u;
        if (!r.read8(slot) || !r.read16(value) || !r.read16(n)) return fail(error, "truncated entries");
        if (slot >= kBoostSlotCount) return fail(error, "entry slot is not ACTOR ... STATE");
        if (value == 0u) return fail(error, "entry value 0 means empty and is never boosted");
        if (n == 0u) return fail(error, "entry without tokens");
        if (!entries.empty() && key_of(slot, value) <= key_of(entries.back().slot, entries.back().value)) {
            return fail(error, "entries not strictly ascending by (slot, value)");
        }
        const Entry entry{slot, value, static_cast<u32>(tokens.size()), n};
        for (u32 j = 0u; j < n; ++j) {
            u32 token = 0u;
            if (!r.read32(token)) return fail(error, "truncated entries");
            if (token >= v) return fail(error, "entry token out of range");
            if (j != 0u && token <= tokens.back()) return fail(error, "entry tokens not strictly ascending");
            tokens.push_back(token);
        }
        largest[slot] = std::max<u32>(largest[slot], n);
        entries.push_back(entry);
    }
    if (r.remaining() != 0u) return fail(error, "trailing bytes");

    u64 bound = 0u;
    for (u32 s = 0u; s < kBoostSlotCount; ++s) bound += largest[s];
    if (bound * magnitude > kBoostMassLimit) {
        return fail(error, "magnitude times the largest entries exceeds the boost mass limit");
    }

    magnitude_ = magnitude;
    entries_   = std::move(entries);
    tokens_    = std::move(tokens);
    return true;
}

const u32* BoostTable::tokens(u8 slot, u16 value, u32& count) const noexcept {
    count = 0u;
    const u32 key = key_of(slot, value);
    std::size_t lo = 0u;
    std::size_t hi = entries_.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2u;
        if (key_of(entries_[mid].slot, entries_[mid].value) < key) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo == entries_.size() || key_of(entries_[lo].slot, entries_[lo].value) != key) return nullptr;
    count = entries_[lo].count;
    return &tokens_[entries_[lo].first];
}

void ContextBoost::build(const BoostTable& table, const Context& context) {
    tokens_.clear();
    magnitude_ = table.magnitude();
    for (u32 slot = 0u; slot < kBoostSlotCount; ++slot) {
        const u16 value = context.slots[slot].current;
        if (value == 0u) continue;
        u32 count = 0u;
        const u32* t = table.tokens(static_cast<u8>(slot), value, count);
        if (count != 0u) tokens_.insert(tokens_.end(), t, t + count);
    }
    std::sort(tokens_.begin(), tokens_.end());
    tokens_.erase(std::unique(tokens_.begin(), tokens_.end()), tokens_.end());
}

std::vector<u8> serialize_boost(const BoostSource& source) {
    std::vector<u8> payload;
    tier2wire::put32(payload, kBoostTableVersion);
    tier2wire::put32(payload, source.vocab_size);
    tier2wire::put32(payload, source.magnitude);
    tier2wire::put32(payload, static_cast<u32>(source.entries.size()));
    for (const BoostSource::Entry& e : source.entries) {
        tier2wire::put8(payload, e.slot);
        tier2wire::put16(payload, e.value);
        tier2wire::put16(payload, static_cast<u32>(e.tokens.size()));
        for (u32 t : e.tokens) tier2wire::put32(payload, t);
    }
    return wrap_container(PackKind::Boost, payload);
}

}  // namespace itantra
