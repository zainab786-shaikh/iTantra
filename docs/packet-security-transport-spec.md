# Packet, Security and Transport — Component Specification

**Project:** iTantra — Indian Multilingual TTS & STT Aided Neural Transceiver
**Problem Statement:** SIH 26173 (ISRO / Department of Space)
**Component:** Packet assembly · encryption · transport interface
**Version:** 1.2
**Status:** Final — ready for implementation
**Companions:** `language-layer-spec.md` v1.3 · `context-manager-spec.md` v1.2 · `tier-1-2-spec.md` v1.5 · `receiver-pipeline-spec.md` v1.2 · `validation-benchmark-contract.md` v1.1
**Implementation:** C++17, integer arithmetic only

### Changes from v1.1

- §7.4 — **FEC clarified.** It remains in the final architecture and the ordering rule is binding wherever a transport supplies it, but `UdpTransport` provides none, so **no FEC is implemented in the current phase.** v1.1 read as requiring FEC on the current transport.
- §7.7 — current-phase table records FEC as none.
- §6.10 — **new.** ChaCha20-Poly1305 is implemented in phase 1. Golden vectors are captured pre-encryption; a build flag bypasses AEAD for determinism debugging.

### Changes from v1.0

- §1.1 — new: **explicit layering boundary.** This document defines the *native payload*, not the complete wire packet. The existing Kotlin `ITantraPacket` outer frame and UDP transport are retained unchanged.
- §3 — **`seq` locked at 8 bits** on the wire. v1.0 stated both "4 bits" and "4/6/8 negotiated by `caps()`", which was contradictory. Negotiated widths are now a documented future extension only.
- §3 — **`priority` is 1 bit** (`NORMAL` / `CRITICAL`). `HIGH` is not a wire state.
- §7.7 — **UDP is retained as the development transport.** v1.0 recommended TCP or UDP+ACK; that conflicted with the working prototype and is withdrawn.
- §9 — worked example recomputed for the new field widths.

---

## 1. Purpose and Boundary

Turns tier output into an encrypted, authenticated **native payload**, and hands it to the existing transport.

```
IN    tier output — symbols, model, and message metadata
OUT   an encrypted, authenticated byte buffer

NOT   tier selection · model selection · outer framing ·
      UDP transmission · what the radio is
```

Three stages, in order:

```
§3–5   Payload assembly     symbols + metadata → byte buffer
§6     Encryption           buffer → ciphertext + tag
§7     Handoff              ciphertext → existing transport
```

### 1.1 Layering — what this document does NOT own

> **This specification defines the native payload only. It is not the complete wire packet.**

The Android application already has a working outer frame and transport. Those are **retained unchanged** for the first implementation phase.

```
┌──────────────────────────────────────────────────────┐
│  KOTLIN — existing, unchanged                        │
│                                                      │
│  ITantraPacket outer frame                           │
│    id · senderId · local timestamp                   │
│    ┌──────────────────────────────────────────────┐  │
│    │  NATIVE C++ — this document                  │  │
│    │                                              │  │
│    │  [ metadata ][ payload ][ flush ][ pad ]     │  │
│    │  [ AEAD tag ]                                │  │
│    │                                              │  │
│    │  opaque byte array to the Kotlin layer       │  │
│    └──────────────────────────────────────────────┘  │
│                                                      │
│  UdpTransport / ThrottledTransport  →  UDP datagram  │
└──────────────────────────────────────────────────────┘
```

### 1.2 Ownership

| Layer | Owner | Status |
|---|---|---|
| Outer frame (`ITantraPacket`) | Kotlin | **Existing — unchanged** |
| Native payload (metadata + compressed symbols + tag) | C++17 | **New — this document** |
| `UdpTransport` / `ThrottledTransport` | Kotlin | **Existing — unchanged** |
| UDP transmission over Wi-Fi | Kotlin | **Existing — unchanged** |

### 1.3 No duplicated fields

