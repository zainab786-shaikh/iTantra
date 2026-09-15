// Tier 1 / Tier 2 golden vector generator — implementation plan Phase 8 (end);
// packet §6.10.1 implementation resolutions ("a separate golden-vector artifact,
// frozen after Phase 8").
//
// RUN ONCE. Its output, native/test/golden/tier_vectors.bin, is FROZEN: after it
// is committed a mismatch is an encoder regression — never a reason to run this
// again (contract §2.2, §7.2). It refuses to overwrite an existing file without
// --format-version-bump, which exists only for a deliberate, recorded bump of the
// packet format, coder, tokenizer or a table version.
//
// It does NOT touch native/test/golden/vectors.bin (Phase 3, frozen).
//
//   itantra-golden-generate-tiers --packs <compiled fixture dir> --fixtures <fixture source dir>
//                                 --out <tier_vectors.bin> [--format-version-bump]
//
// Every vector is produced by the real encoders (tier2_encode for text; the
// Tier 1 frame layout and static model for frames), then checked before the file
// is written: re-derived from the tables embedded in the file itself, and decoded
// back to its input.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "golden/tier_golden_format.h"
#include "lang/languages.h"
#include "packet/parse.h"
#include "tier1/decode.h"
#include "tier2/decode.h"
#include "tier2/encode.h"
#include "tier2_fixture.h"

using namespace itantra;
using langfx::cps;

