// Unit tests — packet/metadata and packet/seq. Implementation plan Phase 3.
//
//   every metadata field has an explicit encoder AND decoder
//   Tier 1 no-hash = 19 bits; with hash = 31; Tier 2 = 21
//   symbol_count escape path (>= 31) round-trips
//   seq: wide counter → 8 bits → reconstruct, across wrap
//
// Plus the Phase 3 context-hash wire mapping (packet §3.3).

#include "common/hash.h"
#include "packet/metadata.h"
#include "packet/seq.h"
#include "itest.h"

using namespace itantra;

namespace {

Metadata tier1(bool hash_present) {
    Metadata m;
    m.tier         = Tier::Tier1;
    m.hash_present = hash_present;
    m.context_hash = hash_present ? 0x123u : 0u;
    return m;
}

Metadata tier2(bool hash_present) {
    Metadata m;
    m.tier         = Tier::Tier2;
    m.language     = 7u;
    m.hash_present = hash_present;
    m.context_hash = hash_present ? 0x123u : 0u;
    return m;
}

u32 written_bits(const Metadata& m) {
    u8 buf[8] = {};
    BitWriter w(buf, 8u);
    ITEST_TRUE(write_metadata(w, m));
    return w.bit_length();
}

}  // namespace

// ---------------------------------------------------------------------------
// Totals — packet §3 "Tier 1 → 19 bits, or 31 with hash. Tier 2 → 21 bits."
// ---------------------------------------------------------------------------

ITEST(metadata_totals_match_packet_spec) {
    ITEST_EQ(metadata_bit_length(tier1(false)), 19u);
    ITEST_EQ(metadata_bit_length(tier1(true)), 31u);
    ITEST_EQ(metadata_bit_length(tier2(false)), 21u);
    ITEST_EQ(metadata_bit_length(tier2(true)), 33u);

    ITEST_EQ(written_bits(tier1(false)), 19u);
    ITEST_EQ(written_bits(tier1(true)), 31u);
    ITEST_EQ(written_bits(tier2(false)), 21u);
    ITEST_EQ(written_bits(tier2(true)), 33u);
}

ITEST(escape_adds_eleven_bits_to_every_total) {
    const Metadata bases[] = {tier1(false), tier1(true), tier2(false), tier2(true)};
    for (Metadata m : bases) {
        const u32 short_bits = metadata_bit_length(m);
        m.symbol_count = 30u;
        ITEST_EQ(written_bits(m), short_bits);
        m.symbol_count = 31u;
        ITEST_EQ(written_bits(m), short_bits + 11u);
        m.symbol_count = static_cast<u16>(kMaxSymbolCount);
        ITEST_EQ(written_bits(m), short_bits + 11u);
        ITEST_EQ(metadata_bit_length(m), short_bits + 11u);
    }
    ITEST_EQ(kMaxMetadataBits, 44u);
}

// ---------------------------------------------------------------------------
// One encoder and one decoder per field, against hand-computed bits
// ---------------------------------------------------------------------------

ITEST(field_tier_encoder_and_decoder) {
    u8 buf[1] = {};
    BitWriter w(buf, 1u);
    put_tier(w, Tier::Tier1);
    put_tier(w, Tier::Tier2);
    ITEST_EQ(buf[0], 0x60u);   // 01 10 0000

    BitReader r(buf, 1u);
    Tier t = Tier::Tier2;
    ITEST_TRUE(get_tier(r, t));
    ITEST_TRUE(t == Tier::Tier1);
    ITEST_TRUE(get_tier(r, t));
    ITEST_TRUE(t == Tier::Tier2);

    const u8 bad[1] = {0x30};   // 00 11
    BitReader rb(bad, 1u);
    ITEST_TRUE(!get_tier(rb, t));
    ITEST_TRUE(!get_tier(rb, t));
}

ITEST(field_symbol_count_short_form_is_five_bits) {
    for (u32 c = 0u; c < 31u; ++c) {
        u8 buf[2] = {};
        BitWriter w(buf, 2u);
        put_symbol_count(w, static_cast<u16>(c));
        ITEST_EQ(w.bit_length(), 5u);
        ITEST_EQ(buf[0], c << 3);
        BitReader r(buf, 2u);
        ITEST_EQ(get_symbol_count(r), c);
        ITEST_EQ(r.bit_position(), 5u);
    }
}

