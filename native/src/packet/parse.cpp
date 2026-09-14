#include "packet/parse.h"

#include "coder/coder.h"
#include "common/bitio.h"

namespace itantra {

ParseStatus parse_metadata(const u8* bytes, u32 length, Metadata& metadata,
                           u32& payload_bit_offset) noexcept {
    payload_bit_offset = 0u;
    BitReader in(bytes, length);
    switch (read_metadata(in, metadata)) {
        case MetadataStatus::Ok:
            payload_bit_offset = in.bit_position();
            return ParseStatus::Ok;
        case MetadataStatus::NegationMismatch:
            payload_bit_offset = in.bit_position();
            return ParseStatus::NegationMismatch;
        case MetadataStatus::BadTier:
            return ParseStatus::BadTier;
        case MetadataStatus::Truncated:
        default:
            return ParseStatus::Truncated;
    }
}

ParseStatus decode_symbols(const u8* bytes, u32 length, u32 payload_bit_offset,
                           u16 symbol_count, const PayloadModel& model, Symbol* out,
                           u32 capacity) noexcept {
    if (symbol_count > capacity || (symbol_count != 0u && out == nullptr)) {
        return ParseStatus::InvalidArgument;
    }

    BitReader in(bytes, length);
    for (u32 skip = payload_bit_offset; skip > 0u;) {
        const u32 chunk = skip > 32u ? 32u : skip;
        in.read(static_cast<u8>(chunk));
        skip -= chunk;
    }

    ArithmeticDecoder dec(in);
    for (u32 i = 0u; i < symbol_count; ++i) {
        u32 symbol = 0u;
        if (!dec.decode(model.model_at(i, out), symbol)) return ParseStatus::DecodeFailure;
        out[i] = symbol;
    }
    return ParseStatus::Ok;
}

ParseStatus parse(const u8* bytes, u32 length, const ModelSelector& selector,
                  ParsedPayload& out) noexcept {
    u32 offset = 0u;
    const ParseStatus status = parse_metadata(bytes, length, out.metadata, offset);
    if (status != ParseStatus::Ok) return status;
    out.metadata_bits = static_cast<u16>(offset);

    const PayloadModel* model = selector.select(out.metadata);
    if (model == nullptr) return ParseStatus::NoModel;

    return decode_symbols(bytes, length, offset, out.metadata.symbol_count, *model, out.symbols,
                          kMaxSymbolCount);
}

}  // namespace itantra
