#pragma once

// Language data — language-layer-spec §4, §7, §11.1, Appendix B.
//
//   common/                     shared, no language content (§4.1)
//     concepts.bin              concept ID → slot type, class, category
//     intents.bin               intent ID  → expected slots, head_implied, is_alert
//     schema_version
//
//   lang/<code>/                one per language; ONE loads at a time (§4.4)
//     meta.json                 language code, pack version, script, TTS voice,
//                               STT confidence threshold, read-back bars
//     normalize.json            digit maps and script normalisation rules (lang/normalize.h)
//     lexicon.bin               surface forms → concept IDs (match automaton)
//     forms.bin                 concept ID → output forms per slot position
//     templates.bin             intent ID → sentence template
//     numbers.bin               number words → numeric values
//     patterns.bin              time / quantity scanners
//     negations.bin             negation words (match automaton) — added in Phase 8
//
// Adding or replacing a language is data, never code (§ decisions #10, L9): the
// loader reads any pack in this layout, and nothing downstream knows which
// language it holds.
//
// Every .bin file is one container (big-endian, never a memcpy'd struct):
//
//   magic "ITLP" · u16 container version (1) · u16 kind · u32 payload length ·
//   payload · u32 CRC-32/ISO-HDLC of everything before it
//
// Buffers are owned by the pack; the lexicon, number and negation automata are
// used in place (lang/lexicon.h), so the same code serves bytes read from a
// file, an mmap'd region or an Android asset.
//
// Phase 8 additions (language spec implementation resolutions):
//   negations.bin      the "where negation words live" open item. Negation is
//                      per clause (tier §5.1); a negation word is neither a
//                      concept nor unmatched text (lang/extract.h).
//   meta.json          readback_normal, readback_critical: the per-language
//                      read-back bars (tier §5.8, language §11.1), in per-mille
//                      similarity, 0 … 1000, critical strictly above normal
//                      (§5.8 "the bar is higher for CRITICAL"). Sender-only.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/types.h"
#include "lang/lexicon.h"
#include "lang/normalize.h"

namespace itantra {

constexpr u16 kPackContainerVersion = 1u;

enum class PackKind : u16 {
    Concepts  = 1,
    Intents   = 2,
    Lexicon   = 3,
    Forms     = 4,
    Templates = 5,
    Numbers   = 6,
    Patterns  = 7,
    // Tier 2 tables (tier2/tables.h), added in Phase 7. Same container.
    Subwords  = 8,
    Ngram     = 9,
    Boost     = 10,
    // Added in Phase 8.
    Negations = 11,   // lang/<code>/negations.bin
    Rules     = 12,   // sender/rules.bin — SENDER-ONLY (tier1/rules.h, tier §3.4)
};

// ---------------------------------------------------------------------------
// Shared contract (§4.1, §7.2)
// ---------------------------------------------------------------------------

// language §7.2 — sender-only; the receiver never reads class or category (tier §3.4).
enum class ConceptClass : u8 {
    Action   = 0,
    Event    = 1,
    State    = 2,
    Entity   = 3,
    Modifier = 4,
};

// A concept that fills no context slot (e.g. an ACTION head).
constexpr u8 kNoSlot = 0xFFu;

// Bit s set = SlotId s. Concept slot types are ACTOR … STATE; LAST_REF is a
// pointer, not a concept slot.
constexpr u8  kConceptSlotMask  = 0x7Fu;
constexpr u32 kConceptSlotCount = 7u;

struct ConceptInfo {
    u16          id;             // != 0 (0 = empty, context Appendix B)
    u8           slot;           // SlotId ACTOR … STATE, or kNoSlot
    ConceptClass concept_class;
    u32          categories;     // category bit mask
};

struct IntentInfo {
    u16  id;                     // != 0 (INTENT_NONE)
    u8   expected_slots;         // slot bit mask
    u8   required_slots;         // subset of expected_slots
    bool head_implied;           // tier §5.5
    bool is_alert;               // language §11.2 — sets CRITICAL
};

enum class FormClass : u8 { Base = 0, Inflected = 1 };
enum class Origin : u8 { Native = 0, Loanword = 1, SttVariant = 2 };

struct LexiconEntry {            // §4.3
    u16       concept_id;
    FormClass form_class;
    Origin    origin;
};

enum class PatternElementKind : u8 { Number = 0, Word = 1 };

struct PatternElement {
    PatternElementKind kind;
    std::string        word;     // Word only; normalised (normalize_surface)
};

// A typed-value scanner (§4.2 patterns.bin, §8 "Detect typed values"). Tried
// in file order at each position; the first that matches consumes its items.
// value = a · first number + b · second number + c, accepted in [min, max].
struct ValuePattern {
    u8                          slot;
    std::vector<PatternElement> elements;
    i32                         a;
    i32                         b;
    i32                         c;
    u32                         min;     // >= 1: 0 means empty in context
    u32                         max;     // <= 65535: a slot value is 16 bits
};

// Slot names used by templates and pack sources: ACTOR … STATE.
bool        slot_from_name(std::string_view name, u8& slot) noexcept;
const char* slot_name(u8 slot) noexcept;

// Template syntax: literal text with {SLOT:form} placeholders; '{' and '}'
// appear nowhere else. SLOT is ACTOR … STATE. form is a forms.bin form name
// for concepts, or "digits" / "native" for numbers; a literal is inserted as
// spoken whatever the form says.
struct TemplatePiece {
    bool             placeholder;
    std::string_view text;       // literal text, or the placeholder's form
    u8               slot;       // placeholder only
};
bool parse_template(std::string_view text, std::vector<TemplatePiece>& out);

// ---------------------------------------------------------------------------
// Files and containers
// ---------------------------------------------------------------------------

using PackFile  = std::pair<std::string, std::vector<u8>>;   // file name, bytes
using PackFiles = std::vector<PackFile>;

bool read_pack_directory(const std::string& directory, const std::vector<std::string>& names, PackFiles& out,
                         std::string& error);

u32             crc32_iso_hdlc(const u8* data, std::size_t length) noexcept;
std::vector<u8> wrap_container(PackKind kind, const std::vector<u8>& payload);
bool            unwrap_container(const std::vector<u8>& file, PackKind kind, const u8*& payload,
                                 std::size_t& payload_length, std::string& error);

// ---------------------------------------------------------------------------
// Packs
// ---------------------------------------------------------------------------

class CommonPack {
public:
    static const std::vector<std::string>& file_names();

