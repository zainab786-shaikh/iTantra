# The single authoritative list of native source files.
#
# Two builds consume this file and neither owns it:
#
#   android-native/app/src/main/cpp/CMakeLists.txt   AGP entry point -> libitantra-native.so
#   native/CMakeLists.txt                            host entry point -> [H] tests
#
# They must compile the SAME sources or the host tests prove nothing about the
# bytes the phone produces, and C-01/C-03 become meaningless
# (validation-benchmark-contract §5.1, §3 "Host results do not substitute for
# device results" — but they do have to be testing the same code).
#
# Where the files live:
#
#   portable core   native/src/        implementation plan §0.2 native tree
#   JNI boundary    this directory
#
# ITANTRA_CPP_DIR is set by whichever entry point includes this file.
# ITANTRA_CORE_DIR is located relative to THIS file, so both entry points
# resolve identical absolute paths. It is also the core include root:
# `#include "common/bitio.h"`.

get_filename_component(ITANTRA_CORE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../../../../native/src" ABSOLUTE)

# ---------------------------------------------------------------------------
# Portable core. Compiles on host and on Android. No JNI, no Android headers.
# Paths relative to native/src. Every file here is covered by the C-04
# no-float build step (native/CMakeLists.txt).
# ---------------------------------------------------------------------------
set(ITANTRA_CORE_SOURCES
        common/bitio.cpp
        common/hash.cpp
        coder/coder.cpp
        coder/static_model.cpp
        packet/metadata.cpp
        packet/seq.cpp
        packet/assemble.cpp
        packet/parse.cpp
        crypto/aead.cpp
        crypto/kdf.cpp
        crypto/nonce.cpp
        crypto/replay.cpp
        context/context.cpp
        context/hash.cpp
        lang/json.cpp
        lang/languages.cpp
        lang/normalize.cpp
        lang/clause.cpp
        lang/lexicon.cpp
        lang/pack.cpp
        lang/extract.cpp
        lang/render.cpp
        tier2/subword.cpp
        tier2/ngram.cpp
        tier2/boost.cpp
        tier2/tables.cpp
        tier2/encode.cpp
        tier2/decode.cpp
        tier1/frame.cpp
        tier1/decode.cpp
        tier1/rules.cpp
        tier1/head.cpp
        tier1/adjacency.cpp
        tier1/slots.cpp
        tier1/readback.cpp
        tier1/encode.cpp
)

# ---------------------------------------------------------------------------
# Third-party, vendored unmodified under native/src/third_party/ (provenance
# in each directory's VENDOR.md). Portable, no JNI. Kept apart from
# ITANTRA_CORE_SOURCES so neither build holds vendored code to the project's
# own warning flags. Covered by the C-04 no-float scan all the same.
#
# Monocypher 4.0.2 — ChaCha20-Poly1305 and HKDF-SHA-512 (implementation plan
# Phase 4). Compiled as C++: its README states the sources "compile as C
# (since C99) and C++ (since C++98)", so no C toolchain is needed.
# ---------------------------------------------------------------------------
set(ITANTRA_THIRD_PARTY_SOURCES
        third_party/monocypher/monocypher.c
        third_party/monocypher/monocypher-ed25519.c
)

# ---------------------------------------------------------------------------
# JNI boundary. Android only — requires jni.h and android/log.h.
#
# Kept coarse: one call per clause, never per token or per symbol
# (IMPLEMENTATION-HANDOFF.md "JNI BOUNDARY").
# ---------------------------------------------------------------------------
set(ITANTRA_JNI_SOURCES
        itantra-native.cpp
)

list(TRANSFORM ITANTRA_CORE_SOURCES PREPEND "${ITANTRA_CORE_DIR}/")
list(TRANSFORM ITANTRA_JNI_SOURCES PREPEND "${ITANTRA_CPP_DIR}/")
list(TRANSFORM ITANTRA_THIRD_PARTY_SOURCES PREPEND "${ITANTRA_CORE_DIR}/")
set_source_files_properties(${ITANTRA_THIRD_PARTY_SOURCES} PROPERTIES LANGUAGE CXX)
