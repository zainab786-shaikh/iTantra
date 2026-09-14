#pragma once

// Arithmetic coder — the ONE coder, shared by Tier 1 and Tier 2 and by both
// phones.
//
// tier §3.1    one arithmetic coder, two symbol sources (tier §12 T2)
// tier §3.2    integer arithmetic only (tier §12 T1)
// tier §13.2   flush cost is a measured decision, not a default
// packet §4    metadata and payload "write into one contiguous bit stream":
//              the coder starts on whatever bit the metadata ended on
// packet §4.1  the flush is a fixed, model-independent number of bits
// packet §3.5  symbol_count is the only stopping condition — the decoder pulls
//              exactly that many symbols and ignores everything after
//
// Symbols reach the coder only as cumulative-frequency intervals (model.h).
// Whether a symbol is an intent, a slot value or a subword is invisible here.
//
// ---------------------------------------------------------------------------
// Variant
// ---------------------------------------------------------------------------
//
// Binary-scaled arithmetic coding with underflow ("follow") bits, after
// Witten, Neal & Cleary, "Arithmetic Coding for Data Compression", CACM 30(6),
// 1987 — at 32-bit precision, emitting single bits through BitWriter.
//
// Chosen for its terminator. Byte-oriented range coders and rANS flush whole
// bytes of coder state; this flushes two bits, and needs no byte alignment
// before the payload (packet §3.6: "No byte alignment on the metadata block").
// Measured in Phase 2 against the candidates tier §13.2 names — see
// PHASE 2 MEASUREMENT at the end of this file.
//
// ---------------------------------------------------------------------------
// WIRE CONTRACT
//
// Everything in this block determines the bytes of every payload. It is part
// of the determinism surface (packet §8: "coder flush bit count — fixed") and
// of the coder version checked at HELLO (packet §8.3). After the Phase 3
// golden-vector freeze it changes only with a deliberate format version bump
// (contract §2.2, §7.2).
// ---------------------------------------------------------------------------
//
// Constants   P = kCoderPrecisionBits = 32
//             TOP = 2^32 - 1   HALF = 2^31   QUARTER = 2^30
//
// State       low, high     inclusive code-value interval [low, high]
//                           initially low = 0, high = TOP
//             follow        count of pending underflow bits, initially 0
//
// Encode symbol with interval [s.low, s.high) of s.total:
//             range = high - low + 1                       (64-bit arithmetic)
//             high  = low + floor(range * s.high / s.total) - 1
//             low   = low + floor(range * s.low  / s.total)
//
//   then renormalise — repeat while one of these applies:
//             high <  HALF                       emit 0
//             low  >= HALF                       emit 1;  low -= HALF;
//                                                         high -= HALF
//             low  >= QUARTER, high < HALF+QUARTER
//                                                follow += 1;
//                                                low -= QUARTER; high -= QUARTER
//             after any of them:  low = 2*low;  high = 2*high + 1
//
//   "emit b"  writes bit b, then `follow` copies of (1 - b); follow = 0.
//
// Finish      follow += 1;  emit (low < QUARTER ? 0 : 1)
//
// Bit order   every bit goes through BitWriter::write(bit, 1): MSB-first,
//             contiguous with whatever was written before the encoder.
//
// Decoding mirrors the encoder exactly: value holds the next P bits of the
// stream; target = floor(((value - low + 1) * total - 1) / range) selects the
// symbol through Model::find, and each renormalisation step shifts one more
// stream bit into value.

#include "coder/model.h"
#include "common/bitio.h"
#include "common/types.h"

namespace itantra {

constexpr u32 kCoderPrecisionBits = 32u;

// ---------------------------------------------------------------------------
// FLUSH BIT COUNT — FIXED.  WIRE CONTRACT.  packet §4.1, packet §8.
// ---------------------------------------------------------------------------
//
//   bit length after finish()
//       = bit length before the encoder
//       + committed_bits() just before finish()
//       + kCoderFlushBits
//
// for every model, every symbol sequence, and every sequence length including
// zero. Nothing about the terminator depends on the content.
//
// committed_bits() counts what the symbols have already paid for: the bits
// renormalisation emitted, plus the pending follow bits. A follow bit is
// payload, not terminator — each one records a halving of the interval that a
// symbol caused, exactly as an emitted bit does. Its VALUE is only known at
// the next emit, which is why finish() writes it; its EXISTENCE was fixed by
// the symbol that caused it.
//
// Why two bits suffice. After renormalisation, low < HALF <= high, and either
// low < QUARTER or high >= HALF + QUARTER. The interval therefore always
// contains a whole quarter, [QUARTER, HALF) or [HALF, HALF + QUARTER), and two
// bits name it.
//
// Consequence the parser relies on (packet §3.5). Every continuation of the
// flushed bits lies inside that quarter, so the decoded symbols do not depend
// on any bit after the flush — zero padding, the end of the buffer (which
// BitReader reads as zero), or arbitrary garbage.
constexpr u32 kCoderFlushBits = 2u;

class ArithmeticEncoder {
public:
    // The encoder owns `out` from construction until finish(): nothing else
    // may write to it in between. It starts at out.bit_length().
    explicit ArithmeticEncoder(BitWriter& out) noexcept;

