#include "packet/assemble.h"

#include <cassert>

namespace itantra {

Metadata metadata_of(const AssemblyInput& in) noexcept {
    Metadata m;
    m.tier         = in.tier;
    m.symbol_count = in.symbol_count;
    m.seq          = in.seq;
    m.hash_present = in.hash_present;
    m.priority     = in.priority;
    m.negation     = in.negation;
    m.language     = in.language;
    m.context_hash = in.context_hash;
    return m;
}

AsmResult assemble(const AssemblyInput& in, NativePayload& out) noexcept {
    out.len           = 0u;
    out.metadata_bits = 0u;

    const Metadata      md    = metadata_of(in);
    const MetadataFault fault = validate_metadata(md);
    if (fault == MetadataFault::SymbolCount) return AsmResult::TooLong;
    assert(fault != MetadataFault::ContextHashTooWide &&
           "hash_present set but context_hash invalid");
    if (fault != MetadataFault::None) return AsmResult::InvalidField;
    if (in.model == nullptr || (in.symbol_count != 0u && in.symbols == nullptr)) {
        return AsmResult::InvalidField;
    }

    // BitWriter zeroes each byte as it enters it, so nothing stale from a
    // previous use of `out` can reach the payload.
    BitWriter bw(out.bytes, kMaxPayloadBytes);
    if (!write_metadata(bw, md)) return AsmResult::InvalidField;
    const u32 metadata_bits = bw.bit_length();

    ArithmeticEncoder enc(bw);
    for (u32 i = 0u; i < in.symbol_count; ++i) {
        if (!enc.encode(in.model->model_at(i, in.symbols), in.symbols[i])) {
            return AsmResult::CoderFailure;
        }
    }
    if (!enc.finish()) return AsmResult::CoderFailure;

    bw.pad_to_byte();
    if (!bw.ok()) return AsmResult::CoderFailure;   // unreachable: kMaxPayloadBytes bound

    out.len           = static_cast<u16>(bw.byte_length());
    out.metadata_bits = static_cast<u16>(metadata_bits);
    return AsmResult::Ok;
}

AsmResult assemble_sealed(const AssemblyInput& in, const SessionKeys& keys, Direction direction,
                          SeqCounter counter, SealedPayload& out) noexcept {
    out.len = 0u;

    u8 nonce[kAeadNonceBytes];
    if (in.seq != seq_to_wire(counter) || !derive_nonce(keys.session_id, direction, counter, nonce)) {
        return AsmResult::InvalidCounter;
    }

    NativePayload plain;
    const AsmResult result = assemble(in, plain);
    if (result != AsmResult::Ok) return result;

    u32 sealed_length = 0u;
    const AeadStatus status = aead_seal(keys.key, nonce, nullptr, 0u, plain.bytes, plain.len, out.bytes,
                                        kMaxSealedPayloadBytes, sealed_length);
    secure_wipe(plain.bytes, plain.len);
    if (status != AeadStatus::Ok) return AsmResult::SealFailure;

    out.len = static_cast<u16>(sealed_length);
    return AsmResult::Ok;
}

}  // namespace itantra
