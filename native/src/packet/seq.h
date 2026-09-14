#pragma once

// seq — packet §3.6, §6.5; receiver §3⑤.
//
// FROZEN (Phase 3, packet format version 1):
//
//   local counter   u64, monotonic, never transmitted
//                   (packet §3.6 permits uint32_t or uint64_t; u64 is pinned)
//   wire seq        the counter's low 8 bits, fixed — never read from caps()
//
// Reconstruction (packet §3.6: "choosing the candidate nearest its
// expectation — the same packet-number reconstruction QUIC uses";
// RFC 9000 Appendix A.3 with an 8-bit window):
//
//   expected    one more than the largest counter the receiver has accepted
//   candidate   (expected with its low 8 bits replaced by wire)
//   if candidate <= expected − 128            → candidate + 256
//   if candidate >  expected + 128            → candidate − 256
//   (neither adjustment is applied if it would leave the u64 range)
//
// A counter c is recovered exactly whenever
//
//     expected − 127  <=  c  <=  expected + 128
//
// i.e. up to 127 packets reordered behind, or up to 128 ahead (127 lost).
// Outside that window the result aliases by 256 — the ambiguity packet §3.6
// and receiver §3⑤ accept.
//
// NOT decided here, deliberately: the counter's value at session start and
// how the receiver's `expected` is initialised. Both belong to the session /
// nonce work (Phase 4) and the receiver (Phase 10).

#include "common/types.h"

namespace itantra {

using SeqCounter = u64;

constexpr u32 kSeqWindowHalf = 128u;

u8 seq_to_wire(SeqCounter counter) noexcept;

SeqCounter seq_reconstruct(SeqCounter expected, u8 wire_seq) noexcept;

}  // namespace itantra
