# iTantra — Implementation Handoff

Paste the block below as the opening instruction to the implementation agent.

---

You are implementing the iTantra architecture defined by the six authoritative specification files in the project's `spec/` directory.

**IMPORTANT:** These six documents are the source of truth. Do not redesign the architecture, simplify it, replace components, or introduce alternative protocols unless a specification explicitly marks something as future or experimental.

## AUTHORITATIVE FILES

```
1. language-layer-spec.md            v1.3
2. context-manager-spec.md           v1.2
3. tier-1-2-spec.md                  v1.5
4. packet-security-transport-spec.md v1.2
5. receiver-pipeline-spec.md         v1.2
6. validation-benchmark-contract.md  v1.1
```

Read all six before writing any code. Confirm the version of each matches this list; if it does not, stop and report.

## IMPLEMENTATION PRINCIPLES

- C++17 native core, **integer arithmetic only**.
- Android integration through JNI.
- Preserve the existing Kotlin application architecture unless a spec explicitly requires a change.
- The new C++ implementation produces the **native payload**; it does **not** replace the Kotlin transport layer.
- UDP remains the current development transport. Do **not** add TCP, UDP+ACK, or any other reliability layer.
- Tier 1 and Tier 2 are candidate encodings for the same clause. Select Tier 1 only when it is **safe AND smaller at the complete native-packet level**; otherwise Tier 2.
- Tier 1 is intentionally lossy and safety-gated.
- Tier 2 is the lossless safety floor and must have **total** byte fallback.
- Context state must remain deterministic and independently reproducible on both phones.
- Sender-only decisions remain sender-only. The receiver must not re-run head selection, rule evaluation, read-back, staleness reasoning, pronoun resolution, clause segmentation, or tier selection.
- Cross-language Tier 1 renders in the **receiver's** language.
- Cross-language Tier 2 remains in the **sender's** language.
- Cross-language Tier 2 must **not** update context on either side.
- `NORMAL` and `CRITICAL` are the only message priorities. **`HIGH` does not exist.**
- `rule_priority` is rule evaluation order, **not** message priority.
- `seq` is fixed at **8 bits** on the wire, backed by a wider local counter.
- Do **not** make `seq` width dependent on `caps()`.
- No `float` or `double` anywhere in encode, decode, model or threshold paths.
- No compiler-dependent struct packing, no bitfields, no `memcpy`-based wire layouts. Explicit bit writing/reading with fixed-width integer types.
- The same deterministic `commit()` logic on sender and receiver.
- Respect the exact context hash definition and the pre-message hash check.
- `INHERIT` and `REF` must **not** reset slot `age`.
- `TIME` never inherits.
- `NEGATION` is explicit per message and is never stored as context.

## EXISTING CODE BOUNDARY

There is already an Android prototype and a C++17/CMake/JNI scaffold. **Do not delete or undo the existing native setup.** Build on it incrementally.

**Preserve** the `ITantraPacket` outer frame and the `UdpTransport` / `ThrottledTransport` boundary as structures.

**But change their contents:** `packet-security-transport-spec.md` §1.3 removes `text`, `mode`, `priority` and `language` from the outer frame. Those values now live in the native payload, which is authoritative. The outer frame retains only `id`, `senderId`, and a locally-assigned receive timestamp that is never transmitted.

"Preserve the boundary" does not mean "leave the class untouched."

```
Kotlin   ITantraPacket outer frame              structure retained, fields changed
           ↓ contains
C++      native payload (this implementation)   new
           ↓ carried by
Kotlin   UdpTransport / ThrottledTransport      unchanged
```

## FEC

FEC is part of the **final** architecture and remains in the specification, strictly outside encryption.

**FEC is NOT implemented in the current phase.** `UdpTransport` / `ThrottledTransport` provides no error correction, so receiver stage ① is a no-op (`receiver-pipeline-spec.md` §3①, `packet-security-transport-spec.md` §7.4).

Do not invent an FEC algorithm because the final architecture allows one. Loss is handled by `seq`, the context hash and repair. Loss and reordering testing must exercise those mechanisms, not pretend the transport performs FEC.

## SECURITY — PHASE 1

**ChaCha20-Poly1305 is implemented in phase 1**, not deferred (`packet-security-transport-spec.md` §6.10).

Reasons it cannot be staged: the AEAD tag replaces the CRC, so building CRC first is wasted work; nonce derivation is entangled with `seq` and the wide local counter; and C-33 is release-blocking.

Do not invent a different crypto construction. Follow §6 exactly — compress then encrypt, encrypt the whole payload, 4-byte truncated tag, derived nonce, PSK plus per-session KDF, replay window.

Two consequences that affect how you build and test:

**Golden vectors are captured pre-encryption.** The vector boundary is the assembled plaintext payload, before AEAD. Ciphertext depends on the session key, so key-derived vectors could never be stable across runs (`validation-benchmark-contract.md` §2.2).

**A build flag must bypass AEAD for determinism debugging.** `ITANTRA_DISABLE_AEAD`, debug and test builds only, rejected at compile time in release builds. Not a staging mechanism — without it, a C-01 failure with AEAD in the path cannot be diagnosed, because an authentication failure hides whether the metadata or the payload was wrong.