The native payload is **authoritative** for everything the decoder needs:

```
tier · symbol_count · seq · hash_present · priority
negation · language · context_hash
```

The outer frame must **not** carry copies of these. It retains only what the Android layer needs for its own bookkeeping — `id` and `senderId` for deduplication and logging, and a locally-assigned receive timestamp that is never transmitted.

The old `text` / `mode` / `priority` / `language` fields of the prototype's `ITantraPacket` are removed from the wire form; those values now come from the decoded native payload.

Rationale is the same as §3.4's: transmitting the same fact twice wastes bytes on packets measured in single digits, and creates two sources of truth that can disagree.

### 1.4 What the new format replaces

```
REPLACES    the prototype's RAW / PACK7 / PHRASE payload generation
RETAINS     ITantraPacket outer framing
RETAINS     UdpTransport, ThrottledTransport, UDP over Wi-Fi
```

---

## 2. Interface

```cpp
struct AssemblyInput {
    Tier          tier;           // TIER_1 | TIER_2
    const Symbol* symbols;
    uint16_t      symbol_count;
    const Model*  model;          // selected upstream

    uint8_t       seq;            // low 8 bits of the local counter (§3.6)
    Priority      priority;       // NORMAL | CRITICAL
    bool          negation;       // Tier 1 only
    LangId        language;       // Tier 2 only
    bool          hash_present;
    uint16_t      context_hash;   // valid only if hash_present
};

struct NativePayload {
    uint8_t  bytes[MAX_PAYLOAD];
    uint16_t len;
    uint16_t metadata_bits;       // boundary, for selective protection (§7.4)
};

AsmResult assemble(const AssemblyInput& in, NativePayload& out);
```

`metadata_bits` exists so a transport applying selective FEC knows where the metadata region ends.

The output is the **native payload** of §1.1 — an opaque byte array to the Kotlin layer, which wraps it in the existing `ITantraPacket` frame.

---

## 3. Metadata Layout

Bit-packed, MSB-first, **uncompressed**, written first. Ordered by parse criticality.

### 3.1 Common prefix — 17 bits

| Bits | Field | Width | Notes |
|---|---|---|---|
| 0–1 | `tier` | 2 | parse-critical; selects the model |
| 2–6 | `symbol_count` | 5 | parse-critical; the stopping condition. Escape to +11 bits if ≥ 31 |
| 7–14 | `seq` | **8** | low bits of the local counter — see §3.6 |
| 15 | `hash_present` | 1 | |
| 16 | `priority` | **1** | `0 = NORMAL`, `1 = CRITICAL` |

### 3.2 Tier-specific

| Tier | Field | Bits |
|---|---|---|
| 1 | `negation` (duplicated) | 2 |
| 2 | `language` | 4 |

### 3.3 Conditional

| Field | Bits | Present when |
|---|---|---|
| `context_hash` | 12 | `hash_present == 1` |

**Totals:** Tier 1 → 19 bits, or 31 with hash. Tier 2 → 21 bits.

### 3.6 `seq` is 8 bits on the wire, backed by a wide local counter

> **Locked for the current implementation. One unambiguous format.**

```
local counter    uint32_t (uint64_t acceptable)
                 monotonic, never wraps in practice
                 never transmitted

wire seq         the low 8 bits of that counter
                 fixed at 8 bits
```

The receiver reconstructs the full counter from the low 8 bits plus its own tracked value, choosing the candidate nearest its expectation — the same packet-number reconstruction QUIC uses.

**Why 8 and not 4.** At 4 bits a gap of 1 is indistinguishable from a gap of 17. 8 bits tolerates a burst of up to 127 lost or reordered packets before ambiguity, which is comfortable on a UDP link with no ordering guarantee (§7.7).

**Why the counter must be wider.** The nonce is derived from it (§6.5), and nonce reuse breaks AEAD catastrophically. An 8-bit counter would repeat every 256 messages.

