// C-04 — static check: zero floating point in the deterministic core.
//
// validation-benchmark-contract §5.1 C-04: no float or double in encode,
// decode, model or threshold paths; pass criterion "zero occurrences". "C-04 is
// automatable as a build step. Make it one." Implementation plan Phase 1:
// a check in CMake that fails the build, not a manual one.
//
// A plain grep trips on comments ("no float here") and strings, and misses
// literals like 1e3 or a std::sqrt(n) result bound to `auto`. So this is a
// small lexer: comments and string, character and raw-string literals are
// skipped, and what remains is checked for
//
//   type names       float  double  float_t  double_t
//   literals         1.5  .5  1.  1e3  2E-3  1.0f  0x1p4
//   headers          <cmath> <math.h> <cfloat> <float.h> <ctgmath> <tgmath.h>
//   std math calls   std::sqrt, std::log2, std::pow, ...  (floating results)
//
// Usage:   c04_no_float <file>...     0 clean · 1 violations · 2 usage/I/O
//          c04_no_float --self-test   verifies the checker itself

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Finding {
    unsigned    line;
    std::string what;
};

const char* const kForbiddenIdents[] = {"float", "double", "float_t", "double_t"};

const char* const kForbiddenHeaders[] = {"cmath", "math.h", "cfloat", "float.h",
                                         "ctgmath", "tgmath.h"};

const char* const kStdMath[] = {
    "sqrt", "cbrt", "pow", "exp", "exp2", "expm1", "log", "log2", "log10",
    "log1p", "floor", "ceil", "round", "lround", "llround", "trunc", "fmod",
    "remainder", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh",
    "cosh", "tanh", "hypot", "nan", "isnan", "isinf", "isfinite", "fabs",
    "ldexp", "frexp", "modf", "lerp",
};

bool ident_start(char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool ident_char(char c) {
    return ident_start(c) || (c >= '0' && c <= '9');
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

template <std::size_t N>
bool in_list(const std::string& s, const char* const (&list)[N]) {
    for (const char* w : list) {
        if (s == w) return true;
    }
    return false;
}

// True if the identifier starting at `start` is written as `std::name`.
bool qualified_by_std(const std::string& src, std::size_t start) {
    std::size_t i = start;
    auto skip_space_back = [&]() {
        while (i > 0u && (src[i - 1u] == ' ' || src[i - 1u] == '\t' ||
                          src[i - 1u] == '\r' || src[i - 1u] == '\n')) {
            --i;
        }
    };
    skip_space_back();
    if (i < 2u || src[i - 1u] != ':' || src[i - 2u] != ':') return false;
    i -= 2u;
    skip_space_back();
    if (i < 3u || src.compare(i - 3u, 3u, "std") != 0) return false;
    return i == 3u || !ident_char(src[i - 4u]);
}

// Skips a "..." or '...' literal starting at `i`. Returns the index after it.
std::size_t skip_quoted(const std::string& src, std::size_t i, char quote) {
    const std::size_t n = src.size();
    ++i;
    while (i < n && src[i] != quote && src[i] != '\n') {
        i += (src[i] == '\\') ? 2u : 1u;
    }
    return (i < n && src[i] == quote) ? i + 1u : i;
}

// Skips a raw string whose opening quote is at `i`. Returns the index after it.
std::size_t skip_raw_string(const std::string& src, std::size_t i, unsigned& line) {
    const std::size_t n = src.size();
    const std::size_t open = src.find('(', i + 1u);
    if (open == std::string::npos) return n;
    const std::string terminator = ")" + src.substr(i + 1u, open - i - 1u) + "\"";
    const std::size_t close = src.find(terminator, open + 1u);
    const std::size_t end = (close == std::string::npos) ? n : close + terminator.size();
    for (std::size_t k = i; k < end; ++k) {
        if (src[k] == '\n') ++line;
    }
    return end;
}

std::vector<Finding> scan(const std::string& src) {
    std::vector<Finding> out;
    const std::size_t n = src.size();
    auto at = [&](std::size_t k) -> char { return k < n ? src[k] : '\0'; };

    std::size_t i = 0u;
    unsigned line = 1u;
    bool at_line_start = true;

    while (i < n) {
        const char c = src[i];

        if (c == '\n') {
            ++line;
            ++i;
            at_line_start = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }

        // Comments.
        if (c == '/' && at(i + 1u) == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && at(i + 1u) == '*') {
            i += 2u;
            while (i < n && !(src[i] == '*' && at(i + 1u) == '/')) {
                if (src[i] == '\n') ++line;
                ++i;
            }
            i = (i < n) ? i + 2u : n;
            continue;
        }

        const bool directive = at_line_start && c == '#';
        at_line_start = false;

        // #include of a floating-point header. Other directives fall through,
        // so `#define K 0.25` is still caught by the literal check.
        if (directive) {
            std::size_t j = i + 1u;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) ++j;
            std::size_t k = j;
            while (k < n && ident_char(src[k])) ++k;
            if (src.compare(j, k - j, "include") == 0) {
                std::size_t e = k;
                while (e < n && src[e] != '\n') ++e;
                const std::string rest = src.substr(k, e - k);
                for (const char* h : kForbiddenHeaders) {
                    if (rest.find(std::string("<") + h + ">") != std::string::npos ||
                        rest.find(std::string("\"") + h + "\"") != std::string::npos) {
                        out.push_back({line, std::string("#include <") + h + ">"});
                    }
                }
                i = e;
                continue;
            }
            ++i;
            continue;
        }

        // String and character literals.
        if (c == '"' || c == '\'') {
            i = skip_quoted(src, i, c);
            continue;
        }

        // Identifiers, including raw-string prefixes.
        if (ident_start(c)) {
            const std::size_t start = i;
            while (i < n && ident_char(src[i])) ++i;
            const std::string id = src.substr(start, i - start);

            if (at(i) == '"' && (id == "R" || id == "u8R" || id == "uR" ||
                                 id == "UR" || id == "LR")) {
                i = skip_raw_string(src, i, line);
                continue;
            }
            if (in_list(id, kForbiddenIdents)) {
                out.push_back({line, "'" + id + "'"});
            } else if (in_list(id, kStdMath) && qualified_by_std(src, start)) {
                out.push_back({line, "'std::" + id + "'"});
            }
            continue;
        }

        // Numeric literals (preprocessing numbers).
        if (is_digit(c) || (c == '.' && is_digit(at(i + 1u)))) {
            const std::size_t start = i;
            const bool hex = c == '0' && (at(i + 1u) == 'x' || at(i + 1u) == 'X');
            bool floating = false;
            bool ud_suffix = false;
            if (hex) i += 2u;

            while (i < n) {
                const char d = src[i];
                const bool exponent_char = hex ? (d == 'p' || d == 'P') : (d == 'e' || d == 'E');

                if (d == '\'' && ident_char(at(i + 1u))) {   // digit separator
                    i += 2u;
                    continue;
                }
                if (!ud_suffix && (d == 'e' || d == 'E' || d == 'p' || d == 'P') &&
                    (at(i + 1u) == '+' || at(i + 1u) == '-')) {
                    if (exponent_char) floating = true;
                    i += 2u;
                    continue;
                }
                if (d == '.') {
                    if (!ud_suffix) floating = true;
                    ++i;
                    continue;
                }
                if (ident_char(d)) {
                    if (d == '_') {
                        ud_suffix = true;
                    } else if (!ud_suffix && exponent_char) {
                        floating = true;
                    }
                    ++i;
                    continue;
                }
                break;
            }
            if (floating) {
                out.push_back({line, "floating literal '" + src.substr(start, i - start) + "'"});
            }
            continue;
        }

        ++i;
    }
    return out;
}

int check_files(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "C-04 ERROR: no files given. An empty file list would pass "
                             "vacuously; the build must supply the core sources.\n");
        return 2;
    }
    std::size_t total = 0u;
    for (int a = 1; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "C-04 ERROR: cannot read %s\n", argv[a]);
            return 2;
        }
        const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        for (const Finding& x : scan(src)) {
            std::fprintf(stderr, "%s(%u): error C-04: floating point: %s\n",
                         argv[a], x.line, x.what.c_str());
            ++total;
        }
    }
    if (total != 0u) {
        std::fprintf(stderr, "C-04 FAIL: %zu floating-point occurrence(s) in the deterministic core\n",
                     total);
        return 1;
    }
    std::printf("C-04 PASS: %d file(s), zero floating-point occurrences\n", argc - 1);
    return 0;
}

