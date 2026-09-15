#include "lang/json.h"

#include <cstring>

namespace itantra {

namespace {

constexpr u32 kMaxDepth = 64u;

class Parser {
public:
    Parser(const u8* data, std::size_t length) : p_(data), n_(length) {}

    bool parse(JsonValue& out, std::string& error) {
        skip_space();
        if (!value(out, 0u)) {
            error = error_.empty() ? "invalid JSON" : error_;
            error += " at byte " + std::to_string(i_);
            return false;
        }
        skip_space();
        if (i_ != n_) {
            error = "trailing content at byte " + std::to_string(i_);
            return false;
        }
        return true;
    }

private:
    bool fail(const char* why) {
        if (error_.empty()) error_ = why;
        return false;
    }

    bool at_end() const { return i_ >= n_; }
    u8 peek() const { return at_end() ? u8{0} : p_[i_]; }

    void skip_space() {
        while (!at_end() && (p_[i_] == ' ' || p_[i_] == '\t' || p_[i_] == '\n' || p_[i_] == '\r')) ++i_;
    }

    bool literal(const char* word) {
        const std::size_t len = std::strlen(word);
        if (n_ - i_ < len || std::memcmp(p_ + i_, word, len) != 0) return fail("unexpected token");
        i_ += len;
        return true;
    }

    bool value(JsonValue& out, u32 depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        out = JsonValue{};
        switch (peek()) {
            case '{': return object(out, depth);
            case '[': return array(out, depth);
            case '"': out.kind = JsonValue::Kind::String; return string(out.string);
            case 't': out.kind = JsonValue::Kind::Bool; out.boolean = true; return literal("true");
            case 'f': out.kind = JsonValue::Kind::Bool; out.boolean = false; return literal("false");
            case 'n': out.kind = JsonValue::Kind::Null; return literal("null");
            default: return integer(out);
        }
    }

