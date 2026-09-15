#include "tier1/decode.h"

#include <vector>

#include "lang/render.h"
#include "packet/parse.h"

namespace itantra {

Tier1DecodeStatus tier1_decode(const CommonPack& common, const Tier2Tables& subwords, const u8* bytes, u32 length,
                               Tier1Decoded& out) {
    out = Tier1Decoded{};
    if (!subwords.loaded()) return Tier1DecodeStatus::InvalidArgument;
    if (bytes == nullptr) return length == 0u ? Tier1DecodeStatus::Malformed : Tier1DecodeStatus::InvalidArgument;

    u32 offset = 0u;
    switch (parse_metadata(bytes, length, out.metadata, offset)) {
        case ParseStatus::Ok:
            break;
        case ParseStatus::NegationMismatch:
            return Tier1DecodeStatus::NegationMismatch;
        default:
            return Tier1DecodeStatus::Malformed;
    }
    if (out.metadata.tier != Tier::Tier1) return Tier1DecodeStatus::NotTier1;

    const Tier1Model model(common, subwords.ngram());
    if (!model.valid()) return Tier1DecodeStatus::InvalidArgument;
    std::vector<Symbol> symbols(out.metadata.symbol_count);
    if (decode_symbols(bytes, length, offset, out.metadata.symbol_count, model, symbols.data(),
                       static_cast<u32>(symbols.size())) != ParseStatus::Ok) {
        return Tier1DecodeStatus::DecodeFailure;
    }
    if (!symbols_to_frame(common, subwords.vocabulary(), symbols.data(), static_cast<u32>(symbols.size()),
                          out.frame)) {
        return Tier1DecodeStatus::Malformed;
    }
    if (out.metadata.hash_present != frame_uses_context(out.frame)) return Tier1DecodeStatus::Malformed;
    return Tier1DecodeStatus::Ok;
}

Tier1Received tier1_resolve(const CommonPack& common, const Tier1Decoded& decoded, const Context* receiver_context) {
    Tier1Received r;
    r.context_matched = !decoded.metadata.hash_present ||
                        (receiver_context != nullptr &&
                         wire_context_hash(context_hash(*receiver_context)) == decoded.metadata.context_hash);
    r.resolved = resolve_frame(common, decoded.frame, receiver_context, r.context_matched);
    return r;
}

Tier1RenderStatus tier1_render(const LanguagePack& listener_pack, const Tier1Decoded& decoded,
                               const Tier1Received& received, std::string& text) {
    text.clear();
    if (received.resolved.unresolved_slots != 0u) return Tier1RenderStatus::Unresolved;
    u8 missing = 0u;
    if (render_frame(listener_pack, decoded.frame.intent, received.resolved.slots, text, missing) != RenderStatus::Ok) {
        text.clear();
        return Tier1RenderStatus::RenderFailed;
    }
    return Tier1RenderStatus::Ok;
}

}  // namespace itantra
