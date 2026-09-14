#include "tier2/encode.h"

#include <algorithm>

namespace itantra {

Tier2Status Tier2Clause::prepare(const u8* text, std::size_t length, const Tier2Message& message) {
    model_.reset();
    tokens_.clear();
    input_ = AssemblyInput{};

    if (!tables_.loaded() || (text == nullptr && length != 0u) || message.language > kMaxLanguage) {
        return Tier2Status::InvalidArgument;
    }

    tables_.vocabulary().tokenize(text, length, tokens_);
    if (tokens_.size() > kMaxSymbolCount) return Tier2Status::TooLong;

    const bool boosted = message.boost_context != nullptr;
    if (boosted) boost_.build(tables_.boost(), *message.boost_context);
    model_ = std::make_unique<Tier2Model>(tables_.ngram(), boosted ? &boost_ : nullptr);

    input_.tier         = Tier::Tier2;
    input_.symbols      = tokens_.data();
    input_.symbol_count = static_cast<u16>(tokens_.size());   // one coded symbol per token
    input_.model        = model_.get();
    input_.seq          = message.seq;
    input_.priority     = message.priority;
    input_.negation     = false;
    input_.language     = message.language;
    input_.hash_present = boosted;
    input_.context_hash = boosted ? wire_context_hash(context_hash(*message.boost_context)) : u16{0};
    return Tier2Status::Ok;
}

Tier2Status tier2_encode(const Tier2Tables& tables, const u8* text, std::size_t length, const Tier2Message& message,
                         NativePayload& out) {
    out.len           = 0u;
    out.metadata_bits = 0u;

    Tier2Clause clause(tables);
    const Tier2Status status = clause.prepare(text, length, message);
    if (status != Tier2Status::Ok) return status;

    switch (assemble(clause.assembly_input(), out)) {
        case AsmResult::Ok:
            return Tier2Status::Ok;
        case AsmResult::TooLong:
            return Tier2Status::TooLong;
        case AsmResult::CoderFailure:
            return Tier2Status::CoderFailure;
        default:
            return Tier2Status::InvalidArgument;
    }
}

std::vector<SourceSpan> tier2_clause_inputs(const UtteranceExtraction& extraction, std::size_t input_length) {
    std::vector<SourceSpan> out;
    const u32 n     = static_cast<u32>(input_length);
    u32       begin = 0u;
    for (std::size_t k = 0u; k < extraction.clauses.size(); ++k) {
        u32 end = n;
        if (k + 1u < extraction.clauses.size()) {
            end = std::min(std::max(extraction.clauses[k + 1u].source.begin, begin), n);
        }
        out.push_back(SourceSpan{begin, end});
        begin = end;
    }
    return out;
}

}  // namespace itantra