ITEST(field_symbol_count_escape_path_round_trips) {
    //   31   → 11111 000 0000 0000   = F8 00
    //   32   → 11111 000 0000 0001   = F8 01
    //   2078 → 11111 111 1111 1111   = FF FF
    const u32 counts[]   = {31u, 32u, 2078u};
    const u8  expect[][2] = {{0xF8, 0x00}, {0xF8, 0x01}, {0xFF, 0xFF}};
    for (u32 i = 0u; i < 3u; ++i) {
        u8 buf[2] = {};
        BitWriter w(buf, 2u);
        put_symbol_count(w, static_cast<u16>(counts[i]));
        ITEST_EQ(w.bit_length(), 16u);
        ITEST_EQ(buf[0], expect[i][0]);
        ITEST_EQ(buf[1], expect[i][1]);
    }

    // Every count, both forms: exact round trip, canonical length.
    for (u32 c = 0u; c <= kMaxSymbolCount; ++c) {
        u8 buf[3] = {};
        BitWriter w(buf, 3u);
        put_symbol_count(w, static_cast<u16>(c));
        ITEST_EQ(w.bit_length(), c < 31u ? 5u : 16u);
        BitReader r(buf, 3u);
        ITEST_EQ(get_symbol_count(r), c);
        ITEST_EQ(r.bit_position(), w.bit_length());
    }

    // Every 16-bit escaped pattern decodes to a count >= 31: no count has a
    // second, longer encoding.
    for (u32 ext = 0u; ext < 2048u; ++ext) {
        const u8 buf[2] = {static_cast<u8>(0xF8u | (ext >> 8)), static_cast<u8>(ext & 0xFFu)};
        BitReader r(buf, 2u);
        ITEST_EQ(get_symbol_count(r), 31u + ext);
    }
}

ITEST(field_seq_encoder_and_decoder) {
    for (u32 s = 0u; s < 256u; ++s) {
        u8 buf[1] = {};
        BitWriter w(buf, 1u);
        put_seq(w, static_cast<u8>(s));
        ITEST_EQ(w.bit_length(), 8u);
        ITEST_EQ(buf[0], s);
        BitReader r(buf, 1u);
        ITEST_EQ(get_seq(r), s);
    }
}

ITEST(field_hash_present_and_priority_encoders_and_decoders) {
    u8 buf[1] = {};
    BitWriter w(buf, 1u);
    put_hash_present(w, true);
    put_hash_present(w, false);
    put_priority(w, Priority::Critical);
    put_priority(w, Priority::Normal);
    ITEST_EQ(w.bit_length(), 4u);
    ITEST_EQ(buf[0], 0xA0u);   // 1 0 1 0

    BitReader r(buf, 1u);
    ITEST_TRUE(get_hash_present(r));
    ITEST_TRUE(!get_hash_present(r));
    ITEST_TRUE(get_priority(r) == Priority::Critical);
    ITEST_TRUE(get_priority(r) == Priority::Normal);
}

ITEST(field_negation_is_two_copies_and_disagreement_is_reported) {
    u8 buf[1] = {};
    BitWriter w(buf, 1u);
    put_negation(w, true);
    put_negation(w, false);
    ITEST_EQ(w.bit_length(), 4u);
    ITEST_EQ(buf[0], 0xC0u);   // 11 00

    BitReader r(buf, 1u);
    bool n = false;
    ITEST_TRUE(get_negation(r, n));
    ITEST_TRUE(n);
    ITEST_TRUE(get_negation(r, n));
    ITEST_TRUE(!n);

    const u8 split[1] = {0x60};   // 01 10
    BitReader rs(split, 1u);
    n = true;
    ITEST_TRUE(!get_negation(rs, n));
    ITEST_TRUE(!n);
    n = true;
    ITEST_TRUE(!get_negation(rs, n));
    ITEST_TRUE(!n);
}

ITEST(field_language_encoder_and_decoder) {
    for (u32 l = 0u; l <= kMaxLanguage; ++l) {
        u8 buf[1] = {};
        BitWriter w(buf, 1u);
        put_language(w, static_cast<LangId>(l));
        ITEST_EQ(w.bit_length(), 4u);
        ITEST_EQ(buf[0], l << 4);
        BitReader r(buf, 1u);
        ITEST_EQ(get_language(r), l);
    }
}

