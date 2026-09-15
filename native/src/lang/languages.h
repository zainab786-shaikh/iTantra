#pragma once

// The ten supported languages — language-layer-spec header, §3.
//
// Language is a USER SETTING, never detected (§3, §17.2). This list only says
// which pack language codes are accepted; nothing in the language layer
// branches on a language (L9). Codes are ISO 639-1.
//
// ---------------------------------------------------------------------------
// LangId — the 4-bit `language` value of Tier 2 metadata (packet §3.2).
// WIRE CONTRACT, frozen in Phase 8 (recorded in the packet and language spec
// implementation resolutions):
//
//   0          unassigned — names no language; never a valid sender language
//   1 … 10     hi, gu, mr, kn, ml, ta, te, or, bn, en — the order the language
//              spec lists them (this table's order)
//   11 … 15    reserved for languages added later
//
// Append-only, like concept IDs (language §7.4): a value is never reused or
// renumbered. A receiver holding an older table decodes an unknown value as "no
// language", never as a different language.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>

#include "common/types.h"

namespace itantra {

struct SupportedLanguage {
    const char* code;   // ISO 639-1
    const char* name;   // English name, for logs and reports only
};

// All ten, in the order the language spec lists them.
const SupportedLanguage* supported_languages(std::size_t& count) noexcept;

bool is_supported_language(const std::string& code) noexcept;

constexpr u8 kLangIdUnassigned = 0u;
constexpr u8 kLangIdMax        = 15u;   // 4 bits

// The LangId of a supported language code; false (id unchanged) otherwise.
bool lang_id_of(const std::string& code, u8& id) noexcept;

// The language a LangId names; null for 0, reserved values and values above 15.
const SupportedLanguage* language_of_id(u8 id) noexcept;

}  // namespace itantra
