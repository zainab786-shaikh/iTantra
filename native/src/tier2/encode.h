#pragma once

// Tier 2 encode — tier §6; packet §2, §5.
//
//   clause bytes → tokenize (tier2/subword.h)
//                → model: static table [+ boost] (tier2/ngram.h, tier2/boost.h)
//                → assemble() (packet/assemble.h) — the frozen coder and format
//
// DECIDED in Phase 7 (recorded in the tier spec's implementation resolutions):
//
//   Input text     (tier §6.1; language spec open item "original clause text or
//                  its normalised form"). Tier 2 codes the clause's ORIGINAL
//                  input bytes, never a normalised form. Language spec §6.1
//                  steps 1–6 are not reversible: NFC and whitespace collapse
//                  change bytes, digit unification turns native digits into
//                  ASCII, case folding and punctuation stripping discard
//                  information. Tier 2 must return the sentence exactly (tier
//                  §1.1; contract C-05 "byte-for-byte, including whitespace and
//                  script"). tier2_clause_inputs() below cuts the utterance at
//                  the clause starts the language layer found, so each clause's
//                  Tier 2 input keeps its own trailing punctuation and spacing,
//                  and all of them concatenate back to the utterance exactly.
//
//   symbol_count   (packet §3.5; deferred from Phase 3.) The token count.
//                  Kneser-Ney codes exactly one symbol per token — no escape
//                  symbols, no end marker — so symbol_count is the length of the
//                  sequence tokenize() produced.
//
//   Language       message.language is the sender's LangId, written unchanged:
//                  Tier 2 stays in the sender's language and is never translated
//                  (tier §1.1, language §10.2). Which LangId value names which
//                  language is NOT decided here (packet spec open item).
//
//   Boost          message.boost_context non-null: boosted — the boost is built
//                  from that context, hash_present = 1 and context_hash =
//                  wire_context_hash(context_hash(*boost_context)), the same
//                  pre-message state. Null: unboosted, hash_present = 0,
//                  context_hash = 0, no context read (tier §6.5).
//
// Clause boundary (§6.6, T4). Every prepare() starts from the static tables: the
// first token's history is BOS and nothing from an earlier clause is kept.
//
// Totality (§6.7). tokenize() cannot fail and every token has p > 0, so the one
// refusal is packet §5's ASM_TOO_LONG: more than kMaxSymbolCount (2078) tokens,
// which the caller must split. A token covers at least one byte, so a clause of
// at most 2078 bytes always encodes.

#include <cstddef>
#include <memory>
#include <vector>

#include "context/context.h"
#include "lang/extract.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "tier2/boost.h"
#include "tier2/ngram.h"
#include "tier2/tables.h"

namespace itantra {

enum class Tier2Status : u8 {
    Ok,
    TooLong,           // encode: more than kMaxSymbolCount tokens (ASM_TOO_LONG); caller must split
    InvalidArgument,   // tables not loaded, null text with a length, LangId above 15
    CoderFailure,      // a token with p == 0 — impossible with loaded tables (§6.7)
    Malformed,         // decode: metadata truncated, or tier 00 / 11
    NotTier2,          // decode: a Tier 1 payload
    ContextRequired,   // decode: hash_present = 1 but no context supplied
    ContextMismatch,   // decode: hash_present = 1 and the receiver's pre-message hash
                       // differs. Nothing decoded; request an unboosted resend (§6.5)
    DecodeFailure,     // decode: impossible with loaded tables (receiver §3⑨)
};

struct Tier2Message {
    u8             seq           = 0u;         // low 8 bits of the sender's counter
    Priority       priority      = Priority::Normal;
    LangId         language      = 0u;         // the SENDER's language
    const Context* boost_context = nullptr;    // PRE-message context, or null for unboosted
};

// One clause prepared for assemble() or assemble_sealed().
class Tier2Clause {
public:
    explicit Tier2Clause(const Tier2Tables& tables) noexcept : tables_(tables) {}
    Tier2Clause(const Tier2Clause&) = delete;
    Tier2Clause& operator=(const Tier2Clause&) = delete;

    // Tokenizes text[0, length) and builds this clause's model, discarding
    // everything from any previous call. `text` may be null when length is 0.
    Tier2Status prepare(const u8* text, std::size_t length, const Tier2Message& message);

    // After prepare() returned Ok, until the next prepare() or destruction. The
    // input points into this object.
    const AssemblyInput&       assembly_input() const noexcept { return input_; }
    const std::vector<Symbol>& tokens() const noexcept { return tokens_; }

private:
    const Tier2Tables&          tables_;
    std::vector<Symbol>         tokens_;
    ContextBoost                boost_;
    std::unique_ptr<Tier2Model> model_;
    AssemblyInput               input_;
};

// prepare() then assemble(): the plaintext native payload (packet §2).
Tier2Status tier2_encode(const Tier2Tables& tables, const u8* text, std::size_t length, const Tier2Message& message,
                         NativePayload& out);

// The Tier 2 input of each clause, as byte ranges of the original utterance:
// clause k runs from its own start (0 for the first clause) to the next clause's
// start (the end of the input for the last). The ranges are contiguous and cover
// the whole input, so no byte the speaker produced is dropped at a clause
// boundary. An utterance in which the language layer found no clause yields no
// range.
std::vector<SourceSpan> tier2_clause_inputs(const UtteranceExtraction& extraction, std::size_t input_length);

}  // namespace itantra
