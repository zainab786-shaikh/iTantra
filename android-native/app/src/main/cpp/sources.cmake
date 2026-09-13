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
# Paths are relative to this directory. ITANTRA_CPP_DIR is set by whichever
# entry point includes this file.

# ---------------------------------------------------------------------------
# Portable core. Compiles on host and on Android. No JNI, no Android headers.
#
# Populated from Phase 1 onward:
#   common/  coder/  packet/  crypto/  context/
#   lang/    tier1/  tier2/   select/  receiver/
#
# Empty in Phase 0 by design — the implementation plan's Phase 0 is
# reconciliation only, "No new logic".
# ---------------------------------------------------------------------------
set(ITANTRA_CORE_SOURCES
        # (Phase 1 adds itantra/common/bitio.cpp, itantra/common/hash.cpp, ...)
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

list(TRANSFORM ITANTRA_CORE_SOURCES PREPEND "${ITANTRA_CPP_DIR}/")
list(TRANSFORM ITANTRA_JNI_SOURCES PREPEND "${ITANTRA_CPP_DIR}/")
