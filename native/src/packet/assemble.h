#pragma once

// Payload assembly — packet §2, §4, §5.
//
// FROZEN: packet format version 1, pinned by native/test/golden/vectors.bin.
//
// Output — the plaintext native payload, which is also the golden vector
// boundary (packet §6.10.1, contract §2.2):
//
//   [ metadata (metadata.h) ][ coder payload ][ flush: 2 bits ][ zero pad ]
//
//   1  write metadata                fixed widths, fixed order, MSB-first
//   2  arithmetic-code the symbols   contiguous with step 1 (coder.h)
//   3  flush                         exactly kCoderFlushBits
//   4  pad with zeros                to the next byte boundary
//
// No CRC. packet §4 steps 5–6 describe CRC-8, but encryption is phase 1 and
// "the AEAD tag replaces the CRC (§6.2). Do not carry both" (§4.1). The tag is
// appended by Phase 4, after this function, outside the golden vectors.

#include "coder/coder.h"
#include "coder/model.h"
#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/nonce.h"
#include "packet/metadata.h"
#include "packet/seq.h"
#include "common/types.h"

namespace itantra {

using Symbol = u32;

// The "model selected upstream" of packet §2 — an internal model-selection
// interface. It is NOT part of the wire format: nothing about it is
// transmitted, and it does not change the arithmetic coder (coder.h), which
// still receives exactly one Model per encode/decode call.
//
// Why position-dependent. A payload is coded under a sequence of probability
// tables: Tier 1 codes an intent, slot modes, slot values and literal subwords
// from different static tables (tier §5.9, receiver §3⑧); Tier 2's n-gram
// distribution depends on the preceding tokens (tier §6.3). A single fixed
// table is the special case that ignores both arguments.
//
// Why encoder and decoder agree. The encoder calls model_at(i, input) and the
// decoder calls model_at(i, decoded), where decoded[0 .. i) equals
// input[0 .. i) whenever symbols 0 .. i-1 decoded correctly. Given the
// contract below, both therefore receive the same table at every position,
// and the coder decodes symbol i identically. By induction the whole payload
// decodes exactly.
//
// CONTRACT — every implementation (Tier 1 in Phase 8, Tier 2 in Phase 7, the
// receiver's selections in Phase 10) must satisfy all of these. Breaking any
// one desynchronises the decode silently; the interface cannot detect it.
//
//   P1  Deterministic, pure selection. The returned table depends only on
//       `position`, on preceding[0 .. position), and on state fixed before
//       coding began. Never on clocks, addresses, allocation, thread state,
//       call history, or caches that can change a result.
//
//   P2  Never read beyond the decoded prefix. preceding[position] and later
//       entries must not be read: on decode they have not been written yet.
//
//   P3  Immutable message-time state. Everything the selection depends on
//       besides the prefix — static tables, and for Tier 2 the context boost
//       gated by hash_present (tier §6.4, §6.5) — is fixed before the first
//       symbol is coded and does not change until the last one is (tier §6.6;
//       coder model.h M5). No adaptation within a message.
//
//   P4  Integer-only, valid models. Every returned Model satisfies model.h
//       M1–M4 using integer frequencies only (tier §3.2, contract C-04). A
//       Tier 2 model additionally keeps p > 0 for every vocabulary token in
//       every context (tier §6.3 non-zero floor).
//
//   P5  Returned reference lifetime. The Model& must stay valid, and unchanged,
//       until the next model_at() call on the same PayloadModel, or until the
//       PayloadModel is destroyed, whichever comes first. The coder uses it
//       before either happens. An implementation may therefore return a reused
//       scratch table; callers must not hold the reference longer.
//
//   P6  Identical tables and version on both sides. Sender and receiver must
//       hold the same tables, boost rules and selection logic, under the same
//       versions checked at HELLO (context §18.1, packet §8.3). A version
//       mismatch must fail pairing visibly; it cannot be detected here.
//
// symbol_count counts CODED symbols: one model_at() call and one coder call
// each. Tier 2 semantics, RESOLVED in Phase 7 (tier2/ngram.h, tier2/encode.h):
// Tier 2 uses Kneser-Ney — a full smoothed integer distribution per context —
// so every Tier 2 token is exactly one coded symbol and a Tier 2 symbol_count
// is the token count. There are no PPM escape symbols. The packet metadata
// layout is unchanged.
class PayloadModel {
public:
    virtual ~PayloadModel() = default;

