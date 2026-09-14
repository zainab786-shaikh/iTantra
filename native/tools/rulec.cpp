// Rule compiler command line — tier §12.1. Implementation plan Phase 8.
//
//   itantra-rulec --common <dir> --rules <rules.tsv> --answers <answers.tsv> [--out <rules.bin>]
//
// Exits 1, printing the first violation, for a malformed table. The same
// library runs inside itantra-packc, so a malformed fixture fails the build.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "tier1/rulec/rulec.h"

int main(int argc, char** argv) {
    std::string common, rules, answers, out;
    for (int a = 1; a + 1 < argc; a += 2) {
        if (std::strcmp(argv[a], "--common") == 0) common = argv[a + 1];
        if (std::strcmp(argv[a], "--rules") == 0) rules = argv[a + 1];
        if (std::strcmp(argv[a], "--answers") == 0) answers = argv[a + 1];
        if (std::strcmp(argv[a], "--out") == 0) out = argv[a + 1];
    }
    if (common.empty() || rules.empty() || answers.empty()) {
        std::fprintf(stderr, "usage: itantra-rulec --common <dir> --rules <file> --answers <file> [--out <file>]\n");
        return 2;
    }
    const itantra::rulec::CompileResult r = itantra::rulec::compile(common, rules, answers);
    if (!r.ok) {
        std::printf("%s\n", r.error.c_str());
        return 1;
    }
    if (!out.empty()) {
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        for (itantra::u8 b : r.rules_bin) file.put(static_cast<char>(b));
        if (!file) {
            std::printf("rulec: cannot write %s\n", out.c_str());
            return 1;
        }
    }
    std::printf("rulec: OK  %u rules, %u answers\n", r.rule_count, r.answer_count);
    return 0;
}