namespace {

[[noreturn]] void die(const std::string& why) {
    std::fprintf(stderr, "generate-tiers: %s\n", why.c_str());
    std::exit(3);
}

struct Gen {
    t2fx::Fixture              fx;
    tiergolden::TierGoldenFile file;
};

LangId lang(const char* code) {
    u8 id = 0u;
    if (!lang_id_of(code, id)) die(std::string("unknown language ") + code);
    return id;
}

const std::string& corpus(const Gen& g, const char* id) {
    for (const t2fx::Utterance& u : g.fx.corpus) {
        if (u.id == id) return u.text;
    }
    die(std::string("no corpus utterance ") + id);
}

Metadata metadata_of_payload(const NativePayload& p) {
    Metadata md;
    u32 offset = 0u;
    if (parse_metadata(p.bytes, p.len, md, offset) != ParseStatus::Ok) die("generated payload does not parse");
    return md;
}

void add_tier2(Gen& g, const std::string& name, const std::string& text, LangId language, Priority priority,
               const Context* boost_context, u8 seq) {
    tiergolden::TierVector v;
    v.name = name;
    v.text = text;
    v.boosted = boost_context != nullptr;
    if (v.boosted) v.context = tiergolden::snapshot_of(*boost_context);
    Tier2Message m;
    m.seq           = seq;
    m.priority      = priority;
    m.language      = language;
    m.boost_context = boost_context;
    const auto p = std::make_unique<NativePayload>();
    if (tier2_encode(g.fx.tables, reinterpret_cast<const u8*>(text.data()), text.size(), m, *p) != Tier2Status::Ok) {
        die(name + ": tier2_encode failed");
    }
    v.metadata      = metadata_of_payload(*p);
    v.metadata_bits = p->metadata_bits;
    v.payload.assign(p->bytes, p->bytes + p->len);
    g.fx.tables.vocabulary().tokenize(reinterpret_cast<const u8*>(text.data()), text.size(), v.symbols);
    g.file.vectors.push_back(std::move(v));
}

void add_tier1(Gen& g, const std::string& name, const Tier1Frame& frame, bool negation, Priority priority,
               u16 wire_hash, u8 seq) {
    tiergolden::TierVector v;
    v.name  = name;
    v.frame = frame;
    if (frame_to_symbols(g.fx.lang.common, g.fx.tables.vocabulary(), frame, v.symbols) != FrameFault::None) {
        die(name + ": invalid frame");
    }
    const Tier1Model model(g.fx.lang.common, g.fx.tables.ngram());
    AssemblyInput in;
    in.tier         = Tier::Tier1;
    in.symbols      = v.symbols.data();
    in.symbol_count = static_cast<u16>(v.symbols.size());
    in.model        = &model;
    in.seq          = seq;
    in.priority     = priority;
    in.negation     = negation;
    in.hash_present = frame_uses_context(frame);
    in.context_hash = in.hash_present ? wire_hash : u16{0};
    const auto p = std::make_unique<NativePayload>();
    if (assemble(in, *p) != AsmResult::Ok) die(name + ": assemble failed");
    v.metadata      = metadata_of_payload(*p);
    v.metadata_bits = p->metadata_bits;
    v.payload.assign(p->bytes, p->bytes + p->len);
    g.file.vectors.push_back(std::move(v));
}

std::string repeat(u32 byte, u32 n) {
    return std::string(n, static_cast<char>(static_cast<u8>(byte)));
}

std::string random_bytes(u32 seed, u32 n) {
    t2fx::XorShift32 rng{seed};
    std::string s;
    for (u32 i = 0u; i < n; ++i) s.push_back(static_cast<char>(static_cast<u8>(rng.next())));
    return s;
}

Context context_with(const Gen& g, std::initializer_list<std::pair<SlotId, const char*>> writes) {
    Context ctx;
    init_context(ctx);
    CommitPayload p{};
    p.seq = 1u;
    for (const auto& w : writes) {
        p.slots[w.first].op    = SlotOp::Write;
        p.slots[w.first].value = g.fx.lang.concepts.at(w.second);
    }
    commit(ctx, p);
    return ctx;
}

Tier1Frame frame(const Gen& g, const char* intent) {
    Tier1Frame f;
    f.intent = g.fx.lang.intents.at(intent);
    return f;
}

void set_id(Tier1Frame& f, SlotId slot, u16 value) {
    f.slots[slot].mode  = SlotMode::Id;
    f.slots[slot].value = value;
}

void set_literal(Tier1Frame& f, SlotId slot, const std::string& text) {
    f.slots[slot].mode    = SlotMode::Literal;
    f.slots[slot].literal = text;
}

void build(Gen& g) {
    const auto concept = [&g](const char* name) { return g.fx.lang.concepts.at(name); };
    const Context gate     = context_with(g, {{SLOT_LOCATION, "north_gate"}});
    const Context hospital = context_with(g, {{SLOT_LOCATION, "hospital"}, {SLOT_OBJECT, "ambulance"}});
    const u16 hospital_hash = wire_context_hash(context_hash(hospital));
    const Priority N = Priority::Normal;
    const Priority C = Priority::Critical;

    // ---- Tier 2 ----------------------------------------------------------------
    add_tier2(g, "t2-min-1-token-en", "a", lang("en"), N, nullptr, 1u);
    add_tier2(g, "t2-empty-nohash-hi", "", lang("hi"), N, nullptr, 2u);
    add_tier2(g, "t2-empty-hash-critical-ta", "", lang("ta"), C, &gate, 3u);
    add_tier2(g, "t2-sentence-en", corpus(g, "e1"), lang("en"), N, nullptr, 4u);
    add_tier2(g, "t2-sentence-hi", corpus(g, "h1"), lang("hi"), N, nullptr, 5u);
    add_tier2(g, "t2-sentence-ta", corpus(g, "t1"), lang("ta"), N, nullptr, 6u);
    add_tier2(g, "t2-boosted-hi-critical", corpus(g, "h1"), lang("hi"), C, &gate, 7u);
    add_tier2(g, "t2-boosted-ta", corpus(g, "t4"), lang("ta"), N, &hospital, 8u);
    add_tier2(g, "t2-code-mixed-hi", corpus(g, "h3"), lang("hi"), N, nullptr, 9u);
    add_tier2(g, "t2-cjk-in-ta", corpus(g, "t10"), lang("ta"), N, nullptr, 10u);
    add_tier2(g, "t2-bengali-in-hi", corpus(g, "h12"), lang("hi"), N, nullptr, 11u);
    add_tier2(g, "t2-native-digits-hi", corpus(g, "h6"), lang("hi"), N, nullptr, 12u);
    add_tier2(g, "t2-nfd-en", "Meet at the cafe" + cps({0x0301}), lang("en"), N, nullptr, 13u);
    add_tier2(g, "t2-mixed-scripts-and-invalid-bytes",
              cps({0x0906, 0x0917, 0x20, 0x0BA4, 0x0BC0, 0x20}) + "north gate " +
                  cps({0x674E, 0x660E, 0x20, 0x0645, 0x0627, 0x0621, 0x20, 0x1F468, 0x200D, 0x1F469, 0x20, 0x1F1EE,
                       0x1F1F3}) +
                  t2fx::bytes({0xFFu, 0xC0u, 0x80u, 0xEDu, 0xA0u, 0x80u}),
              lang("en"), N, nullptr, 14u);
    add_tier2(g, "t2-count-30-hash", repeat(0xFEu, 30u), lang("en"), C, &gate, 30u);
    add_tier2(g, "t2-count-31-hash", repeat(0xFEu, 31u), lang("en"), C, &gate, 31u);
    add_tier2(g, "t2-count-32-hash", repeat(0xFEu, 32u), lang("en"), C, &gate, 32u);
    add_tier2(g, "t2-count-max-2078-random-hash", random_bytes(0x2078A5u, kMaxSymbolCount), lang("ta"), C, &hospital, 78u);
    add_tier2(g, "t2-count-max-2078-random-nohash", random_bytes(0x2078B6u, kMaxSymbolCount), lang("hi"), N, nullptr, 79u);
    {
        std::string all;
        for (u32 b = 0u; b < 256u; ++b) all.push_back(static_cast<char>(static_cast<u8>(b)));
        add_tier2(g, "t2-all-256-bytes", all, lang("en"), N, nullptr, 80u);
    }
    add_tier2(g, "t2-seq-255", "send help", lang("en"), N, nullptr, 255u);
    {
        std::size_t count = 0u;
        const SupportedLanguage* all = supported_languages(count);
        for (std::size_t i = 0u; i < count; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "t2-langid-%02u-%s", static_cast<unsigned>(i + 1u), all[i].code);
            add_tier2(g, name, "ok", lang(all[i].code), N, nullptr, static_cast<u8>(100u + i));
        }
    }