ITEST(field_context_hash_encoder_and_decoder) {
    u8 buf[2] = {};
    BitWriter w(buf, 2u);
    put_context_hash(w, 0xABCu);
    ITEST_EQ(w.bit_length(), 12u);
    ITEST_EQ(buf[0], 0xABu);
    ITEST_EQ(buf[1], 0xC0u);

    for (u32 h = 0u; h <= kMaxWireContextHash; ++h) {
        u8 b[2] = {};
        BitWriter hw(b, 2u);
        put_context_hash(hw, static_cast<u16>(h));
        BitReader r(b, 2u);
        ITEST_EQ(get_context_hash(r), h);
    }
}

// ---------------------------------------------------------------------------
// Whole block
// ---------------------------------------------------------------------------

ITEST(metadata_block_matches_hand_computed_bytes_tier1) {
    // tier 01 | count 00001 | seq 10100101 | hash_present 0 | priority 1 | negation 11
    //   0100 0011  0100 1010  111 00000   = 43 4A E0     (19 bits)
    Metadata m;
    m.tier = Tier::Tier1;
    m.symbol_count = 1u;
    m.seq = 0xA5u;
    m.priority = Priority::Critical;
    m.negation = true;

    u8 buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    BitWriter w(buf, 4u);
    ITEST_TRUE(write_metadata(w, m));
    ITEST_EQ(w.bit_length(), 19u);
    ITEST_EQ(buf[0], 0x43u);
    ITEST_EQ(buf[1], 0x4Au);
    ITEST_EQ(buf[2], 0xE0u);

    BitReader r(buf, 3u);
    Metadata back;
    ITEST_TRUE(read_metadata(r, back) == MetadataStatus::Ok);
    ITEST_TRUE(back == m);
    ITEST_EQ(r.bit_position(), 19u);
}

ITEST(metadata_block_matches_hand_computed_bytes_tier2_with_hash) {
    // tier 10 | count 00000 | seq 00000000 | hash_present 1 | priority 0 |
    // language 1111 | context_hash 1010 1011 1100
    //   1000 0000  0000 0001  0111 1101  0101 1110  0          = 80 01 7D 5E 00  (33 bits)
    Metadata m;
    m.tier = Tier::Tier2;
    m.hash_present = true;
    m.language = 0xFu;
    m.context_hash = 0xABCu;

    u8 buf[5] = {};
    BitWriter w(buf, 5u);
    ITEST_TRUE(write_metadata(w, m));
    ITEST_EQ(w.bit_length(), 33u);
    const u8 expect[5] = {0x80, 0x01, 0x7D, 0x5E, 0x00};
    for (u32 i = 0u; i < 5u; ++i) ITEST_EQ(buf[i], expect[i]);

    BitReader r(buf, 5u);
    Metadata back;
    ITEST_TRUE(read_metadata(r, back) == MetadataStatus::Ok);
    ITEST_TRUE(back == m);
}

ITEST(metadata_block_round_trips_every_field_combination) {
    const u16 counts[] = {0u, 1u, 30u, 31u, 1000u, 2078u};
    const u8  seqs[]   = {0u, 1u, 127u, 128u, 255u};
    for (u32 tier = 1u; tier <= 2u; ++tier) {
        for (u16 count : counts) {
            for (u8 seq : seqs) {
                for (u32 hp = 0u; hp < 2u; ++hp) {
                    for (u32 pri = 0u; pri < 2u; ++pri) {
                        for (u32 x = 0u; x < (tier == 1u ? 2u : 16u); ++x) {
                            Metadata m;
                            m.tier = static_cast<Tier>(tier);
                            m.symbol_count = count;
                            m.seq = seq;
                            m.hash_present = hp != 0u;
                            m.context_hash = hp != 0u ? static_cast<u16>((count * 7u + x) & 0xFFFu) : 0u;
                            m.priority = static_cast<Priority>(pri);
                            if (tier == 1u) m.negation = x != 0u; else m.language = static_cast<LangId>(x);

                            ITEST_TRUE(validate_metadata(m) == MetadataFault::None);
                            u8 buf[6] = {};
                            BitWriter w(buf, 6u);
                            ITEST_TRUE(write_metadata(w, m));
                            ITEST_EQ(w.bit_length(), metadata_bit_length(m));
                            BitReader r(buf, 6u);
                            Metadata back;
                            ITEST_TRUE(read_metadata(r, back) == MetadataStatus::Ok);
                            ITEST_TRUE(back == m);
                            ITEST_EQ(r.bit_position(), w.bit_length());
                        }
                    }
                }
            }
        }
    }
}

