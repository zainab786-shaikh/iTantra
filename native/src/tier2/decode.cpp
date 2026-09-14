#include "tier2/decode.h"

#include <vector>

#include "packet/parse.h"

namespace itantra {

Tier2Status tier2_decode(const Tier2Tables& tables, const u8* bytes, u32 length, const Context* context,
                         Tier2Decoded& out) {
    out = Tier2Decoded{};
    if (!tables.loaded()) return Tier2Status::InvalidArgument;
    if (bytes == nullptr) return length == 0u ? Tier2Status::Malformed : Tier2Status::InvalidArgument;

    u32 offset = 0u;
    switch (parse_metadata(bytes, length, out.metadata, offset)) {
        case ParseStatus::Ok:
            break;
        case ParseStatus::NegationMismatch:   // a Tier 1 field
            return Tier2Status::NotTier2;
        default:
            return Tier2Status::Malformed;
    }
    if (out.metadata.tier != Tier::Tier2) return Tier2Status::NotTier2;

    ContextBoost boost;
    const bool boosted = out.metadata.hash_present;
    if (boosted) {
        if (context == nullptr) return Tier2Status::ContextRequired;
        if (wire_context_hash(context_hash(*context)) != out.metadata.context_hash) {
            return Tier2Status::ContextMismatch;
        }
        boost.build(tables.boost(), *context);
    }

    const Tier2Model model(tables.ngram(), boosted ? &boost : nullptr);
    std::vector<Symbol> tokens(out.metadata.symbol_count);
    if (decode_symbols(bytes, length, offset, out.metadata.symbol_count, model, tokens.data(),
                       static_cast<u32>(tokens.size())) != ParseStatus::Ok) {
        return Tier2Status::DecodeFailure;
    }
    std::string text;
    if (!tables.vocabulary().detokenize(tokens.data(), tokens.size(), text)) return Tier2Status::DecodeFailure;
    out.text = std::move(text);
    return Tier2Status::Ok;
}

}  // namespace itantra