**Future extension, not current behaviour.** A later revision may negotiate the wire width from `caps()` for links with different loss characteristics. Until then the width is fixed at 8 bits and `caps()` does **not** select it. Implementations must not read a width from `caps()`.

**No byte alignment on the metadata block.** Padding to a boundary would cost ~5 bits out of a ~48-bit packet. The payload starts at the next bit.

### 3.4 Why metadata is uncompressed and first

The decoder cannot start until it knows which model to use, and `tier` selects the model. Compressing `tier` would require the model in order to read the field that names the model.

Consequences worth keeping:

- metadata parses with zero coder state
- nothing in metadata joins the coder's determinism surface
- before encryption is enabled, a corrupt payload still yields a readable `seq`

> **Note:** once encryption is enabled (§6), metadata is inside the ciphertext and is no longer readable on a packet that fails authentication. That is a deliberate trade — see §6.3.

### 3.5 `symbol_count` is the only stopping condition

Arithmetic coding makes trailing bits genuinely ambiguous. A byte length or an end-marker symbol are both weaker choices. The decoder counts down and ignores everything after.

---

## 4. Assembly Algorithm

```
1  write metadata bits          fixed widths, fixed order, MSB-first
2  write payload bits           arithmetic coder over symbols, using model
3  flush coder                  fixed bit count, model-independent
4  pad with zeros               to the next byte boundary
5  compute CRC-8                over every byte written so far
6  append CRC
```

Steps 1 and 2 write into one contiguous bit stream.

### 4.1 Contracts

**Flush.** A fixed, model-independent number of bits. Not "until it resolves" — a fixed count, identical on both sides. Note `tier-1-2-spec.md` §13.2: flush cost is a measured parameter, not a rounding error at these payload sizes.

**Padding.** Zeros only. Never read by the parser, but covered by the CRC, so the value must be deterministic.

**CRC.** Over metadata + payload + padding. Polynomial, initial value and bit order are frozen constants, documented once. It **must** cover the metadata — otherwise a corrupted `seq` or `negation` passes undetected, and `negation` is the one field whose flip inverts the message.

> **When encryption is enabled, the AEAD tag replaces the CRC (§6.2).** Do not carry both.

---

## 5. Bounds and Errors

| Condition | Result |
|---|---|
| `symbol_count` exceeds the escape range | `ASM_TOO_LONG` — caller must split. Should not occur on clause-level input. |
| `hash_present` set but hash invalid | Programming error; assert in debug |
| Coder failure | Cannot occur — byte fallback and the non-zero floor guarantee every symbol is codable (`tier-1-2-spec.md` §6.7) |

Packet size versus transport MTU is **not** checked here — see §7.5.

---

## 6. Encryption

### 6.1 Order: compress, then encrypt. Never the reverse.

```
send      tier → assemble → ENCRYPT → transport
receive   transport → DECRYPT → parse → decode
```

Encrypted data is statistically random and does not compress. Encrypting first destroys every byte of compression work.

### 6.2 Encrypt the whole packet, and the tag replaces the CRC

Leaving metadata in the clear leaks more than it looks:

```
priority       → an observer sees exactly when an alert fires
symbol_count   → message length
seq            → traffic rate and gaps
language       → who is operating
```

In a distress system, *when an alert happens* is arguably the most sensitive bit on the wire.

An AEAD tag is a cryptographic MAC — strictly stronger than CRC-8, and it detects tampering as well as corruption. Carrying both is waste.

### 6.3 Accepted cost

Metadata is no longer readable before decryption. A corrupted packet fails authentication and is discarded whole, so the "readable `seq` on a corrupt packet" property of §3.4 is lost.

Acceptable: the **next** good packet reveals the gap via its `seq`. Detection is one message later, not lost.

### 6.4 Tag length

```
packet                6 bytes
+ 16-byte tag        22 bytes    ← standard AEAD, unusable here
+ 8-byte tag         14 bytes
+ 4-byte tag         10 bytes
```