ITEST(read_metadata_rejects_tier_00_and_11) {
    const u8 zero[4] = {0x00, 0x00, 0x00, 0x00};
    const u8 three[4] = {0xC0, 0x00, 0x00, 0x00};
    Metadata m;
    BitReader r0(zero, 4u);
    ITEST_TRUE(read_metadata(r0, m) == MetadataStatus::BadTier);
    BitReader r3(three, 4u);
    ITEST_TRUE(read_metadata(r3, m) == MetadataStatus::BadTier);
}

ITEST(read_metadata_reports_truncation_at_every_short_length) {
    Metadata m = tier1(true);
    m.symbol_count = 500u;   // escape: 42 bits
    u8 buf[6] = {};
    BitWriter w(buf, 6u);
    ITEST_TRUE(write_metadata(w, m));
    ITEST_EQ(w.bit_length(), 42u);

    for (u32 len = 0u; len < 6u; ++len) {
        BitReader r(len == 0u ? nullptr : buf, len);
        Metadata back;
        ITEST_TRUE(read_metadata(r, back) == MetadataStatus::Truncated);
    }
    BitReader full(buf, 6u);
    Metadata back;
    ITEST_TRUE(read_metadata(full, back) == MetadataStatus::Ok);
}

ITEST(read_metadata_reports_negation_mismatch_and_still_reads_the_rest) {
    Metadata m = tier1(true);
    m.seq = 99u;
    m.negation = true;
    u8 buf[4] = {};
    BitWriter w(buf, 4u);
    ITEST_TRUE(write_metadata(w, m));

    // negation copies are bits 17 and 18; clear the second
    buf[2] = static_cast<u8>(buf[2] & ~0x20u);
    BitReader r(buf, 4u);
    Metadata back;
    ITEST_TRUE(read_metadata(r, back) == MetadataStatus::NegationMismatch);
    ITEST_TRUE(!back.negation);
    ITEST_EQ(back.seq, 99u);
    ITEST_EQ(back.context_hash, 0x123u);
    ITEST_EQ(r.bit_position(), 31u);
}

ITEST(validate_metadata_names_every_fault) {
    Metadata m;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::None);

    m = Metadata{}; m.tier = static_cast<Tier>(0u);
    ITEST_TRUE(validate_metadata(m) == MetadataFault::Tier);
    m = Metadata{}; m.tier = static_cast<Tier>(3u);
    ITEST_TRUE(validate_metadata(m) == MetadataFault::Tier);
    m = Metadata{}; m.symbol_count = static_cast<u16>(kMaxSymbolCount + 1u);
    ITEST_TRUE(validate_metadata(m) == MetadataFault::SymbolCount);
    m = Metadata{}; m.priority = static_cast<Priority>(2u);
    ITEST_TRUE(validate_metadata(m) == MetadataFault::Priority);
    m = tier2(false); m.negation = true;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::NegationOnTier2);
    m = tier1(false); m.language = 1u;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::LanguageOnTier1);
    m = tier2(false); m.language = 16u;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::Language);
    m = tier1(true); m.context_hash = 0x1000u;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::ContextHashTooWide);
    m = tier1(false); m.context_hash = 1u;
    ITEST_TRUE(validate_metadata(m) == MetadataFault::ContextHashWithoutFlag);

    u8 buf[8] = {};
    BitWriter w(buf, 8u);
    ITEST_TRUE(!write_metadata(w, m));
    ITEST_EQ(w.bit_length(), 0u);
}

// ---------------------------------------------------------------------------
// Context hash wire mapping — packet §3.3, frozen in Phase 3
// ---------------------------------------------------------------------------

ITEST(wire_context_hash_is_the_low_twelve_bits) {
    ITEST_EQ(wire_context_hash(0x0000u), 0x000u);
    ITEST_EQ(wire_context_hash(0xFFFFu), 0xFFFu);
    ITEST_EQ(wire_context_hash(0xEF8Au), 0xF8Au);   // hash of the all-zero table
    ITEST_EQ(wire_context_hash(0x0FE1u), 0xFE1u);
    ITEST_EQ(wire_context_hash(0x1234u), 0x234u);
    for (u32 h = 0u; h <= 0xFFFFu; ++h) ITEST_EQ(wire_context_hash(static_cast<u16>(h)), h & 0xFFFu);
}

