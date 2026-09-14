// Second translation unit for context_test.cpp — "commit() is ONE function".
//
// Stands in for the other side of the link: the sender and the receiver are
// built as different translation units, and both must reach the same commit
// symbol. If commit were ever made static, inline-duplicated, or given a
// second variant, the address taken here would differ from the one taken in
// context_test.cpp.

#include "context/context.h"

namespace itantra_test {

using CommitFn = itantra::CommitResult (*)(itantra::Context&, const itantra::CommitPayload&) noexcept;

CommitFn commit_seen_from_another_translation_unit() {
    return &itantra::commit;
}

}  // namespace itantra_test