**Truncate to 4 bytes.** 1-in-4-billion forgery odds per attempt, comfortable on a rate-limited radio link. 8 bytes if the threat model warrants it.

Even at 4 bytes this is ~65% overhead on a 6-byte packet. That is the honest number and it belongs in the harness alongside everything else.

### 6.5 The nonce must cost zero bytes

Transmitting one (8–12 bytes) would be fatal at this size. **Derive it:**

```
nonce = f(session_id, direction, message_counter)
```

Both sides track the counter; nothing goes on the wire. This is what DTLS and QUIC do.

> **Nonce reuse breaks AEAD catastrophically** — it can destroy confidentiality *and* authenticity.

`seq` is 8 bits and wraps at 256 — far too soon to derive a nonce from directly. So the **wide local counter of §3.6** (`uint32_t` or wider) is what feeds the nonce, and only its low 8 bits ride as `seq`. The receiver reconstructs the full counter from those low bits plus its own tracked value.

The transmitted field stays 8 bits; the counter never wraps in practice; the nonce never repeats.

### 6.6 Cipher

**ChaCha20-Poly1305.** Fast and constant-time in pure software, no hardware AES dependency, open-source implementations widely available. AES-GCM is fine where ARMv8 crypto extensions exist, but ChaCha20 removes the hardware question entirely — which matters on the low-end phones targeted here.

### 6.7 Keys

```
Pre-shared key, provisioned at pairing
      ↓
per-session key = KDF(PSK, nonce_A, nonce_B)
```

Both phones contribute randomness during HELLO, so each session gets fresh keys without a full key exchange. It matches how the codebook and gazetteer are already provisioned — same trust event, same moment.

X25519 ECDH is the upgrade path, but it needs authentication or it is open to man-in-the-middle. A PSK sidesteps that.

### 6.8 Replay protection — free

AEAD stops forgery but not replay. `seq` is already tracked, so add a sliding window and reject anything already seen. Costs nothing new.

### 6.9 Security review

Crypto is the one area where "looks right" is insufficient. Nonce reuse and truncated tags both have sharp edges. This design must be reviewed against a known-good reference before it ships.

### 6.10 Implementation phase — encryption is phase 1

> **ChaCha20-Poly1305 is implemented in the first phase, not deferred.**

Three reasons it cannot sensibly be staged:

- **The AEAD tag replaces the CRC (§6.2).** Building CRC first means building something that is then deleted.
- **The nonce derivation is entangled with `seq` and the wide local counter (§3.6, §6.5).** Adding it later means revisiting the metadata layout.
- **C-33 is release-blocking** (`validation-benchmark-contract.md` §5.6) — 100,000 messages with zero nonce repeats.

The cipher adds **nothing to the determinism surface**: ChaCha20-Poly1305 is byte-exact by specification and integer-only. Only the KDF and nonce derivation need pinning, and those are already covered by §8.

#### 6.10.1 Golden vectors are captured pre-encryption

Ciphertext depends on the session key, so key-derived vectors would differ on every run and C-01 could never be stable.

```
GOLDEN VECTOR BOUNDARY
    symbols → metadata + payload + flush + pad    ← vectors captured HERE
                        ↓
              AEAD encrypt + tag                  ← not part of the vector
```

Vectors are the assembled **plaintext** payload. Encryption is validated separately by C-33 and the crypto round-trip tests of §10.

#### 6.10.2 A build flag must bypass AEAD

```
ITANTRA_DISABLE_AEAD    debug/test builds only
```

Not a staging mechanism — a diagnostic one. If C-01 fails with AEAD in the path, an authentication failure hides whether the metadata or the payload was wrong, and the failure cannot be diagnosed.

**Release builds must reject this flag at compile time.**

---

## 7. Transport

### 7.1 Interface