    // Encodes `symbol` under `model`. Symbols may use different models, in
    // any order, provided the decoder uses the same sequence of models.
    //
    // A symbol with p == 0 — zero frequency, outside the alphabet, or a model
    // breaking M1/M2 — is a programming error: asserted. In an NDEBUG build
    // it writes nothing and latches ok() to false.
    bool encode(const Model& model, u32 symbol) noexcept;

    // The same, for a caller that already holds the interval.
    bool encode_range(const SymbolRange& range) noexcept;

    // Writes the pending follow bits and the kCoderFlushBits terminator.
    // Exactly once; encoding or finishing again afterwards is asserted.
    bool finish() noexcept;

    // Bits the encoded symbols have committed so far, relative to the start:
    // emitted bits plus pending follow bits. See kCoderFlushBits.
    u32 committed_bits() const noexcept;

    // False once any write failed (BitWriter capacity) or any contract was
    // violated. Latched: a failed encoder never extends the stream again.
    bool ok()       const noexcept { return ok_; }
    bool finished() const noexcept { return finished_; }

private:
    void emit(u32 bit) noexcept;

    BitWriter* out_;
    u64        low_;
    u64        high_;
    u32        follow_;
    u32        start_bits_;
    bool       ok_;
    bool       finished_;
};

class ArithmeticDecoder {
public:
    // Reads kCoderPrecisionBits bits from `in` immediately, so construct it
    // after the metadata has been parsed and `in` sits on the first payload
    // bit. The decoder reads up to kCoderPrecisionBits - kCoderFlushBits bits
    // beyond the flush; BitReader returns zeros past the end, so in.overran()
    // after decoding is expected and is not an error.
    explicit ArithmeticDecoder(BitReader& in) noexcept;

    // Decodes one symbol under `model` — the same model the encoder used for
    // this position. The caller decodes exactly symbol_count symbols
    // (packet §3.5); there is no end-of-stream signal.
    //
    // Any bit string decodes to SOME sequence of p > 0 symbols; this cannot
    // fail on well-formed models. A model breaking M1 or M4 is asserted, and
    // in an NDEBUG build latches ok() to false.
    bool decode(const Model& model, u32& symbol) noexcept;

    bool ok() const noexcept { return ok_; }

private:
    BitReader* in_;
    u64        low_;
    u64        high_;
    u64        value_;
    bool       ok_;
};

// ---------------------------------------------------------------------------
// PHASE 2 MEASUREMENT — coder candidates (tier §13.2, plan Phase 2 notes)
// ---------------------------------------------------------------------------
//
// [H] host measurement, not a device result and not M-32 itself (Phase 13
// records M-32 on the real models). Integer-only harness; every candidate
// round-trip verified, zero failures. 20,000 messages per row, symbols drawn
// by probability from Zipf-shaped tables with total 2^16 (rANS needs a
// power-of-two total). "Packet" = ceil((19 metadata bits + payload bits) / 8),
// the Tier 1 no-hash plaintext before AEAD (packet §3, §6.10.1).
//
//   A  this coder — bitwise, 32-bit, 2-bit flush
//   B  32-bit range coder (LZMA-style carry), naive flush: 4 bytes of state
//   C  the same range coder, carefully terminated: 1 fixed byte
//   D  rANS, 32-bit state, byte renormalisation, 4-byte state flush
//
//   mean payload bits                          mean packet bytes
//   table     symbols  ideal     A      B      C      D     A      B      C      D
//   64-sym       1      4.84   6.04  32.30   8.30  32.30   3.55   7.03   4.03   7.03
//   64-sym       2      9.69  10.76  37.79  13.79  37.79   4.17   7.72   4.72   7.72
//   64-sym       4     19.51  20.57  47.49  23.49  47.49   5.38   8.93   5.93   8.93
//   64-sym       8     38.93  39.98  66.93  42.93  66.93   7.81  11.36   8.36  11.36
//   64-sym      32    155.63 156.69 183.66 159.66 183.64  22.39  25.95  22.95  25.95
//   4096-sym     1      8.91  10.14  36.64  12.64  36.64   4.07   7.58   4.58   7.58
//   4096-sym     4     35.53  36.59  63.51  39.51  63.51   7.38  10.93   7.93  10.93
//   4096-sym    16    142.35 143.41 170.38 146.38 170.38  20.73  24.29  21.29  24.29
//
// Overhead above the ideal code length, independent of message length:
//   A ≈ 1.1 bits   C ≈ 3 bits   B, D ≈ 27 bits
//
// A beats the best byte-oriented candidate (C) by about half a byte per
// packet, and the naive range coder and rANS by about 3.5 bytes — on packets
// of 4 to 8 bytes. C's extra cost is byte granularity plus its 8-bit
// terminator; it also depends on zero bytes following the payload, where A
// tolerates any trailing bits (see kCoderFlushBits).

}  // namespace itantra
