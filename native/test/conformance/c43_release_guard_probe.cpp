// C-43 host precondition probe — THIS FILE MUST NOT COMPILE.
//
// contract C-43 [D]: "Release build with ITANTRA_DISABLE_AEAD set → Compile
// error." C-43 itself runs on a device release build (Phase 11). This probe
// checks the guard in crypto/aead.h on the host: a translation unit that sees
// both NDEBUG (a release build) and ITANTRA_DISABLE_AEAD must fail with the
// guard's own message. CTest "C-43.precondition.release-guard" builds it and
// passes only on that message. It is excluded from the normal build.

#define NDEBUG
#define ITANTRA_DISABLE_AEAD
#include "crypto/aead.h"

int main() {
    return itantra::kAeadBypassed ? 1 : 0;
}