ITEST(wire_context_hash_detects_every_single_bit_flip_and_every_ver_change) {
    // The two guarantees that decided the mapping (metadata.h).
    ContextHashInput sample{};
    for (u32 s = 0u; s < kSlotCount; ++s) {
        sample.current[s] = static_cast<u16>(0x1234u * (s + 1u));
        sample.ver[s] = static_cast<u8>(17u * s + 3u);
    }
    const ContextHashInput bases[] = {ContextHashInput{}, sample};
    for (const ContextHashInput& base : bases) {
        const u16 w0 = wire_context_hash(context_hash(base));
        for (u32 s = 0u; s < kSlotCount; ++s) {
            for (u32 bit = 0u; bit < 16u; ++bit) {
                ContextHashInput in = base;
                in.current[s] = static_cast<u16>(in.current[s] ^ (1u << bit));
                ITEST_TRUE(wire_context_hash(context_hash(in)) != w0);
            }
            for (u32 v = 0u; v < 256u; ++v) {
                if (v == base.ver[s]) continue;
                ContextHashInput in = base;
                in.ver[s] = static_cast<u8>(v);
                ITEST_TRUE(wire_context_hash(context_hash(in)) != w0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// seq — packet §3.6
// ---------------------------------------------------------------------------

ITEST(seq_wire_is_the_low_eight_bits_of_the_counter) {
    ITEST_EQ(seq_to_wire(0u), 0u);
    ITEST_EQ(seq_to_wire(255u), 255u);
    ITEST_EQ(seq_to_wire(256u), 0u);
    ITEST_EQ(seq_to_wire(0x1234567890ABCDEFull), 0xEFu);
    ITEST_EQ(seq_to_wire(0xFFFFFFFFFFFFFFFFull), 0xFFu);
}

ITEST(seq_reconstructs_every_counter_within_the_window) {
    const SeqCounter expectations[] = {
        127u, 128u, 200u, 255u, 256u, 257u, 511u, 512u, 1000u, 65535u, 65536u,
        0xFFFFFFFFull, 0x100000000ull, (SeqCounter{1} << 40) + 17u,
    };
    for (SeqCounter expected : expectations) {
        for (u32 behind = 0u; behind <= 127u && behind <= expected; ++behind) {
            const SeqCounter c = expected - behind;
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(c)), c);
        }
        for (u32 ahead = 1u; ahead <= 128u; ++ahead) {
            const SeqCounter c = expected + ahead;
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(c)), c);
        }
    }
}

ITEST(seq_reconstructs_across_many_wraps_with_gaps_and_reordering) {
    SeqCounter expected = 0u;   // next counter the receiver expects
    SeqCounter counter  = 0u;
    u32 state = 0x2468ACE1u;
    for (u32 i = 0u; i < 20000u; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const u32 gap = state % 129u;                 // 0…128 lost
        counter += gap;
        ITEST_EQ(seq_reconstruct(expected, seq_to_wire(counter)), counter);
        expected = counter + 1u;

        // an older packet arriving late, up to 127 behind
        const u32 late = 1u + (state >> 8) % 127u;
        if (late <= counter) {
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(expected - late)), expected - late);
        }
        counter += 1u;
    }
    ITEST_TRUE(counter > 256u * 50u);   // many wraps of the 8-bit field
}

ITEST(seq_near_zero_never_underflows) {
    for (SeqCounter expected = 0u; expected < 128u; ++expected) {
        for (SeqCounter c = 0u; c <= expected + 128u; ++c) {
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(c)), c);
        }
    }
}

ITEST(seq_near_the_top_of_the_counter_never_overflows) {
    const SeqCounter kMax = 0xFFFFFFFFFFFFFFFFull;
    const SeqCounter expectations[] = {kMax, kMax - 10u, kMax - 128u, kMax - 300u};
    for (SeqCounter expected : expectations) {
        for (u32 behind = 0u; behind <= 127u; ++behind) {
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(expected - behind)), expected - behind);
        }
        for (u32 ahead = 1u; ahead <= 128u && ahead <= kMax - expected; ++ahead) {
            ITEST_EQ(seq_reconstruct(expected, seq_to_wire(expected + ahead)), expected + ahead);
        }
    }
}

ITEST(seq_outside_the_window_aliases_by_256) {
    // The documented limit, not a defect: 129 ahead reads as 127 behind.
    const SeqCounter expected = 1000u;
    ITEST_EQ(seq_reconstruct(expected, seq_to_wire(expected + 129u)), expected - 127u);
    ITEST_EQ(seq_reconstruct(expected, seq_to_wire(expected - 128u)), expected + 128u);
}

ITEST_MAIN("unit.metadata")
