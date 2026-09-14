#include "tier1/readback.h"

#include <algorithm>
#include <map>
#include <vector>

namespace itantra {

namespace {

struct Token {
    std::string text;
    u32         begin;
    u32         end;
};

std::vector<Token> tokens_of(const std::string& text) {
    std::vector<Token> out;
    std::size_t i = 0u;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        out.push_back(Token{text.substr(i, j - i), static_cast<u32>(i), static_cast<u32>(j)});
        i = j;
    }
    return out;
}

// A spoken token that belongs to the unmatched words the literal was taken from.
bool literal_token(const ClauseExtraction& clause, const SourceSpan& span, const Token& t) noexcept {
    for (const TextToken& u : clause.unmatched) {
        if (u.begin == t.begin && u.end == t.end && u.source.begin >= span.begin && u.source.end <= span.end) {
            return true;
        }
    }
    return false;
}

}  // namespace

u32 word_similarity_permille(const std::string& a, const std::string& b) {
    const std::vector<Token> x = tokens_of(a);
    const std::vector<Token> y = tokens_of(b);
    const std::size_t n = x.size();
    const std::size_t m = y.size();
    const std::size_t longest = std::max(n, m);
    if (longest == 0u) return kReadbackScale;

    std::vector<u32> prev(m + 1u);
    std::vector<u32> cur(m + 1u);
    for (std::size_t j = 0u; j <= m; ++j) prev[j] = static_cast<u32>(j);
    for (std::size_t i = 1u; i <= n; ++i) {
        cur[0] = static_cast<u32>(i);
        for (std::size_t j = 1u; j <= m; ++j) {
            const u32 substitute = prev[j - 1u] + (x[i - 1u].text == y[j - 1u].text ? 0u : 1u);
            cur[j] = std::min(std::min(prev[j] + 1u, cur[j - 1u] + 1u), substitute);
        }
        prev.swap(cur);
    }
    const u64 distance = prev[m];
    return static_cast<u32>((static_cast<u64>(longest) - distance) * kReadbackScale / longest);
}

ReadbackResult readback_check(const LanguagePack& sender_pack, const CommonPack& common, const Tier1Frame& frame,
                              const Context& sender_context, const ClauseExtraction& clause, Priority priority,
                              const SourceSpan* literal_span) {
    ReadbackResult r;
    r.bar = priority == Priority::Critical ? sender_pack.readback_critical() : sender_pack.readback_normal();

    // R2: nothing is transmitted that the template does not render.
    std::string_view template_text;
    std::vector<TemplatePiece> pieces;
    if (!sender_pack.template_text(frame.intent, template_text) || !parse_template(template_text, pieces)) return r;
    u32 placeholders = 0u;
    for (const TemplatePiece& p : pieces) {
        if (p.placeholder) placeholders |= u32{1} << p.slot;
    }
    bool has_literal = false;
    for (u32 s = 0u; s < kConceptSlotCount; ++s) {
        const SlotMode mode = frame.slots[s].mode;
        has_literal = has_literal || mode == SlotMode::Literal;
        if (mode != SlotMode::Absent && ((placeholders >> s) & 1u) == 0u) {
            r.unrendered_slots = static_cast<u8>(r.unrendered_slots | (1u << s));
        }
    }
    if (r.unrendered_slots != 0u) return r;

    const ResolvedFrame resolved = resolve_frame(common, frame, &sender_context, true);
    if (resolved.unresolved_slots != 0u) return r;

    u8 missing = 0u;
    r.render_status = render_frame(sender_pack, frame.intent, resolved.slots, r.text, missing);
    if (r.render_status != RenderStatus::Ok) return r;
    r.rendered = true;

    const std::string rendered =
        normalize_surface(reinterpret_cast<const u8*>(r.text.data()), r.text.size(), sender_pack.rules());

    // R1: every spoken token is accounted for in the rendering.
    std::map<std::string, u32> available;
    for (const Token& t : tokens_of(rendered)) ++available[t.text];
    r.covered = true;
    for (const Token& t : tokens_of(clause.match_text)) {
        if (has_literal && literal_span != nullptr && literal_token(clause, *literal_span, t)) continue;
        const auto it = available.find(t.text);
        if (it == available.end() || it->second == 0u) {
            r.covered     = false;
            r.unexplained = t.text;
            break;
        }
        --it->second;
    }

    r.similarity = word_similarity_permille(clause.match_text, rendered);
    r.passed     = r.covered && r.similarity >= r.bar;
    return r;
}

}  // namespace itantra