```cpp
struct TransportCaps {
    uint16_t mtu;
    bool     reliable;      // link-layer ARQ present?
    bool     ordered;       // delivery order preserved?
    bool     duplex;
    bool     broadcast;     // one-to-many, no return channel
    uint32_t typical_rtt_ms;
};

struct Transport {
    void send(const uint8_t* pkt, size_t len, Priority p);
    void on_receive(const uint8_t* pkt, size_t len);
    TransportCaps caps() const;
};
```

### 7.2 `caps()` configures the context mode

At pairing:

```
caps.reliable && caps.ordered  →  optimistic context mode,
                                  receiver-initiated repair
otherwise                      →  periodic full-explicit mode,
                                  every Nth message
```

Both modes are specified (`context-manager-spec.md` §16.1); this selects between them.

`caps()` does **not** select the `seq` width. That is fixed at 8 bits (§3.6).

For the current implementation over UDP, `caps.reliable` and `caps.ordered` are both **false**, so the periodic full-explicit mode applies.

### 7.3 Ordering

`caps.ordered` is false on UDP, so reordered packets can read as sequence gaps and trigger sync requests that were not strictly necessary.

Accepted for the current implementation. The 8-bit `seq` (§3.6) gives enough headroom that reconstruction stays unambiguous, and a spurious sync costs one small exchange rather than a lost message.

### 7.4 FEC is strictly outside encryption

```
assemble → encrypt → FEC → wire
wire → FEC decode → decrypt+verify → parse
```

If FEC were inside encryption, bit errors would fail authentication before FEC could correct them — losing packets FEC could have saved.

```
caps.reliable == true   → skip FEC; the link already has it
caps.reliable == false  → apply FEC WHERE THE TRANSPORT PROVIDES IT
```

For selective protection of the metadata region only, use `NativePayload.metadata_bits` (§2). Under selective protection the tag sits outside the protected region — acceptable, since an authentication failure means discard either way.

#### FEC is not implemented in the current phase

> **`UdpTransport` / `ThrottledTransport` provides no FEC. None is to be implemented now.**

FEC remains part of the final architecture for the constrained radio, and the ordering rule above is binding **whenever a transport supplies it**. It is not a licence to invent one.

```
FINAL ARCHITECTURE    FEC required for the constrained radio,
                      strictly outside encryption

CURRENT PHASE         no FEC — the transport provides none
                      loss is handled by seq, context hash and repair
                      (context-manager-spec.md §16.2, §18)
```

Do not add an arbitrary FEC scheme merely because `caps.reliable` is false. A transport that performs no error correction reports none, and the receiver's stage ① (`receiver-pipeline-spec.md` §3①) is a no-op.

### 7.5 MTU and fragmentation

Normal messages are 3–12 bytes plus tag, so usually nothing to do. A long Tier 2 clause with byte fallback could exceed a small MTU.

```
Negotiate a larger MTU at connect where the link allows it
   → covers any realistic clause, no fragmentation

If the transport cannot
   → the TRANSPORT layer fragments and reassembles transparently
```

Fragmentation stays **inside** the transport abstraction. The tier layer must not split a clause further — one clause is one message, and that invariant keeps context commits ordered.

On an unreliable link, losing one fragment loses the message. Correct: treat it as a lost message and let existing recovery handle it.

### 7.6 Priority queueing

```
send queue:  CRITICAL packets first, then NORMAL, FIFO within each
```

Small detail, but it is what makes CRITICAL preemption true when the queue is busy. See §12 for the full priority definition.

### 7.7 Current transport — UDP over Wi-Fi, retained

> **The existing `UdpTransport` / `ThrottledTransport` abstraction is retained unchanged. Do not add a TCP or ACK layer for the current implementation.**

```
RETAINED    UdpTransport over Wi-Fi
RETAINED    ThrottledTransport decorator (simulated link rate)
NOT ADDED   TCP, UDP+ACK, or any new reliability layer
```

