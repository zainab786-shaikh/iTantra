#pragma once

// Tier 2 decode — tier §6.5, §10; receiver §3 ⑦ ⑧ ⑨, §5, §6.2, §7.2.
//
//   parse_metadata                  the tier must be 2
//   hash_present = 1                the receiver's PRE-message context is required
//                                   and its wire hash must equal context_hash.
//                                   Otherwise NOTHING is decoded (ContextMismatch):
//                                   a boost from a different context is a
//                                   different table, and the arithmetic decode
//                                   would desynchronise into garbage (§6.5,
//                                   receiver §6.2). Then the boost is built from
//                                   that context.
//   hash_present = 0                the context is not read at all
//   decode symbol_count tokens      then concatenate their bytes (receiver §5)
//
// The text is in the SENDER's language: metadata.language is the value the
// sender wrote, never the receiver's setting (receiver §7.2). It is the exact
// bytes the sender encoded — no normalisation, no translation.
//
// Input is the PLAINTEXT native payload (after open_payload(), packet/parse.h).
// Committing context from the text — and skipping that across languages — is
// the receiver pipeline's job (receiver §8, §8.3), not this function's.

#include <string>

#include "context/context.h"
#include "packet/metadata.h"
#include "tier2/encode.h"
#include "tier2/tables.h"

namespace itantra {

struct Tier2Decoded {
    Metadata    metadata;   // filled whenever the metadata parsed, whatever the status
    std::string text;       // Ok only
};

// `context` is the receiver's pre-message context; read only when hash_present = 1.
Tier2Status tier2_decode(const Tier2Tables& tables, const u8* bytes, u32 length, const Context* context,
                         Tier2Decoded& out);

}  // namespace itantra
