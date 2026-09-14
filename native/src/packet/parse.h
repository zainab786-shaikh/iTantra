#pragma once

// Payload parsing — the inverse of assemble(). packet §3.4, §3.5; receiver
// §3③, §3⑨.
//
// Two stages, so the receiver can put its gates between them (receiver §2):
//
//   parse_metadata   fixed widths, zero coder state            receiver ③ ④
//   (receiver: seq gap, replay, context hash gates)            receiver ⑤ ⑥ ⑦
//   (receiver: select the model from the tier)                receiver ⑧
//   decode_symbols   exactly symbol_count symbols               receiver ⑨
//
// parse() chains them for callers with no gates in between.
//
// symbol_count is the only stopping condition (packet §3.5). Nothing after
// the last decoded symbol is examined — not the flush's continuation, not the
// padding (packet §4.1 "Never read by the parser"), not trailing bytes.
//
// Input is the PLAINTEXT payload; decryption (Phase 4) happens before this.

#include "coder/model.h"
#include "packet/assemble.h"
#include "packet/metadata.h"
#include "common/types.h"

namespace itantra {

enum class ParseStatus : u8 {
    Ok,
    Truncated,          // metadata runs past the end of the buffer
    BadTier,            // tier 00 or 11
    NegationMismatch,   // Tier 1 negation copies disagree: reject (receiver §3④)
    NoModel,            // no model for this metadata
    DecodeFailure,      // the model broke its contract (coder asserts in debug)
    InvalidArgument,    // null pointers, or more symbols than `capacity`
    AuthenticationFailed,   // AEAD tag check failed: discard, nothing parsed (receiver §3②)
    InvalidCounter,     // counter 0, above kMaxCounter, or invalid direction
    SeqMismatch,        // authenticated, but seq is not the counter's low 8 bits
};

// Reads the metadata block from the start of `bytes`. On Ok,
// `payload_bit_offset` is the first coder bit. NegationMismatch fills every
// other field of `metadata` but the packet must be rejected, not decoded.
ParseStatus parse_metadata(const u8* bytes, u32 length, Metadata& metadata,
                           u32& payload_bit_offset) noexcept;

// Decodes `symbol_count` symbols starting at `payload_bit_offset` into
// out[0 .. symbol_count). The model is asked for position i with out[0 .. i)
// already decoded, exactly as the encoder asked with the input symbols.
ParseStatus decode_symbols(const u8* bytes, u32 length, u32 payload_bit_offset,
                           u16 symbol_count, const PayloadModel& model, Symbol* out,
                           u32 capacity) noexcept;

// The tier selects the model (packet §3.4). Returns null when there is none.
class ModelSelector {
public:
    virtual ~ModelSelector() = default;
    virtual const PayloadModel* select(const Metadata& metadata) const noexcept = 0;
};

struct ParsedPayload {
    Metadata metadata;
    u16      metadata_bits;
    Symbol   symbols[kMaxSymbolCount];
};

ParseStatus parse(const u8* bytes, u32 length, const ModelSelector& selector,
                  ParsedPayload& out) noexcept;

// ---------------------------------------------------------------------------
// Decrypt before parse — packet §6.1, receiver §2 ② → ③, §11 R1 (Phase 4)
// ---------------------------------------------------------------------------
//
// Authentication precedes everything: no metadata field is read and no symbol
// decoded from a packet that fails it (receiver §2.1).
//
// `direction` is the SENDER's direction and `counter` the sender's wide
// counter for this packet; together they select the nonce (crypto/nonce.h).
//
// SPEC GAP — not resolved here. The receiver reconstructs the counter from
// seq (packet §3.6), but seq is inside the ciphertext (§6.2) and the counter
// is needed to decrypt (§6.5, receiver §3②). How the receiver obtains the
// counter before decrypting belongs to the receiver pipeline (Phase 10);
// these functions take it explicitly and, once authenticated, confirm that
// the decrypted seq matches it (SeqMismatch).

// Decrypts and authenticates into `plaintext` (len set; metadata_bits 0).
ParseStatus open_payload(const u8* sealed, u32 length, const SessionKeys& keys, Direction direction,
                         SeqCounter counter, NativePayload& plaintext) noexcept;

// open_payload, then parse. The decrypted plaintext is wiped before returning.
// On any status other than Ok the contents of `out` are unspecified.
ParseStatus open_and_parse(const u8* sealed, u32 length, const SessionKeys& keys, Direction direction,
                           SeqCounter counter, const ModelSelector& selector,
                           ParsedPayload& out) noexcept;

}  // namespace itantra
