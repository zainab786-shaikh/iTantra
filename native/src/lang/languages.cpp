#include "lang/languages.h"

namespace itantra {

namespace {

// language-layer-spec.md header: "Hindi, Gujarati, Marathi, Kannada,
// Malayalam, Tamil, Telugu, Odia, Bengali, English". This table is the ONLY
// place a language code appears in native/src/lang (checked by
// conformance.c20_c27).
constexpr SupportedLanguage kLanguages[] = {
    {"hi", "Hindi"},   {"gu", "Gujarati"}, {"mr", "Marathi"}, {"kn", "Kannada"}, {"ml", "Malayalam"},
    {"ta", "Tamil"},   {"te", "Telugu"},   {"or", "Odia"},    {"bn", "Bengali"}, {"en", "English"},
};

}  // namespace

const SupportedLanguage* supported_languages(std::size_t& count) noexcept {
    count = sizeof(kLanguages) / sizeof(kLanguages[0]);
    return kLanguages;
}

bool is_supported_language(const std::string& code) noexcept {
    for (const SupportedLanguage& l : kLanguages) {
        if (code == l.code) return true;
    }
    return false;
}

}  // namespace itantra
