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

ParseStatus open_payload(const u8* sealed, u32 length, const SessionKeys& keys, Direction direction,
                         SeqCounter counter, NativePayload& plaintext) noexcept {
    plaintext.len           = 0u;
    plaintext.metadata_bits = 0u;
    if (sealed == nullptr && length != 0u) return ParseStatus::InvalidArgument;

    u8 nonce[kAeadNonceBytes];
    if (!derive_nonce(keys.session_id, direction, counter, nonce)) return ParseStatus::InvalidCounter;

    // Any AEAD refusal — bad tag, too short, larger than any payload — means
    // discard (receiver §3②: corruption and tampering are indistinguishable).
    u32 n = 0u;
    if (aead_open(keys.key, nonce, nullptr, 0u, sealed, length, plaintext.bytes, kMaxPayloadBytes, n) !=
        AeadStatus::Ok) {
        return ParseStatus::AuthenticationFailed;
    }
    plaintext.len = static_cast<u16>(n);
    return ParseStatus::Ok;
}

ParseStatus open_and_parse(const u8* sealed, u32 length, const SessionKeys& keys, Direction direction,
                           SeqCounter counter, const ModelSelector& selector,
                           ParsedPayload& out) noexcept {
    NativePayload plain;
    ParseStatus status = open_payload(sealed, length, keys, direction, counter, plain);
    if (status != ParseStatus::Ok) return status;

    u32 offset = 0u;
    status = parse_metadata(plain.bytes, plain.len, out.metadata, offset);
    if ((status == ParseStatus::Ok || status == ParseStatus::NegationMismatch) &&
        out.metadata.seq != seq_to_wire(counter)) {
        status = ParseStatus::SeqMismatch;
    }
    if (status == ParseStatus::Ok) {
        out.metadata_bits = static_cast<u16>(offset);
        const PayloadModel* model = selector.select(out.metadata);
        status = model == nullptr
                     ? ParseStatus::NoModel
                     : decode_symbols(plain.bytes, plain.len, offset, out.metadata.symbol_count, *model,
                                      out.symbols, kMaxSymbolCount);
    }

    secure_wipe(plain.bytes, plain.len);
    return status;
}

}  // namespace itantra
