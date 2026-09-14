#include "lang/languages.h"

namespace itantra {

namespace {

// language-layer-spec.md header: "Hindi, Gujarati, Marathi, Kannada,
// Malayalam, Tamil, Telugu, Odia, Bengali, English". This table is the ONLY
// place a language code appears in native/src/lang (checked by
// conformance.c20_c27).
//
// Its order is also the LangId wire mapping (languages.h): entry i is LangId
// i + 1. Append only.
constexpr SupportedLanguage kLanguages[] = {
    {"hi", "Hindi"},   {"gu", "Gujarati"}, {"mr", "Marathi"}, {"kn", "Kannada"}, {"ml", "Malayalam"},
    {"ta", "Tamil"},   {"te", "Telugu"},   {"or", "Odia"},    {"bn", "Bengali"}, {"en", "English"},
};

constexpr std::size_t kLanguageCount = sizeof(kLanguages) / sizeof(kLanguages[0]);

}  // namespace

const SupportedLanguage* supported_languages(std::size_t& count) noexcept {
    count = kLanguageCount;
    return kLanguages;
}

bool is_supported_language(const std::string& code) noexcept {
    for (const SupportedLanguage& l : kLanguages) {
        if (code == l.code) return true;
    }
    return false;
}

bool lang_id_of(const std::string& code, u8& id) noexcept {
    for (std::size_t i = 0u; i < kLanguageCount; ++i) {
        if (code == kLanguages[i].code) {
            id = static_cast<u8>(i + 1u);
            return true;
        }
    }
    return false;
}

const SupportedLanguage* language_of_id(u8 id) noexcept {
    if (id == kLangIdUnassigned || id > kLanguageCount) return nullptr;
    return &kLanguages[id - 1u];
}

}  // namespace itantra
