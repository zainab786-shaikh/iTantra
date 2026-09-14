#pragma once

// The ten supported languages — language-layer-spec header, §3.
//
// Language is a USER SETTING, never detected (§3, §17.2). This list only says
// which pack language codes are accepted; nothing in the language layer
// branches on a language (L9). Codes are ISO 639-1.
//
// NOT decided here: the 4-bit `language` value carried in Tier 2 metadata
// (packet §3.2). That mapping is a wire contract and remains open.

#include <cstddef>
#include <string>

namespace itantra {

struct SupportedLanguage {
    const char* code;   // ISO 639-1
    const char* name;   // English name, for logs and reports only
};

// All ten, in the order the language spec lists them.
const SupportedLanguage* supported_languages(std::size_t& count) noexcept;

bool is_supported_language(const std::string& code) noexcept;

}  // namespace itantra
