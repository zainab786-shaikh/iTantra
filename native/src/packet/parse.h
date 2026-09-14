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

}  // namespace itantra
