#pragma once

// Minimal strict JSON reader for language pack metadata — language spec §4.2
// (meta.json, normalize.json).
//
// Deliberately small and strict, because a pack is a shared contract and
// both phones must read it identically:
//
//   - objects, arrays, strings, true, false, null
//   - numbers are INTEGERS only: no fraction, no exponent (tier §3.2 / C-04:
//     no floating point anywhere a threshold could come from)
//   - strings are UTF-8; \uXXXX escapes (with surrogate pairs) are decoded;
//     invalid UTF-8, lone surrogates and raw control characters are rejected
//   - duplicate object keys are rejected
//   - nothing but whitespace may follow the top-level value
//
// Anything else is an error, never a best-effort guess.

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.h"

namespace itantra {

struct JsonValue {
    enum class Kind : u8 { Null, Bool, Integer, String, Array, Object };

    Kind                   kind    = Kind::Null;
    bool                   boolean = false;
    i64                    integer = 0;
    std::string            string;          // UTF-8
    std::vector<JsonValue> items;           // Array elements, or Object member values
    std::vector<std::string> keys;          // Object member names, parallel to items

    // Object member by key; null if absent or not an object.
    const JsonValue* member(const char* key) const noexcept;
};

// Parses `length` bytes. On failure returns false with a message naming the
// byte offset.
bool parse_json(const u8* data, std::size_t length, JsonValue& out, std::string& error);

}  // namespace itantra
