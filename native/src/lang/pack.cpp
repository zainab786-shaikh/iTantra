#include "lang/pack.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "lang/json.h"
#include "lang/languages.h"
#include "lang/utf8.h"

namespace itantra {

namespace {

// ---------------------------------------------------------------------------
// Big-endian byte I/O
// ---------------------------------------------------------------------------

class Writer {
public:
    std::vector<u8> bytes;
    void put8(u32 v) { bytes.push_back(static_cast<u8>(v)); }
    void put16(u32 v) {
        put8(v >> 8);
        put8(v);
    }
    void put32(u32 v) {
        put16(v >> 16);
        put16(v & 0xFFFFu);
    }
    void raw(const std::string& s) { bytes.insert(bytes.end(), s.begin(), s.end()); }
};

class Reader {
public:
    Reader(const u8* data, std::size_t length) : p_(data), n_(length) {}

    bool get8(u8& v) {
        if (n_ - i_ < 1u) return false;
        v = p_[i_++];
        return true;
    }
    bool get16(u16& v) {
        if (n_ - i_ < 2u) return false;
        v = static_cast<u16>((u32{p_[i_]} << 8) | u32{p_[i_ + 1u]});
        i_ += 2u;
        return true;
    }
    bool get32(u32& v) {
        if (n_ - i_ < 4u) return false;
        v = (u32{p_[i_]} << 24) | (u32{p_[i_ + 1u]} << 16) | (u32{p_[i_ + 2u]} << 8) | u32{p_[i_ + 3u]};
        i_ += 4u;
        return true;
    }
    bool take(std::size_t n, const u8*& out) {
        if (n_ - i_ < n) return false;
        out = p_ + i_;
        i_ += n;
        return true;
    }
    std::size_t remaining() const noexcept { return n_ - i_; }

private:
    const u8*   p_;
    std::size_t n_;
    std::size_t i_ = 0u;
};

constexpr const char* kSlotNames[kConceptSlotCount] = {"ACTOR", "OBJECT", "LOCATION", "SEVERITY",
                                                       "QUANTITY", "TIME", "STATE"};

bool valid_utf8(const u8* data, std::size_t length) {
    for (std::size_t i = 0u; i < length;) {
        u32 cp = 0u;
        bool valid = true;
        i += utf8::decode(data, length, i, cp, valid);
        if (!valid) return false;
    }
    return true;
}

bool valid_form_name(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool builtin_number_form(std::string_view name) {
    return name == "digits" || name == "native";
}

const std::vector<u8>* find_file(const PackFiles& files, const std::string& name) {
    for (const PackFile& f : files) {
        if (f.first == name) return &f.second;
    }
    return nullptr;
}

bool require_file(const PackFiles& files, const std::string& name, const std::vector<u8>*& out, std::string& error) {
    out = find_file(files, name);
    if (out == nullptr) {
        error = "missing pack file " + name;
        return false;
    }
    return true;
}

bool fail(std::string& error, const std::string& file, const char* why) {
    error = file + ": " + why;
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Names and templates
// ---------------------------------------------------------------------------

bool slot_from_name(std::string_view name, u8& slot) noexcept {
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        if (name == kSlotNames[s]) {
            slot = static_cast<u8>(s);
            return true;
        }
    }
    return false;
}

const char* slot_name(u8 slot) noexcept {
    return slot < kConceptSlotCount ? kSlotNames[slot] : "NONE";
}

bool parse_template(std::string_view text, std::vector<TemplatePiece>& out) {
    out.clear();
    if (text.empty()) return false;
    std::size_t i = 0u;
    while (i < text.size()) {
        const std::size_t open = text.find('{', i);
        const std::size_t stray = text.find('}', i);
        if (stray != std::string_view::npos && (open == std::string_view::npos || stray < open)) return false;
        if (open == std::string_view::npos) {
            out.push_back(TemplatePiece{false, text.substr(i), 0u});
            break;
        }
        if (open > i) out.push_back(TemplatePiece{false, text.substr(i, open - i), 0u});
        const std::size_t close = text.find('}', open);
        if (close == std::string_view::npos) return false;
        const std::string_view body = text.substr(open + 1u, close - open - 1u);
        if (body.find('{') != std::string_view::npos) return false;
        const std::size_t colon = body.find(':');
        if (colon == std::string_view::npos) return false;
        u8 slot = 0u;
        if (!slot_from_name(body.substr(0u, colon), slot)) return false;
        const std::string_view form = body.substr(colon + 1u);
        if (!valid_form_name(form)) return false;
        out.push_back(TemplatePiece{true, form, slot});
        i = close + 1u;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Files and containers
// ---------------------------------------------------------------------------

bool read_pack_directory(const std::string& directory, const std::vector<std::string>& names, PackFiles& out,
                         std::string& error) {
    out.clear();
    for (const std::string& name : names) {
        const std::string path = directory + "/" + name;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "cannot read " + path;
            return false;
        }
        std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<u8> bytes(raw.size());
        for (std::size_t k = 0u; k < raw.size(); ++k) bytes[k] = static_cast<u8>(raw[k]);
        out.emplace_back(name, std::move(bytes));
    }
    return true;
}

u32 crc32_iso_hdlc(const u8* data, std::size_t length) noexcept {
    u32 crc = 0xFFFFFFFFu;
    for (std::size_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (u32 k = 0u; k < 8u; ++k) crc = (crc & 1u) != 0u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<u8> wrap_container(PackKind kind, const std::vector<u8>& payload) {
    Writer w;
    w.raw("ITLP");
    w.put16(kPackContainerVersion);
    w.put16(static_cast<u32>(kind));
    w.put32(static_cast<u32>(payload.size()));
    w.bytes.insert(w.bytes.end(), payload.begin(), payload.end());
    w.put32(crc32_iso_hdlc(w.bytes.data(), w.bytes.size()));
    return w.bytes;
}

bool unwrap_container(const std::vector<u8>& file, PackKind kind, const u8*& payload, std::size_t& payload_length,
                      std::string& error) {
    payload = nullptr;
    payload_length = 0u;
    if (file.size() < 16u) {
        error = "container too short";
        return false;
    }
    if (file[0] != 'I' || file[1] != 'T' || file[2] != 'L' || file[3] != 'P') {
        error = "bad container magic";
        return false;
    }
    const u32 version = (u32{file[4]} << 8) | u32{file[5]};
    const u32 got_kind = (u32{file[6]} << 8) | u32{file[7]};
    const u32 length = (u32{file[8]} << 24) | (u32{file[9]} << 16) | (u32{file[10]} << 8) | u32{file[11]};
    if (version != kPackContainerVersion) {
        error = "unsupported container version";
        return false;
    }
    if (got_kind != static_cast<u32>(kind)) {
        error = "wrong container kind";
        return false;
    }
    if (static_cast<std::size_t>(length) + 16u != file.size()) {
        error = "container length mismatch";
        return false;
    }
    const std::size_t body = file.size() - 4u;
    const u32 stored = (u32{file[body]} << 24) | (u32{file[body + 1u]} << 16) | (u32{file[body + 2u]} << 8) |
                       u32{file[body + 3u]};
    if (crc32_iso_hdlc(file.data(), body) != stored) {
        error = "container CRC-32 mismatch";
        return false;
    }
    payload = file.data() + 12u;
    payload_length = length;
    return true;
}

// ---------------------------------------------------------------------------
// CommonPack
// ---------------------------------------------------------------------------

const std::vector<std::string>& CommonPack::file_names() {
    static const std::vector<std::string> names = {"concepts.bin", "intents.bin", "schema_version"};
    return names;
}

bool CommonPack::load(PackFiles files, std::string& error) {
    concepts_.clear();
    intents_.clear();
    schema_version_ = 0u;

    const std::vector<u8>* schema = nullptr;
    const std::vector<u8>* concepts = nullptr;
    const std::vector<u8>* intents = nullptr;
    if (!require_file(files, "schema_version", schema, error) || !require_file(files, "concepts.bin", concepts, error) ||
        !require_file(files, "intents.bin", intents, error)) {
        return false;
    }

    u64 version = 0u;
    std::size_t digits = 0u;
    std::size_t k = 0u;
    for (; k < schema->size() && (*schema)[k] >= '0' && (*schema)[k] <= '9'; ++k, ++digits) {
        version = version * 10u + static_cast<u64>((*schema)[k] - '0');
        if (version > 0xFFFFFFFFu) return fail(error, "schema_version", "out of range");
    }
    for (; k < schema->size(); ++k) {
        if ((*schema)[k] != '\n' && (*schema)[k] != '\r') return fail(error, "schema_version", "not a decimal integer");
    }
    if (digits == 0u || version == 0u) return fail(error, "schema_version", "must be a positive integer");
    schema_version_ = static_cast<u32>(version);

    const u8* payload = nullptr;
    std::size_t length = 0u;
    std::string why;
    if (!unwrap_container(*concepts, PackKind::Concepts, payload, length, why)) return fail(error, "concepts.bin", why.c_str());
    Reader rc(payload, length);
    u32 count = 0u;
    if (!rc.get32(count) || rc.remaining() != static_cast<std::size_t>(count) * 8u) return fail(error, "concepts.bin", "bad size");
    for (u32 i = 0u; i < count; ++i) {
        u16 id = 0u;
        u8 slot = 0u, cls = 0u;
        u32 categories = 0u;
        rc.get16(id);
        rc.get8(slot);
        rc.get8(cls);
        rc.get32(categories);
        if (id == 0u) return fail(error, "concepts.bin", "concept id 0 is reserved (empty)");
        if (!concepts_.empty() && id <= concepts_.back().id) return fail(error, "concepts.bin", "ids not strictly ascending");
        if (slot != kNoSlot && slot >= kConceptSlotCount) return fail(error, "concepts.bin", "invalid slot type");
        if (cls > static_cast<u8>(ConceptClass::Modifier)) return fail(error, "concepts.bin", "invalid concept class");
        concepts_.push_back(ConceptInfo{id, slot, static_cast<ConceptClass>(cls), categories});
    }

    if (!unwrap_container(*intents, PackKind::Intents, payload, length, why)) return fail(error, "intents.bin", why.c_str());
    Reader ri(payload, length);
    if (!ri.get32(count) || ri.remaining() != static_cast<std::size_t>(count) * 6u) return fail(error, "intents.bin", "bad size");
    for (u32 i = 0u; i < count; ++i) {
        u16 id = 0u;
        u8 expected = 0u, required = 0u, flags = 0u, reserved = 0u;
        ri.get16(id);
        ri.get8(expected);
        ri.get8(required);
        ri.get8(flags);
        ri.get8(reserved);
        if (id == 0u) return fail(error, "intents.bin", "intent id 0 is reserved (INTENT_NONE)");
        if (!intents_.empty() && id <= intents_.back().id) return fail(error, "intents.bin", "ids not strictly ascending");
        if ((expected & ~kConceptSlotMask) != 0u || (required & ~expected) != 0u) return fail(error, "intents.bin", "invalid slot masks");
        if ((flags & ~0x03u) != 0u || reserved != 0u) return fail(error, "intents.bin", "invalid flags");
        intents_.push_back(IntentInfo{id, expected, required, (flags & 1u) != 0u, (flags & 2u) != 0u});
    }
    return true;
}

const ConceptInfo* CommonPack::concept_info(u16 id) const noexcept {
    const auto it = std::lower_bound(concepts_.begin(), concepts_.end(), id,
                                     [](const ConceptInfo& c, u16 v) { return c.id < v; });
    return (it != concepts_.end() && it->id == id) ? &*it : nullptr;
}

const IntentInfo* CommonPack::intent(u16 id) const noexcept {
    const auto it = std::lower_bound(intents_.begin(), intents_.end(), id,
                                     [](const IntentInfo& c, u16 v) { return c.id < v; });
    return (it != intents_.end() && it->id == id) ? &*it : nullptr;
}

// ---------------------------------------------------------------------------
// LanguagePack
// ---------------------------------------------------------------------------

const std::vector<std::string>& LanguagePack::file_names() {
    static const std::vector<std::string> names = {"meta.json",     "normalize.json", "lexicon.bin",  "forms.bin",
                                                   "templates.bin", "numbers.bin",    "patterns.bin", "negations.bin"};
    return names;
}

bool LanguagePack::load(PackFiles files, const CommonPack& common, std::string& error) {
    *this = LanguagePack{};
    files_ = std::move(files);
    const PackFiles& f = files_;

    const std::vector<u8>* meta = nullptr;
    const std::vector<u8>* normalize = nullptr;
    const std::vector<u8>* lexicon = nullptr;
    const std::vector<u8>* forms = nullptr;
    const std::vector<u8>* templates = nullptr;
    const std::vector<u8>* numbers = nullptr;
    const std::vector<u8>* patterns = nullptr;
    const std::vector<u8>* negations = nullptr;
    if (!require_file(f, "meta.json", meta, error) || !require_file(f, "normalize.json", normalize, error) ||
        !require_file(f, "lexicon.bin", lexicon, error) || !require_file(f, "forms.bin", forms, error) ||
        !require_file(f, "templates.bin", templates, error) || !require_file(f, "numbers.bin", numbers, error) ||
        !require_file(f, "patterns.bin", patterns, error) || !require_file(f, "negations.bin", negations, error)) {
        return false;
    }

    // ---- meta.json ----
    {
        JsonValue root;
        std::string why;
        if (!parse_json(meta->data(), meta->size(), root, why)) return fail(error, "meta.json", why.c_str());
        if (root.kind != JsonValue::Kind::Object) return fail(error, "meta.json", "top level must be an object");
        static const char* const kKeys[] = {"language",  "pack_version", "script", "tts_voice", "stt_confidence_threshold",
                                            "readback_normal", "readback_critical"};
        for (const std::string& key : root.keys) {
            bool known = false;
            for (const char* k : kKeys) known = known || key == k;
            if (!known) return fail(error, "meta.json", ("unknown key " + key).c_str());
        }
        const JsonValue* language = root.member("language");
        const JsonValue* version = root.member("pack_version");
        const JsonValue* script = root.member("script");
        const JsonValue* voice = root.member("tts_voice");
        const JsonValue* threshold = root.member("stt_confidence_threshold");
        if (language == nullptr || language->kind != JsonValue::Kind::String || !is_supported_language(language->string)) {
            return fail(error, "meta.json", "language must be one of the ten supported ISO 639-1 codes");
        }
        if (version == nullptr || version->kind != JsonValue::Kind::Integer || version->integer < 1 ||
            version->integer > 0xFFFFFFFFll) {
            return fail(error, "meta.json", "pack_version must be a positive integer");
        }
        if (script == nullptr || script->kind != JsonValue::Kind::String || script->string.empty()) {
            return fail(error, "meta.json", "script must be a non-empty string");
        }
        if (voice == nullptr || voice->kind != JsonValue::Kind::String || voice->string.empty()) {
            return fail(error, "meta.json", "tts_voice must be a non-empty string");
        }
        if (threshold == nullptr || threshold->kind != JsonValue::Kind::Integer || threshold->integer < 0 ||
            threshold->integer > 65535) {
            return fail(error, "meta.json", "stt_confidence_threshold must be an integer 0 ... 65535");
        }
        const JsonValue* normal = root.member("readback_normal");
        const JsonValue* critical = root.member("readback_critical");
        if (normal == nullptr || normal->kind != JsonValue::Kind::Integer || normal->integer < 0 ||
            normal->integer > kReadbackScale) {
            return fail(error, "meta.json", "readback_normal must be an integer 0 ... 1000 (per-mille)");
        }
        if (critical == nullptr || critical->kind != JsonValue::Kind::Integer || critical->integer < 0 ||
            critical->integer > kReadbackScale) {
            return fail(error, "meta.json", "readback_critical must be an integer 0 ... 1000 (per-mille)");
        }
        if (critical->integer <= normal->integer) {
            return fail(error, "meta.json", "readback_critical must be above readback_normal (tier 5.8)");
        }
        readback_normal_ = static_cast<u32>(normal->integer);
        readback_critical_ = static_cast<u32>(critical->integer);
        language_ = language->string;
        pack_version_ = static_cast<u32>(version->integer);
        script_ = script->string;
        tts_voice_ = voice->string;
        stt_threshold_ = threshold->integer;
    }

    // ---- normalize.json ----
    {
        std::string why;
        if (!parse_normalize_rules(normalize->data(), normalize->size(), rules_, why)) {
            error = why;
            return false;
        }
    }

    const u8* payload = nullptr;
    std::size_t length = 0u;
    std::string why;

    // ---- lexicon.bin ----
    {
        if (!unwrap_container(*lexicon, PackKind::Lexicon, payload, length, why)) return fail(error, "lexicon.bin", why.c_str());
        std::size_t used = 0u;
        if (!lexicon_.attach(payload, length, used, why)) return fail(error, "lexicon.bin", why.c_str());
        Reader r(payload + used, length - used);
        u32 patterns_count = 0u;
        if (!r.get32(patterns_count) || patterns_count != lexicon_.pattern_count()) return fail(error, "lexicon.bin", "pattern table");
        u32 expected_first = 0u;
        for (u32 p = 0u; p < patterns_count; ++p) {
            u32 first = 0u, count = 0u;
            if (!r.get32(first) || !r.get32(count) || first != expected_first || count == 0u) {
                return fail(error, "lexicon.bin", "entry ranges");
            }
            lexicon_ranges_.emplace_back(first, count);
            expected_first += count;
        }
        u32 items = 0u;
        if (!r.get32(items) || items != expected_first || r.remaining() != static_cast<std::size_t>(items) * 4u) {
            return fail(error, "lexicon.bin", "entry table");
        }
        for (u32 i = 0u; i < items; ++i) {
            u16 concept_id = 0u;
            u8 form_class = 0u, origin = 0u;
            r.get16(concept_id);
            r.get8(form_class);
            r.get8(origin);
            if (common.concept_info(concept_id) == nullptr) return fail(error, "lexicon.bin", "unknown concept id");
            if (form_class > 1u || origin > 2u) return fail(error, "lexicon.bin", "invalid form_class or origin");
            lexicon_items_.push_back(
                LexiconEntry{concept_id, static_cast<FormClass>(form_class), static_cast<Origin>(origin)});
        }
        for (const auto& range : lexicon_ranges_) {
            for (u32 a = range.first; a < range.first + range.second; ++a) {
                for (u32 b = a + 1u; b < range.first + range.second; ++b) {
                    if (lexicon_items_[a].concept_id == lexicon_items_[b].concept_id) {
                        return fail(error, "lexicon.bin", "duplicate concept for one surface form");
                    }
                }
            }
        }
    }

    // ---- numbers.bin ----
    {
        if (!unwrap_container(*numbers, PackKind::Numbers, payload, length, why)) return fail(error, "numbers.bin", why.c_str());
        std::size_t used = 0u;
        if (!numbers_.attach(payload, length, used, why)) return fail(error, "numbers.bin", why.c_str());
        Reader r(payload + used, length - used);
        u32 count = 0u;
        if (!r.get32(count) || count != numbers_.pattern_count() || r.remaining() != static_cast<std::size_t>(count) * 4u) {
            return fail(error, "numbers.bin", "value table");
        }
        for (u32 i = 0u; i < count; ++i) {
            u32 v = 0u;
            r.get32(v);
            if (v > 999999999u) return fail(error, "numbers.bin", "value out of range");
            number_values_.push_back(v);
        }
    }

    // ---- patterns.bin ----
    {
        if (!unwrap_container(*patterns, PackKind::Patterns, payload, length, why)) return fail(error, "patterns.bin", why.c_str());
        Reader r(payload, length);
        u32 count = 0u;
        if (!r.get32(count) || count > 4096u) return fail(error, "patterns.bin", "pattern count");
        struct WordRef {
            std::size_t pattern;
            std::size_t element;
            u32 offset;
            u32 length;
        };
        std::vector<WordRef> refs;
        for (u32 i = 0u; i < count; ++i) {
            ValuePattern vp{};
            u8 elements = 0u;
            u32 a = 0u, b = 0u, c = 0u;
            if (!r.get8(vp.slot) || !r.get8(elements) || !r.get32(a) || !r.get32(b) || !r.get32(c) || !r.get32(vp.min) ||
                !r.get32(vp.max)) {
                return fail(error, "patterns.bin", "truncated");
            }
            vp.a = static_cast<i32>(a);
            vp.b = static_cast<i32>(b);
            vp.c = static_cast<i32>(c);
            if (vp.slot >= kConceptSlotCount || elements == 0u || elements > 8u) return fail(error, "patterns.bin", "invalid pattern");
            if (vp.min < 1u || vp.max > 65535u || vp.min > vp.max) return fail(error, "patterns.bin", "invalid value range");
            u32 numbers_in = 0u;
            for (u8 e = 0u; e < elements; ++e) {
                u8 kind = 0u;
                if (!r.get8(kind) || kind > 1u) return fail(error, "patterns.bin", "invalid element");
                PatternElement el{static_cast<PatternElementKind>(kind), std::string()};
                if (el.kind == PatternElementKind::Word) {
                    u32 off = 0u, len = 0u;
                    if (!r.get32(off) || !r.get32(len)) return fail(error, "patterns.bin", "truncated");
                    refs.push_back(WordRef{patterns_.size(), vp.elements.size(), off, len});
                } else {
                    ++numbers_in;
                }
                vp.elements.push_back(el);
            }
            if (numbers_in > 2u || (numbers_in < 2u && vp.b != 0) || (numbers_in == 0u && vp.a != 0)) {
                return fail(error, "patterns.bin", "coefficients do not match number elements");
            }
            patterns_.push_back(std::move(vp));
        }
        u32 pool_size = 0u;
        const u8* pool = nullptr;
        if (!r.get32(pool_size) || !r.take(pool_size, pool) || r.remaining() != 0u) return fail(error, "patterns.bin", "word pool");
        for (const WordRef& ref : refs) {
            if (ref.length == 0u || ref.offset > pool_size || pool_size - ref.offset < ref.length ||
                !valid_utf8(pool + ref.offset, ref.length)) {
                return fail(error, "patterns.bin", "invalid word");
            }
            std::string word(reinterpret_cast<const char*>(pool + ref.offset), ref.length);
            if (word.find(' ') != std::string::npos) return fail(error, "patterns.bin", "a word element must be one token");
            patterns_[ref.pattern].elements[ref.element].word = std::move(word);
        }
    }

    // ---- negations.bin ----
    {
        if (!unwrap_container(*negations, PackKind::Negations, payload, length, why)) {
            return fail(error, "negations.bin", why.c_str());
        }
        std::size_t used = 0u;
        if (!negations_.attach(payload, length, used, why)) return fail(error, "negations.bin", why.c_str());
        if (used != length) return fail(error, "negations.bin", "trailing bytes");
    }

    // ---- forms.bin ----
    {
        if (!unwrap_container(*forms, PackKind::Forms, payload, length, why)) return fail(error, "forms.bin", why.c_str());
        Reader r(payload, length);
        u16 names = 0u;
        if (!r.get16(names)) return fail(error, "forms.bin", "truncated");
        for (u16 i = 0u; i < names; ++i) {
            u8 len = 0u;
            const u8* bytes = nullptr;
            if (!r.get8(len) || !r.take(len, bytes)) return fail(error, "forms.bin", "truncated");
            std::string name(reinterpret_cast<const char*>(bytes), len);
            if (!valid_form_name(name) || builtin_number_form(name)) return fail(error, "forms.bin", "invalid form name");
            if (std::find(form_names_.begin(), form_names_.end(), name) != form_names_.end()) {
                return fail(error, "forms.bin", "duplicate form name");
            }
            form_names_.push_back(name);
        }
        u32 count = 0u;
        if (!r.get32(count)) return fail(error, "forms.bin", "truncated");
        for (u32 i = 0u; i < count; ++i) {
            FormEntry e{};
            if (!r.get16(e.concept_id) || !r.get16(e.form_id) || !r.get32(e.offset) || !r.get32(e.length)) {
                return fail(error, "forms.bin", "truncated");
            }
            if (common.concept_info(e.concept_id) == nullptr) return fail(error, "forms.bin", "unknown concept id");
            if (e.form_id >= names) return fail(error, "forms.bin", "invalid form id");
            if (!forms_.empty() && (e.concept_id < forms_.back().concept_id ||
                                    (e.concept_id == forms_.back().concept_id && e.form_id <= forms_.back().form_id))) {
                return fail(error, "forms.bin", "entries not strictly sorted");
            }
            forms_.push_back(e);
        }
        u32 pool_size = 0u;
        const u8* pool = nullptr;
        if (!r.get32(pool_size) || !r.take(pool_size, pool) || r.remaining() != 0u) return fail(error, "forms.bin", "text pool");
        form_pool_.assign(reinterpret_cast<const char*>(pool), pool_size);
        for (const FormEntry& e : forms_) {
            if (e.length == 0u || e.offset > pool_size || pool_size - e.offset < e.length ||
                !valid_utf8(pool + e.offset, e.length)) {
                return fail(error, "forms.bin", "invalid form text");
            }
        }
    }

    // ---- templates.bin ----
    {
        if (!unwrap_container(*templates, PackKind::Templates, payload, length, why)) return fail(error, "templates.bin", why.c_str());
        Reader r(payload, length);
        u32 count = 0u;
        if (!r.get32(count)) return fail(error, "templates.bin", "truncated");
        for (u32 i = 0u; i < count; ++i) {
            TemplateEntry e{};
            if (!r.get16(e.intent_id) || !r.get32(e.offset) || !r.get32(e.length)) return fail(error, "templates.bin", "truncated");
            if (common.intent(e.intent_id) == nullptr) return fail(error, "templates.bin", "unknown intent id");
            if (!templates_.empty() && e.intent_id <= templates_.back().intent_id) {
                return fail(error, "templates.bin", "entries not strictly sorted");
            }
            templates_.push_back(e);
        }
        u32 pool_size = 0u;
        const u8* pool = nullptr;
        if (!r.get32(pool_size) || !r.take(pool_size, pool) || r.remaining() != 0u) return fail(error, "templates.bin", "text pool");
        template_pool_.assign(reinterpret_cast<const char*>(pool), pool_size);
        std::vector<TemplatePiece> pieces;
        for (const TemplateEntry& e : templates_) {
            if (e.length == 0u || e.offset > pool_size || pool_size - e.offset < e.length ||
                !valid_utf8(pool + e.offset, e.length)) {
                return fail(error, "templates.bin", "invalid template text");
            }
            if (!parse_template(std::string_view(template_pool_).substr(e.offset, e.length), pieces)) {
                return fail(error, "templates.bin", "invalid template syntax");
            }
            for (const TemplatePiece& p : pieces) {
                if (p.placeholder && !builtin_number_form(p.text) && !has_form_name(p.text)) {
                    return fail(error, "templates.bin", "template uses an undefined form");
                }
            }
        }
    }
    return true;
}

const LexiconEntry* LanguagePack::lexicon_entries(u32 pattern, u32& count) const noexcept {
    if (pattern >= lexicon_ranges_.size()) {
        count = 0u;
        return nullptr;
    }
    count = lexicon_ranges_[pattern].second;
    return lexicon_items_.data() + lexicon_ranges_[pattern].first;
}

u32 LanguagePack::number_value(u32 pattern) const noexcept {
    return pattern < number_values_.size() ? number_values_[pattern] : 0u;
}

bool LanguagePack::has_form_name(std::string_view form_name) const noexcept {
    return std::find(form_names_.begin(), form_names_.end(), form_name) != form_names_.end();
}

bool LanguagePack::form(u16 concept_id, std::string_view form_name, std::string_view& text) const noexcept {
    const auto name = std::find(form_names_.begin(), form_names_.end(), form_name);
    if (name == form_names_.end()) return false;
    const u16 form_id = static_cast<u16>(name - form_names_.begin());
    const auto it = std::lower_bound(forms_.begin(), forms_.end(), std::make_pair(concept_id, form_id),
                                     [](const FormEntry& e, const std::pair<u16, u16>& key) {
                                         return e.concept_id < key.first ||
                                                (e.concept_id == key.first && e.form_id < key.second);
                                     });
    if (it == forms_.end() || it->concept_id != concept_id || it->form_id != form_id) return false;
    text = std::string_view(form_pool_).substr(it->offset, it->length);
    return true;
}

bool LanguagePack::template_text(u16 intent_id, std::string_view& text) const noexcept {
    const auto it = std::lower_bound(templates_.begin(), templates_.end(), intent_id,
                                     [](const TemplateEntry& e, u16 v) { return e.intent_id < v; });
    if (it == templates_.end() || it->intent_id != intent_id) return false;
    text = std::string_view(template_pool_).substr(it->offset, it->length);
    return true;
}

// ---------------------------------------------------------------------------
// Serialisers
// ---------------------------------------------------------------------------

std::vector<u8> serialize_concepts(const std::vector<ConceptInfo>& concepts) {
    std::vector<ConceptInfo> sorted = concepts;
    std::sort(sorted.begin(), sorted.end(), [](const ConceptInfo& a, const ConceptInfo& b) { return a.id < b.id; });
    Writer w;
    w.put32(static_cast<u32>(sorted.size()));
    for (const ConceptInfo& c : sorted) {
        w.put16(c.id);
        w.put8(c.slot);
        w.put8(static_cast<u32>(c.concept_class));
        w.put32(c.categories);
    }
    return wrap_container(PackKind::Concepts, w.bytes);
}

std::vector<u8> serialize_intents(const std::vector<IntentInfo>& intents) {
    std::vector<IntentInfo> sorted = intents;
    std::sort(sorted.begin(), sorted.end(), [](const IntentInfo& a, const IntentInfo& b) { return a.id < b.id; });
    Writer w;
    w.put32(static_cast<u32>(sorted.size()));
    for (const IntentInfo& i : sorted) {
        w.put16(i.id);
        w.put8(i.expected_slots);
        w.put8(i.required_slots);
        w.put8((i.head_implied ? 1u : 0u) | (i.is_alert ? 2u : 0u));
        w.put8(0u);
    }
    return wrap_container(PackKind::Intents, w.bytes);
}

std::vector<u8> serialize_lexicon(const AutomatonBuilder& automaton,
                                  const std::vector<std::vector<LexiconEntry>>& entries_by_pattern) {
    Writer w;
    automaton.serialize(w.bytes);
    w.put32(static_cast<u32>(entries_by_pattern.size()));
    u32 first = 0u;
    for (const auto& entries : entries_by_pattern) {
        w.put32(first);
        w.put32(static_cast<u32>(entries.size()));
        first += static_cast<u32>(entries.size());
    }
    w.put32(first);
    for (const auto& entries : entries_by_pattern) {
        for (const LexiconEntry& e : entries) {
            w.put16(e.concept_id);
            w.put8(static_cast<u32>(e.form_class));
            w.put8(static_cast<u32>(e.origin));
        }
    }
    return wrap_container(PackKind::Lexicon, w.bytes);
}

std::vector<u8> serialize_numbers(const AutomatonBuilder& automaton, const std::vector<u32>& value_by_pattern) {
    Writer w;
    automaton.serialize(w.bytes);
    w.put32(static_cast<u32>(value_by_pattern.size()));
    for (u32 v : value_by_pattern) w.put32(v);
    return wrap_container(PackKind::Numbers, w.bytes);
}

std::vector<u8> serialize_negations(const AutomatonBuilder& automaton) {
    Writer w;
    automaton.serialize(w.bytes);
    return wrap_container(PackKind::Negations, w.bytes);
}

std::vector<u8> serialize_forms(const std::vector<FormSource>& forms) {
    std::vector<std::string> names;
    for (const FormSource& f : forms) names.push_back(f.form_name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    struct Row {
        u16 concept_id;
        u16 form_id;
        const std::string* text;
    };
    std::vector<Row> rows;
    for (const FormSource& f : forms) {
        const u16 id = static_cast<u16>(std::find(names.begin(), names.end(), f.form_name) - names.begin());
        rows.push_back(Row{f.concept_id, id, &f.text});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.concept_id < b.concept_id || (a.concept_id == b.concept_id && a.form_id < b.form_id);
    });

    Writer w;
    w.put16(static_cast<u32>(names.size()));
    for (const std::string& n : names) {
        w.put8(static_cast<u32>(n.size()));
        w.raw(n);
    }
    w.put32(static_cast<u32>(rows.size()));
    std::string pool;
    for (const Row& row : rows) {
        w.put16(row.concept_id);
        w.put16(row.form_id);
        w.put32(static_cast<u32>(pool.size()));
        w.put32(static_cast<u32>(row.text->size()));
        pool += *row.text;
    }
    w.put32(static_cast<u32>(pool.size()));
    w.raw(pool);
    return wrap_container(PackKind::Forms, w.bytes);
}

std::vector<u8> serialize_templates(const std::vector<TemplateSource>& templates) {
    std::vector<const TemplateSource*> rows;
    for (const TemplateSource& t : templates) rows.push_back(&t);
    std::sort(rows.begin(), rows.end(), [](const TemplateSource* a, const TemplateSource* b) { return a->intent_id < b->intent_id; });
    Writer w;
    w.put32(static_cast<u32>(rows.size()));
    std::string pool;
    for (const TemplateSource* t : rows) {
        w.put16(t->intent_id);
        w.put32(static_cast<u32>(pool.size()));
        w.put32(static_cast<u32>(t->text.size()));
        pool += t->text;
    }
    w.put32(static_cast<u32>(pool.size()));
    w.raw(pool);
    return wrap_container(PackKind::Templates, w.bytes);
}

std::vector<u8> serialize_patterns(const std::vector<ValuePattern>& patterns) {
    Writer w;
    std::string pool;
    w.put32(static_cast<u32>(patterns.size()));
    for (const ValuePattern& p : patterns) {
        w.put8(p.slot);
        w.put8(static_cast<u32>(p.elements.size()));
        w.put32(static_cast<u32>(p.a));
        w.put32(static_cast<u32>(p.b));
        w.put32(static_cast<u32>(p.c));
        w.put32(p.min);
        w.put32(p.max);
        for (const PatternElement& e : p.elements) {
            w.put8(static_cast<u32>(e.kind));
            if (e.kind == PatternElementKind::Word) {
                w.put32(static_cast<u32>(pool.size()));
                w.put32(static_cast<u32>(e.word.size()));
                pool += e.word;
            }
        }
    }
    w.put32(static_cast<u32>(pool.size()));
    w.raw(pool);
    return wrap_container(PackKind::Patterns, w.bytes);
}

}  // namespace itantra