    // ---- Tier 1 ----------------------------------------------------------------
    add_tier1(g, "t1-min-intent-only", frame(g, "QUERY_CASUALTY_COUNT"), false, N, 0u, 150u);
    {
        Tier1Frame f = frame(g, "REPORT_FIRE_AT");
        set_id(f, SLOT_LOCATION, concept("north_gate"));
        add_tier1(g, "t1-fire-north-gate-critical", f, false, C, 0u, 151u);
        set_id(f, SLOT_SEVERITY, concept("urgent"));
        add_tier1(g, "t1-fire-with-severity", f, false, C, 0u, 152u);
    }
    {
        Tier1Frame f = frame(g, "REQUEST_MEDICAL_AT");
        set_id(f, SLOT_OBJECT, concept("ambulance"));
        set_id(f, SLOT_LOCATION, concept("hospital"));
        set_id(f, SLOT_QUANTITY, 3u);
        add_tier1(g, "t1-medical-explicit-quantity", f, false, C, 0u, 153u);
        f.slots[SLOT_QUANTITY] = FrameSlot{};
        f.slots[SLOT_LOCATION] = FrameSlot{SlotMode::Inherit, 0u, std::string()};
        add_tier1(g, "t1-medical-inherit-location", f, false, C, hospital_hash, 154u);
        f.slots[SLOT_LOCATION].mode = SlotMode::Ref;
        add_tier1(g, "t1-medical-ref-location", f, false, C, hospital_hash, 155u);
    }
    {
        Tier1Frame f = frame(g, "CANCEL_REQUEST");
        set_id(f, SLOT_OBJECT, concept("water"));
        add_tier1(g, "t1-cancel-negated", f, true, N, 0u, 156u);
    }
    {
        Tier1Frame f = frame(g, "REPORT_CASUALTY_COUNT");
        set_id(f, SLOT_QUANTITY, 3u);
        set_id(f, SLOT_STATE, concept("injured"));
        add_tier1(g, "t1-casualty-count-with-state-head", f, false, C, 0u, 157u);
    }
    for (const u32 v : {1u, 255u, 256u, 65535u}) {
        Tier1Frame f = frame(g, "ANSWER_COUNT");
        set_id(f, SLOT_QUANTITY, static_cast<u16>(v));
        add_tier1(g, "t1-number-" + std::to_string(v), f, false, N, 0u, static_cast<u8>(158u + (v & 7u)));
    }
    {
        Tier1Frame f = frame(g, "REPORT_FIRE_AT");
        set_id(f, SLOT_LOCATION, 9u);   // not a LOCATION concept: coded through ESCAPE
        add_tier1(g, "t1-number-escape-in-concept-slot", f, false, C, 0u, 166u);
    }
    {
        const struct {
            const char* name;
            std::string text;
        } literals[] = {
            {"t1-literal-latin", "Tell Ravi to"},
            {"t1-literal-devanagari", cps({0x0930, 0x092E, 0x0947, 0x0936, 0x20, 0x0915, 0x094B})},
            {"t1-literal-tamil", cps({0x0BAE, 0x0BC1, 0x0BB0, 0x0BC1, 0x0B95, 0x0BA9, 0x0BCD})},
            {"t1-literal-bengali", cps({0x0985, 0x09AE, 0x09BF, 0x09A4})},
            {"t1-literal-cjk-emoji-byte-fallback", cps({0x674E, 0x660E, 0x20, 0x1F691})},
            {"t1-literal-invalid-bytes", t2fx::bytes({0xFFu, 0xC0u, 0x80u, 0xFEu})},
        };
        u8 seq = 170u;
        for (const auto& l : literals) {
            Tier1Frame f = frame(g, "REQUEST_MOVE");
            set_literal(f, SLOT_ACTOR, l.text);
            add_tier1(g, l.name, f, false, N, 0u, seq++);
        }
    }
    {
        Tier1Frame f = frame(g, "REPORT_STATE");
        set_id(f, SLOT_STATE, concept("trapped"));
        set_literal(f, SLOT_LOCATION, "Ravi Nagar");
        set_literal(f, SLOT_OBJECT, cps({0x092C, 0x0938}));
        add_tier1(g, "t1-two-literals", f, false, N, 0u, 180u);
    }
    for (const u32 n : {26u, 27u, 28u}) {   // 4 + n symbols: 30, 31, 32
        Tier1Frame f = frame(g, "REQUEST_MOVE");
        set_literal(f, SLOT_ACTOR, repeat(0xFEu, n));
        f.slots[SLOT_LOCATION].mode = SlotMode::Inherit;
        add_tier1(g, "t1-count-" + std::to_string(4u + n) + "-hash", f, false, C, hospital_hash, static_cast<u8>(n));
    }
    {
        Tier1Frame f = frame(g, "REQUEST_MOVE");
        set_literal(f, SLOT_ACTOR, repeat(0xFEu, kMaxLiteralTokens));
        f.slots[SLOT_LOCATION].mode = SlotMode::Inherit;
        add_tier1(g, "t1-literal-max-255-tokens-hash-critical-negated", f, true, C, 0xFFFu, 190u);
    }
    {
        Tier1Frame explicit_frame = frame(g, "REQUEST_GENERIC");
        set_id(explicit_frame, SLOT_OBJECT, concept("water"));
        Tier1Frame inherit_frame = frame(g, "REQUEST_GENERIC");
        inherit_frame.slots[SLOT_OBJECT].mode = SlotMode::Inherit;
        u32 k = 0u;
        for (u32 hp = 0u; hp < 2u; ++hp) {
            for (u32 pri = 0u; pri < 2u; ++pri) {
                for (u32 neg = 0u; neg < 2u; ++neg, ++k) {
                    const std::string name = "t1-variant-hash" + std::to_string(hp) + (pri != 0u ? "-critical" : "-normal") +
                                             "-neg" + std::to_string(neg);
                    add_tier1(g, name, hp != 0u ? inherit_frame : explicit_frame, neg != 0u, pri != 0u ? C : N,
                              static_cast<u16>((0x5A5u + 0x111u * k) & 0xFFFu), static_cast<u8>(200u + 7u * k));
                }
            }
        }
    }
    {
        Tier1Frame f = frame(g, "REPORT_FLOOD_AT");
        set_id(f, SLOT_LOCATION, concept("river"));
        add_tier1(g, "t1-seq-255-flood", f, false, C, 0u, 255u);
    }
    {
        Tier1Frame f = frame(g, "REQUEST_SUPPLY_AT");
        set_id(f, SLOT_OBJECT, concept("blanket"));
        set_id(f, SLOT_LOCATION, concept("relief_camp"));
        add_tier1(g, "t1-supply", f, false, N, 0u, 240u);
        Tier1Frame stop = frame(g, "REQUEST_STOP");
        set_id(stop, SLOT_ACTOR, concept("army"));
        add_tier1(g, "t1-stop", stop, false, N, 0u, 241u);
        Tier1Frame evacuate = frame(g, "REQUEST_EVACUATE_AT");
        set_id(evacuate, SLOT_LOCATION, concept("school"));
        add_tier1(g, "t1-evacuate", evacuate, false, C, 0u, 242u);
        Tier1Frame collapse = frame(g, "REPORT_COLLAPSE_AT");
        set_id(collapse, SLOT_LOCATION, concept("bridge"));
        add_tier1(g, "t1-collapse", collapse, false, C, 0u, 243u);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string packs, fixtures, out;
    bool bump = false;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--packs") == 0 && a + 1 < argc) packs = argv[++a];
        else if (std::strcmp(argv[a], "--fixtures") == 0 && a + 1 < argc) fixtures = argv[++a];
        else if (std::strcmp(argv[a], "--out") == 0 && a + 1 < argc) out = argv[++a];
        else if (std::strcmp(argv[a], "--format-version-bump") == 0) bump = true;
    }
    if (packs.empty() || fixtures.empty() || out.empty()) {
        std::fprintf(stderr, "usage: itantra-golden-generate-tiers --packs <dir> --fixtures <dir> --out <file> [--format-version-bump]\n");
        return 2;
    }
    if (golden::file_exists(out.c_str()) && !bump) {
        std::fprintf(stderr, "REFUSED: %s exists. Tier golden vectors are frozen (contract 2.2, 7.2).\n"
                             "A mismatch is an encoder regression. Regenerate only for a deliberate, recorded version bump.\n",
                     out.c_str());
        return 1;
    }