    bool object(JsonValue& out, u32 depth) {
        out.kind = JsonValue::Kind::Object;
        ++i_;
        skip_space();
        if (peek() == '}') { ++i_; return true; }
        for (;;) {
            skip_space();
            if (peek() != '"') return fail("expected object key");
            std::string key;
            if (!string(key)) return false;
            for (const std::string& k : out.keys) {
                if (k == key) return fail("duplicate object key");
            }
            skip_space();
            if (peek() != ':') return fail("expected ':'");
            ++i_;
            skip_space();
            JsonValue v;
            if (!value(v, depth + 1u)) return false;
            out.keys.push_back(key);
            out.items.push_back(std::move(v));
            skip_space();
            if (peek() == ',') { ++i_; continue; }
            if (peek() == '}') { ++i_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool array(JsonValue& out, u32 depth) {
        out.kind = JsonValue::Kind::Array;
        ++i_;
        skip_space();
        if (peek() == ']') { ++i_; return true; }
        for (;;) {
            skip_space();
            JsonValue v;
            if (!value(v, depth + 1u)) return false;
            out.items.push_back(std::move(v));
            skip_space();
            if (peek() == ',') { ++i_; continue; }
            if (peek() == ']') { ++i_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool integer(JsonValue& out) {
        out.kind = JsonValue::Kind::Integer;
        bool negative = false;
        if (peek() == '-') { negative = true; ++i_; }
        if (at_end() || p_[i_] < '0' || p_[i_] > '9') return fail("expected a value");
        if (p_[i_] == '0' && i_ + 1u < n_ && p_[i_ + 1u] >= '0' && p_[i_ + 1u] <= '9') {
            return fail("leading zero");
        }
        u64 magnitude = 0u;
        while (!at_end() && p_[i_] >= '0' && p_[i_] <= '9') {
            const u64 digit = static_cast<u64>(p_[i_] - '0');
            if (magnitude > (u64{1} << 62) / 10u) return fail("integer out of range");
            magnitude = magnitude * 10u + digit;
            ++i_;
        }
        if (!at_end() && (p_[i_] == '.' || p_[i_] == 'e' || p_[i_] == 'E')) {
            return fail("non-integer number (pack data is integer-only)");
        }
        out.integer = negative ? -static_cast<i64>(magnitude) : static_cast<i64>(magnitude);
        return true;
    }

    static void put_utf8(std::string& s, u32 cp) {
        if (cp < 0x80u) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            s.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            s.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            s.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    bool hex4(u32& out) {
        if (n_ - i_ < 4u) return fail("short \\u escape");
        out = 0u;
        for (u32 k = 0u; k < 4u; ++k) {
            const u8 c = p_[i_++];
            u32 d = 0u;
            if (c >= '0' && c <= '9') d = static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<u32>(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F') d = static_cast<u32>(c - 'A') + 10u;
            else return fail("bad \\u escape");
            out = (out << 4) | d;
        }
        return true;
    }

    // One UTF-8 encoded codepoint starting at i_, validated (no overlongs,
    // no surrogates, <= U+10FFFF).
    bool utf8(std::string& s) {
        const u8 b0 = p_[i_];
        u32 need = 0u;
        u32 cp = 0u;
        u32 min = 0u;
        if (b0 >= 0xC2u && b0 <= 0xDFu) { need = 1u; cp = b0 & 0x1Fu; min = 0x80u; }
        else if (b0 >= 0xE0u && b0 <= 0xEFu) { need = 2u; cp = b0 & 0x0Fu; min = 0x800u; }
        else if (b0 >= 0xF0u && b0 <= 0xF4u) { need = 3u; cp = b0 & 0x07u; min = 0x10000u; }
        else return fail("invalid UTF-8");
        if (n_ - i_ < need + 1u) return fail("truncated UTF-8");
        for (u32 k = 1u; k <= need; ++k) {
            const u8 b = p_[i_ + k];
            if ((b & 0xC0u) != 0x80u) return fail("invalid UTF-8");
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return fail("invalid UTF-8");
        s.append(reinterpret_cast<const char*>(p_ + i_), need + 1u);
        i_ += need + 1u;
        return true;
    }

    bool string(std::string& out) {
        ++i_;   // opening quote
        for (;;) {
            if (at_end()) return fail("unterminated string");
            const u8 c = p_[i_];
            if (c == '"') { ++i_; return true; }
            if (c < 0x20u) return fail("control character in string");
            if (c == '\\') {
                ++i_;
                if (at_end()) return fail("unterminated escape");
                const u8 e = p_[i_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        u32 cp = 0u;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800u && cp <= 0xDBFFu) {
                            if (n_ - i_ < 2u || p_[i_] != '\\' || p_[i_ + 1u] != 'u') return fail("lone surrogate");
                            i_ += 2u;
                            u32 lo = 0u;
                            if (!hex4(lo)) return false;
                            if (lo < 0xDC00u || lo > 0xDFFFu) return fail("lone surrogate");
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                            return fail("lone surrogate");
                        }
                        put_utf8(out, cp);
                        break;
                    }
                    default: return fail("bad escape");
                }
                continue;
            }
            if (c < 0x80u) {
                out.push_back(static_cast<char>(c));
                ++i_;
                continue;
            }
            if (!utf8(out)) return false;
        }
    }

    const u8*   p_;
    std::size_t n_;
    std::size_t i_ = 0u;
    std::string error_;
};

}  // namespace

const JsonValue* JsonValue::member(const char* key) const noexcept {
    if (kind != Kind::Object) return nullptr;
    for (std::size_t k = 0u; k < keys.size(); ++k) {
        if (keys[k] == key) return &items[k];
    }
    return nullptr;
}

bool parse_json(const u8* data, std::size_t length, JsonValue& out, std::string& error) {
    out = JsonValue{};
    if (data == nullptr && length != 0u) {
        error = "null input";
        return false;
    }
    Parser parser(data, length);
    return parser.parse(out, error);
}

}  // namespace itantra