## GOLDEN VECTORS — ORDERING

Golden vectors cannot exist before an encoder does. The sequence is:

```
1  build the encoder
2  generate vectors from it
3  FREEZE them — they are now the contract
4  thereafter, a vector mismatch is an encoder regression,
   never a reason to regenerate
```

Regenerating vectors to clear a failing C-01 destroys the only evidence that two devices agree. Regeneration is permitted **only** on a deliberate format version bump, recorded in the relevant spec's change log.

## LANGUAGE DATA — SYNTHETIC FIXTURES FIRST

The real language packs (`lexicon.bin`, `forms.bin`, `templates.bin`, `numbers.bin`, `patterns.bin`) are derived from a corpus and codebook that the team is still building. **Do not block on them.**

Build the interfaces and the deterministic extraction engine against a **small synthetic fixture** — a handful of concepts and intents across two or three languages, enough to exercise every code path including code-mixing, inflected forms, literals and unknown words.

The fixture is a test artifact, not a shipped one. Real packs replace it without any code change; if a code change is needed, the interface is wrong.

## DO NOT IMPLEMENT YET

- **Romanised IR.** Marked `Pending one benchmark` in `tier-1-2-spec.md` §13.1. It is a compression candidate only, decided by M-30. Do not build it.
- **Cross-language tier preference and the operator toggle.** Deferred, `tier-1-2-spec.md` §8.4.
- **Tier 3.** Interface stub only, `tier-1-2-spec.md` §7.
- **FEC.** See above.
- **Clause bundling.** Deferred, `tier-1-2-spec.md` §4.2.

## JNI BOUNDARY

Keep it **coarse — one call per clause**, not per token or per symbol. JNI crossings are expensive and will show up in M-20 and M-21.

Encode and decode are single calls taking and returning byte arrays. The native side owns the context, the models and the coder; the Kotlin side owns audio, transport and UI.

## VALIDATION

`validation-benchmark-contract.md` is normative.

- **Conformance tests (C-xx) are release-blocking.** A failure is reported as a failure, never as a low score.
- **Characterisation measurements (M-xx) are recorded, not graded.** They cannot fail.
- Implement tests using the exact `C-xx` and `M-xx` IDs.
- Determinism across ≥3 devices with different SoC vendors is mandatory (C-01).
- Tier 2 must round-trip exactly, including adversarial input (C-05, C-06).
- Tier 1 must pass the configured read-back threshold (C-07).
- Low-confidence STT must never select Tier 1 (C-25).
- Unresolved context values must never be rendered (C-31).
- Context divergence must never produce silently wrong inherited values (C-11).
- Tier selection must compare complete native packet size (C-19).

Phase gates are in §4 of that document. A later gate's results are meaningless if an earlier gate fails.

## IMPLEMENTATION ORDER

```
1   Read all six specs. Confirm versions.
2   Reconcile the existing Kotlin/native boundary with the packet spec,
    including the ITantraPacket field removal (§1.3).
3   Shared low-level deterministic primitives:
      bit writer/reader, fixed-width serialisation, hashing
    Unit tests beside each.
4   Arithmetic coder — one implementation, shared by both tiers.
5   Native payload assembly and parsing.
    → generate golden vectors here, then freeze (see above)
    → C-01 … C-04
6   Encryption — ChaCha20-Poly1305, nonce derivation, replay window.
    → C-40 … C-43, C-33
7   Context Manager.
8   Language Layer data interfaces and deterministic extraction,
    against synthetic fixtures.
9   Tier 2 — subwords, byte fallback, static n-gram, non-zero floor.
    → C-05, C-06
10  Tier 1 — adjacency, head selection, rule table, slot resolution,
    literals, read-back gate.
    → C-07
11  Tier selection — safe AND smaller at complete-packet level.
    → C-17 … C-19
12  Receiver pipeline integration.
    → C-10 … C-16, C-30 … C-35
13  Integrate with the existing Kotlin transport.
14  Full conformance suite, then characterisation measurements.
15  Only then optimise performance and compression.
```

Steps 5 and 6 come before the Context Manager because golden vectors and determinism must be established before anything depends on them.

## WHEN YOU ENCOUNTER AMBIGUITY

Do not silently invent behaviour.

First check all six specs. If they genuinely conflict or leave something unspecified, **stop at that boundary** and report:

- the exact conflicting sections, with document and section numbers
- the implementation decision that is required
- the smallest possible change needed to resolve it

Do not redesign unrelated components to work around a gap.

## CODE QUALITY

- Small deterministic components with explicit interfaces.
- Keep shared wire-contract data strictly separate from sender-only data (`tier-1-2-spec.md` §3.4). The receiver must not link against rule tables, concept classes or category masks.
- No hidden global mutable state.
- Unit tests beside each low-level component.
- **Every wire-format field has an explicit encoder and decoder.** No implicit serialisation.
- Every architectural invariant has a test.

## BEFORE MAKING LARGE CHANGES

Provide a concise implementation plan and identify which specification section each change satisfies. Do not begin multi-file changes without it.