    bool load(PackFiles files, std::string& error);

    u32                             schema_version() const noexcept { return schema_version_; }
    const ConceptInfo*              concept_info(u16 id) const noexcept;
    const IntentInfo*               intent(u16 id) const noexcept;
    const std::vector<ConceptInfo>& concepts() const noexcept { return concepts_; }
    const std::vector<IntentInfo>&  intents() const noexcept { return intents_; }

private:
    std::vector<ConceptInfo> concepts_;
    std::vector<IntentInfo>  intents_;
    u32                      schema_version_ = 0u;
};

// Read-back bars are per-mille similarity (tier §5.8).
constexpr u32 kReadbackScale = 1000u;

class LanguagePack {
public:
    // Movable, not copyable: the automata point into files_, whose buffers
    // survive a move but not a copy.
    LanguagePack() = default;
    LanguagePack(const LanguagePack&) = delete;
    LanguagePack& operator=(const LanguagePack&) = delete;
    LanguagePack(LanguagePack&&) = default;
    LanguagePack& operator=(LanguagePack&&) = default;

    static const std::vector<std::string>& file_names();

    // Loads and validates every file against `common`: every concept and
    // intent referenced must exist there.
    bool load(PackFiles files, const CommonPack& common, std::string& error);

    const std::string&    language() const noexcept { return language_; }
    u32                   pack_version() const noexcept { return pack_version_; }
    const std::string&    script() const noexcept { return script_; }
    const std::string&    tts_voice() const noexcept { return tts_voice_; }
    i64                   stt_confidence_threshold() const noexcept { return stt_threshold_; }
    u32                   readback_normal() const noexcept { return readback_normal_; }
    u32                   readback_critical() const noexcept { return readback_critical_; }
    const NormalizeRules& rules() const noexcept { return rules_; }

    const Automaton&    lexicon() const noexcept { return lexicon_; }
    const LexiconEntry* lexicon_entries(u32 pattern, u32& count) const noexcept;

    const Automaton& numbers() const noexcept { return numbers_; }
    u32              number_value(u32 pattern) const noexcept;

    const Automaton& negations() const noexcept { return negations_; }

    const std::vector<ValuePattern>& patterns() const noexcept { return patterns_; }

    bool form(u16 concept_id, std::string_view form_name, std::string_view& text) const noexcept;
    bool has_form_name(std::string_view form_name) const noexcept;
    bool template_text(u16 intent_id, std::string_view& text) const noexcept;

private:
    struct FormEntry {
        u16 concept_id;
        u16 form_id;
        u32 offset;
        u32 length;
    };
    struct TemplateEntry {
        u16 intent_id;
        u32 offset;
        u32 length;
    };

    PackFiles                               files_;
    std::string                             language_;
    u32                                     pack_version_ = 0u;
    std::string                             script_;
    std::string                             tts_voice_;
    i64                                     stt_threshold_ = 0;
    u32                                     readback_normal_ = 0u;
    u32                                     readback_critical_ = 0u;
    NormalizeRules                          rules_;
    Automaton                               lexicon_;
    std::vector<std::pair<u32, u32>>        lexicon_ranges_;   // per pattern: first entry, count
    std::vector<LexiconEntry>               lexicon_items_;
    Automaton                               numbers_;
    std::vector<u32>                        number_values_;
    Automaton                               negations_;
    std::vector<ValuePattern>               patterns_;
    std::vector<std::string>                form_names_;
    std::vector<FormEntry>                  forms_;
    std::string                             form_pool_;
    std::vector<TemplateEntry>              templates_;
    std::string                             template_pool_;
};

// ---------------------------------------------------------------------------
// Serialisers — used by the pack compiler (native/tools/packc.cpp). Each
// returns a complete container file.
// ---------------------------------------------------------------------------

struct FormSource {
    u16         concept_id;
    std::string form_name;
    std::string text;
};

struct TemplateSource {
    u16         intent_id;
    std::string text;
};

std::vector<u8> serialize_concepts(const std::vector<ConceptInfo>& concepts);
std::vector<u8> serialize_intents(const std::vector<IntentInfo>& intents);
std::vector<u8> serialize_lexicon(const AutomatonBuilder& automaton,
                                  const std::vector<std::vector<LexiconEntry>>& entries_by_pattern);
std::vector<u8> serialize_numbers(const AutomatonBuilder& automaton, const std::vector<u32>& value_by_pattern);
std::vector<u8> serialize_negations(const AutomatonBuilder& automaton);
std::vector<u8> serialize_forms(const std::vector<FormSource>& forms);
std::vector<u8> serialize_templates(const std::vector<TemplateSource>& templates);
std::vector<u8> serialize_patterns(const std::vector<ValuePattern>& patterns);

}  // namespace itantra