int self_test() {
    struct Case {
        const char* src;
        bool        violation;
    };
    const Case cases[] = {
        // must be caught
        {"float x;", true},
        {"double y = 0;", true},
        {"long double z;", true},
        {"float_t a;", true},
        {"auto a = 1.5;", true},
        {"auto b = 1e3;", true},
        {"auto c = .5f;", true},
        {"auto d = 0x1p4;", true},
        {"auto e = 2E-3;", true},
        {"auto f = 1.;", true},
        {"#include <cmath>", true},
        {"  #  include <float.h>", true},
        {"#include \"math.h\"", true},
        {"auto g = std::sqrt(4);", true},
        {"auto h = std :: log2 (8);", true},
        {"#define K 0.25", true},
        {"u32 n = static_cast<u32>(std::floor(x));", true},
        // must not be caught
        {"// float in a line comment", false},
        {"/* double\n 1.5 */ int x;", false},
        {"const char* s = \"float 1.5 double\";", false},
        {"const char* r = R\"x(double 2.0 \")x\";", false},
        {"char q = '\\''; int k = 1;", false},
        {"int h = 0xE5;", false},
        {"int h2 = 0x1E+5;", false},
        {"u32 m = 0xFFFFFFFFu;", false},
        {"int i = 1'000'000;", false},
        {"int j = 0b1010;", false},
        {"char dot = '.';", false},
        {"int floating = 3; int doubled = 2; int double_quote = 1;", false},
        {"int log2 = 1; my::sqrt(4); log(msg); ::mystd::pow(2);", false},
        {"auto m = 7_meters;", false},
        {"#include \"common/types.h\"", false},
        {"#include <cstdint>", false},
        {"void f(int...); a[0].b = 2;", false},
    };

    int failures = 0;
    for (const Case& c : cases) {
        const bool found = !scan(c.src).empty();
        if (found != c.violation) {
            std::printf("[FAIL] expected %s: %s\n", c.violation ? "violation" : "clean", c.src);
            ++failures;
        }
    }

    // Line numbers are reported correctly across comments and raw strings.
    const std::vector<Finding> lined = scan("int a;\n/*\n*/\nconst char* r = R\"(\n)\";\nfloat b;\n");
    if (lined.size() != 1u || lined[0].line != 6u) {
        std::printf("[FAIL] line numbering\n");
        ++failures;
    }

    std::printf("C-04 self-test: %zu cases, %d failed\n", sizeof(cases) / sizeof(cases[0]) + 1u,
                failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) return self_test();
    return check_files(argc, argv);
}