    // The table for the symbol at `position`, given preceding[0 .. position).
    // See the contract above (P1–P6).
    virtual const Model& model_at(u32 position, const Symbol* preceding) const noexcept = 0;
};

// packet §2, field for field. `context_hash` is the 12-bit WIRE value
// (wire_context_hash() of the 16-bit context hash).
struct AssemblyInput {
    Tier                tier         = Tier::Tier1;
    const Symbol*       symbols      = nullptr;
    u16                 symbol_count = 0u;
    const PayloadModel* model        = nullptr;

    u8       seq          = 0u;        // low 8 bits of the local counter (seq.h)
    Priority priority     = Priority::Normal;
    bool     negation     = false;     // Tier 1 only
    LangId   language     = 0u;        // Tier 2 only
    bool     hash_present = false;
    u16      context_hash = 0u;        // valid only if hash_present
};

// Committed coder bits per symbol never exceed 26: before narrowing the
// interval is at least 2^30 + 2 wide (coder.h), a p > 0 symbol keeps at least
// (2^30 + 2) / 2^24 → 64 of it, and renormalisation stops once the width
// passes 2^31, after at most 26 doublings.
constexpr u32 kCoderMaxBitsPerSymbol = 26u;
static_assert(kCoderPrecisionBits == 32u && kModelMaxTotal == (u32{1} << 24),
              "re-derive kCoderMaxBitsPerSymbol");

// Largest plaintext payload: every assemble() of valid input fits.
constexpr u32 kMaxPayloadBytes =
    (kMaxMetadataBits + kCoderMaxBitsPerSymbol * kMaxSymbolCount + kCoderFlushBits + 7u) / 8u;
static_assert(kMaxPayloadBytes == 6760u, "worst-case payload size");

struct NativePayload {
    u8  bytes[kMaxPayloadBytes];
    u16 len;
    u16 metadata_bits;   // where the coder payload starts (packet §2, §7.4)
};

enum class AsmResult : u8 {
    Ok,
    TooLong,        // ASM_TOO_LONG: symbol_count > kMaxSymbolCount; the clause is never
                    // split — tier selection handles it (select/select.h, ClauseTooLong)
    InvalidField,   // a metadata field outside its range, a field on the wrong
                    // tier, or a null model / symbols pointer
    CoderFailure,   // a symbol with p == 0 under its model (programming error;
                    // the coder asserts in debug builds)
    InvalidCounter, // assemble_sealed: counter 0, above kMaxCounter, invalid
                    // direction, or in.seq is not the counter's low 8 bits
    SealFailure,    // assemble_sealed: the AEAD refused its arguments (unreachable
                    // for a valid payload)
};

Metadata metadata_of(const AssemblyInput& in) noexcept;

// Assembles `in` into `out`. On any result other than Ok, out.len and
// out.metadata_bits are 0 and no payload exists.
//
// hash_present with a context_hash wider than 12 bits is the programming error
// packet §5 names: asserted in debug, InvalidField otherwise.
AsmResult assemble(const AssemblyInput& in, NativePayload& out) noexcept;

// ---------------------------------------------------------------------------
// Encrypt after assembly — packet §6.1, §6.2, §6.10 (Phase 4)
// ---------------------------------------------------------------------------
//
//   assemble()  →  plaintext native payload   ← golden vector boundary, unchanged
//   aead_seal() →  ciphertext ‖ 4-byte tag     ← what the Kotlin layer carries
//
// Compress then encrypt, never the reverse (§6.1). The whole payload,
// metadata included, is encrypted (§6.2). assemble() itself is untouched, so
// the frozen golden vectors still describe its output exactly.
//
// `counter` is this message's wide counter in `direction` (crypto/nonce.h).
// in.seq must equal seq_to_wire(counter): the seq on the wire and the counter
// in the nonce are one value, never two sources of truth.

constexpr u32 kMaxSealedPayloadBytes = kMaxPayloadBytes + kAeadTagBytes;
static_assert(kMaxPayloadBytes <= kAeadMaxMessageBytes, "AEAD message bound");

struct SealedPayload {
    u8  bytes[kMaxSealedPayloadBytes];
    u16 len;
};

// On any result other than Ok, out.len is 0. The intermediate plaintext is
// wiped before returning.
AsmResult assemble_sealed(const AssemblyInput& in, const SessionKeys& keys, Direction direction,
                          SeqCounter counter, SealedPayload& out) noexcept;

}  // namespace itantra