The native compression system produces bytes for that transport. Nothing about the transport changes.

**Accepted consequences:**

| | |
|---|---|
| `caps.reliable` | false |
| `caps.ordered` | false |
| Context mode | periodic full-explicit (§7.2) |
| **FEC** | **none — the transport provides none (§7.4)** |
| Packet loss | possible; a lost message is a context gap |
| Reordering | possible; may cause a spurious sync request (§7.3) |

The sequence number, context hash and repair mechanisms are all **present and available for testing** — they simply operate over a link that can genuinely lose and reorder packets, which is closer to the eventual constrained radio than a reliable link would be.

**Recovery-path testing:** inject deliberate loss and reordering later, to exercise repair on purpose rather than waiting for it to happen.

`Transport::caps()` is retained as the abstraction for future constrained radio links (§7.2). It must report honestly — a transport claiming reliability it lacks will silently mis-configure the context layer.

**Operational note:** a hotspot-based link makes one phone the access point. That phone's battery drains faster and if it dies the link dies. Fine for development; record it as a deployment constraint.

---

## 8. Determinism Surface

Four items, and this is the entire surface for the assembly stage:

```
metadata field order and widths
bit order within fields          MSB-first
coder flush bit count            fixed
CRC polynomial, init, bit order  fixed
```

Plus, once encryption is enabled:

```
KDF definition and inputs
nonce derivation function
counter width and the seq → counter reconstruction rule
```

### 8.1 Architecture independence

The packet format must be defined entirely by this spec and **not at all by the machine**:

```cpp
// wrong — layout depends on the compiler
memcpy(buf, &metadata, sizeof(metadata));

// right — layout is defined by you
put_bits(buf, pos, tier,         2);  pos += 2;
put_bits(buf, pos, symbol_count, 5);  pos += 5;
```

- **Never memcpy a struct onto the wire.** Padding and alignment differ by ABI.
- **No bitfields in structs.** Packing order is implementation-defined in C++.
- **Fixed-width types everywhere.** `uint16_t`, never `int`.
- **Byte order stated explicitly.** Every target is little-endian ARM; never rely on it.

### 8.2 No dynamic shared state

The packet layer has **none**. All its shared knowledge is compiled-in constants, verified once at pairing.

| Layer | Shared state | Can desync? |
|---|---|---|
| Context Manager | slot values, versions | Yes — hash + repair |
| Tier 2 model | static tables | No, if versions match |
| **Packet** | **none** | **No** |

The only per-session state is each side tracking the peer's last `seq`, which is self-correcting.

### 8.3 Version checking

Checked once at HELLO, alongside codebook and schema versions:

```
packet format version · coder version · CRC parameters
cipher suite · KDF version
```

Mismatch fails pairing visibly. Better than a version field in every packet, which would cost bits on every message to guard against something only wrong at connect time.

**Caveat:** a future broadcast mode has no HELLO, so broadcast packets would need their own small version field. Not a problem now — do not design it out.

---

## 9. Worked Example

```
"fire at north gate" — Tier 1, first message of a session

NATIVE PAYLOAD (this document)
  metadata        19 bits    tier 2 · count 5 · seq 8 ·
                             hash_present 1 · priority 1 · negation 2
  payload         18 bits
  flush          ~16 bits    ← coder-dependent, measured
  pad             ~3 bits
                  ─────────
                   56 bits  =  7 bytes
  AEAD tag (4 B)             4 bytes
                  ─────────
  native payload            11 bytes

OUTER FRAME (Kotlin, existing)
  ITantraPacket framing overhead — measure in situ
                  ─────────
  UDP datagram              11 bytes + outer frame
```

Two things to note.

The fixed overhead — metadata, flush, pad and tag — is **identical for both tiers**. This is why `tier-1-2-spec.md` §13.1 requires the Tier 1 versus Tier 2 margin to be measured at packet level, not payload level.