    Gen g;
    if (!g.fx.load(packs, fixtures)) die("fixture not loaded: " + g.fx.error);
    for (const char* dir : {"common", "tier2"}) {
        const std::vector<std::string>& names =
            std::strcmp(dir, "common") == 0 ? CommonPack::file_names() : Tier2Tables::file_names();
        PackFiles files;
        std::string error;
        if (!read_pack_directory(packs + "/" + dir, names, files, error)) die(error);
        for (PackFile& f : files) g.file.tables.push_back(tiergolden::TableFile{std::string(dir) + "/" + f.first, std::move(f.second)});
    }
    build(g);

    // Check against the file itself: serialise, load back, use only its embedded tables.
    const std::vector<u8> bytes = tiergolden::serialize(g.file);
    tiergolden::TierGoldenFile check;
    std::string error;
    if (!tiergolden::deserialize(bytes, check, error)) die("serialised file does not load back: " + error);
    tiergolden::LoadedTables tables;
    if (!tables.load(check, error)) die("embedded tables do not load: " + error);
    for (const tiergolden::TierVector& v : check.vectors) {
        std::vector<Symbol> symbols;
        std::vector<u8> payload;
        u16 bits = 0u;
        if (!tiergolden::derive_symbols(tables, v, symbols) || symbols != v.symbols ||
            !tiergolden::encode_symbols(tables, v, symbols, payload, bits) || payload != v.payload || bits != v.metadata_bits) {
            die(v.name + ": does not re-encode from the embedded tables");
        }
        if (v.metadata.tier == Tier::Tier2) {
            const Context ctx = tiergolden::context_of(v.context);
            Tier2Decoded d;
            if (tier2_decode(tables.tier2, v.payload.data(), static_cast<u32>(v.payload.size()), v.boosted ? &ctx : nullptr, d) !=
                    Tier2Status::Ok ||
                d.text != v.text) {
                die(v.name + ": does not decode to its text");
            }
        } else {
            Tier1Decoded d;
            if (tier1_decode(tables.common, tables.tier2, v.payload.data(), static_cast<u32>(v.payload.size()), d) !=
                    Tier1DecodeStatus::Ok ||
                d.frame != v.frame) {
                die(v.name + ": does not decode to its frame");
            }
        }
    }
    if (!golden::write_file(out.c_str(), bytes)) die("cannot write " + out);

    std::printf("tier golden vectors: file v%u, packet v%u, coder v%u, tokenizer v%u, n-gram v%u, boost v%u, tier1 v%u\n",
                check.file_version, check.packet_format_version, check.coder_version, check.tokenizer_version,
                check.ngram_version, check.boost_version, check.tier1_version);
    std::printf("  %zu embedded tables, %zu vectors, %zu bytes\n", check.tables.size(), check.vectors.size(), bytes.size());
    for (const tiergolden::TierVector& v : check.vectors) {
        std::printf("  %-52s T%u  %5zu sym  %4zu B ", v.name.c_str(), static_cast<u32>(v.metadata.tier), v.symbols.size(),
                    v.payload.size());
        for (std::size_t i = 0u; i < v.payload.size() && i < 6u; ++i) std::printf(" %02X", v.payload[i]);
        std::printf("\n");
    }
    return 0;
}
