# Monocypher — vendored

Used for ChaCha20-Poly1305 (RFC 8439) and HKDF-SHA-512 (RFC 5869).
Implementation plan Phase 4: "Use a reviewed ChaCha20-Poly1305 implementation. Do not hand-roll the cipher."

| | |
|---|---|
| Project | Monocypher, https://monocypher.org |
| Version | 4.0.2 |
| Licence | dual BSD-2-Clause / CC0-1.0 — see `LICENCE.md` (BSD-2-Clause relied on) |
| Modified | **No.** Files are byte-identical to the release tarball |

## Provenance

Downloaded from two independent hosts and compared:

| Source | SHA-256 |
|---|---|
| `https://monocypher.org/download/monocypher-4.0.2.tar.gz` | `38D07179738C0C90677DBA3CEB7A7B8496BCFEA758BA1A53E803FED30AE0879C` |
| `https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz` | `BC1CA30B1B2654E4E7DAF2492C0D204200E55137F23FDA6B7142FD7D523BD6B4` |

The four source files differ between the two archives in exactly one line each: the version comment (`// Monocypher version 4.0.2` in the release, `// Monocypher version __git__` in the tag archive). Every other line is identical. The files here are from the release tarball.

| File | Origin in tarball | SHA-256 |
|---|---|---|
| `monocypher.c` | `src/monocypher.c` | `AFE2B098C8569577A84488E0B98D276D1FBA6506ADEA68BB9241A52111734C59` |
| `monocypher.h` | `src/monocypher.h` | `F78BB31255CFB7BEBA66AFD2137F5194C8A025CF40488B6CC1E295234D43F374` |
| `monocypher-ed25519.c` | `src/optional/monocypher-ed25519.c` | `7C9B16056CBD27521919E8A6F56A228808B9E718AFC42E3D33F28C08E5ABDEE2` |
| `monocypher-ed25519.h` | `src/optional/monocypher-ed25519.h` | `BD546EDCD468D64E28CAA3DBF4B1D6BFAD7435C0CE994723FD81AAE26405121B` |
| `LICENCE.md` | `LICENCE.md` | `5F8360E4C06DDCC584BDB4B210C6AF824C4BB301E6A9A521869B6D90795CA4B3` |

## How it is built

- Compiled **as C++** in both the host and the Android build (`LANGUAGE CXX`, `sources.cmake`). Monocypher's README: the sources "compile as C (since C99) and C++ (since C++98)". No C toolchain is added.
- Built without the project's warnings-as-errors flags; it is not our code to reformat.
- Covered by the C-04 no-float scan like the rest of `native/src/`.

## Verification in this repository

- `native/test/unit/crypto_test.cpp`: the RFC 8439 §2.8.2 AEAD test vector, byte for byte, through Monocypher directly (16-byte tag) and through `crypto/aead` (4-byte tag).
- HKDF-SHA-512 output checked against an independent implementation (.NET `HMACSHA512`, itself checked against RFC 4231 test case 2).

## Not a substitute for review

`packet-security-transport-spec.md` §6.9 requires this design to be reviewed against a known-good reference before it ships. Using a reviewed library does not discharge that requirement for how it is used here (4-byte tag, nonce derivation, KDF inputs).

To update: replace the files from a new release, record the new hashes here, and re-run `unit.crypto` and `conformance.c40_c43`. A different cipher or KDF output is a pairing-contract change (packet §8.3).