The outer frame adds further fixed bytes on top. Since §1.3 removes the duplicated fields, that frame should now be small — but measure it, because at these payload sizes an eight-byte timestamp would dominate everything this document optimises.

---

## 10. Test Vectors

```
minimum        1 symbol, no hash, both tiers
maximum        symbol_count at the escape boundary, hash present
all variants   tier × hash_present
round trip     assemble → parse → compare, fuzzed
crypto         encrypt → decrypt → compare; tampered tag rejected;
               replayed seq rejected
cross-device   identical bytes on ≥3 SoC vendors
```

Round-trip equality is the correctness test. **Cross-device byte equality is the one that decides whether the demo works.**

---

## 11. Message Priority — Normative Definition

Two message-level states. **`HIGH` is not a state** — it does not exist on the wire, in the API, or in the UI.

```
NORMAL     0
CRITICAL   1
```

### 11.1 How CRITICAL is set

```
CRITICAL  =  intents.bin[intent].is_alert          (automatic)
             OR the operator's "Send as Critical"  (manual override)
```

Either path raises it. **Neither can lower it, and the system never lowers it.** The manual override is the only path available to Tier 2 messages, which carry no intent.

### 11.2 What CRITICAL means

| Property | Behaviour |
|---|---|
| Carried in the packet | 1 bit of metadata (§3.1) |
| Send queue | overtakes all `NORMAL` traffic (§7.6) |
| Read-back threshold | higher bar than `NORMAL` (`tier-1-2-spec.md` §5.8) |
| Playback | highest priority; preempts `NORMAL` playback in progress |
| Volume | highest practical Android volume |
| Indication | vibration plus a critical visual indication |

Playback, volume and indication are the output layer's responsibility; this document defines only the wire field and the queue rule.

### 11.3 Not to be confused with `rule_priority`

The Tier 1 rule table uses a numeric ordering field (`rule_priority`: 200, 100, 10) that controls **which rule matches first**. That is rule evaluation order and has nothing to do with message priority. See `tier-1-2-spec.md` §5.4.

---

## 12. Decisions Register

| # | Decision | Status |
|---|---|---|
| 1 | Metadata uncompressed, fixed-width, written first | Locked |
| 2 | `symbol_count` is the only stopping condition | Locked |
| 3 | Fixed coder flush bit count | Locked |
| 4 | CRC covers metadata as well as payload | Locked |
| 5 | Compress then encrypt, never the reverse | Locked |
| 6 | Encrypt the whole packet, metadata included | Locked |
| 7 | AEAD tag replaces the CRC | Locked |
| 8 | Tag truncated to 4 bytes (8 if warranted) | Locked |
| 9 | Nonce derived, never transmitted | Locked |
| 10 | Wide local counter, low bits as `seq` | Locked |
| 11 | ChaCha20-Poly1305 | Locked |
| 12 | PSK + per-session KDF | Locked |
| 13 | Replay window on `seq` | Locked |
| 14 | `caps()` drives context mode only — **not** `seq` width | Locked |
| 15 | FEC strictly outside encryption | Locked |
| 16 | Fragmentation inside the transport abstraction | Locked |
| 17 | **UDP over Wi-Fi retained as the development transport; no TCP/ACK layer** | Locked |
| 18 | Versions checked at HELLO, not per packet | Locked |
| 19 | **This document defines the native payload, not the wire packet** | Locked |
| 20 | **Outer `ITantraPacket` frame and UDP transport retained unchanged** | Locked |
| 21 | **No field duplicated between outer frame and native payload** | Locked |
| 22 | **`seq` fixed at 8 bits; local counter uint32_t or wider** | Locked |
| 23 | **Message priority is NORMAL or CRITICAL only; HIGH does not exist** | Locked |
| 24 | Negotiated `seq` widths | **Future extension — not current** |
| 25 | Constrained radio choice | **Outstanding** |
| 26 | Security review against a known-good reference | **Required before ship** |
