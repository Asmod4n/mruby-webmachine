// What another architecture answers, without another machine.
//
// allowed_run_length has three arms. AVX2 reads thirty two bytes at a
// time, NEON reads sixteen and takes its answer out of a shift-and-narrow
// that packs four bits per byte, and the fallback is a byte loop. Only one
// of the three is compiled on any machine, so a build here never touches
// the NEON arm and a corpus grown here never reaches its block boundaries.
//
// Cross compile this and run it under qemu and the arm that no machine
// here has meets the same two questions:
//
//   - Does the wide scan find the byte the byte loop finds, for every
//     table, at every length, at every position, for all 256 values.
//   - Does every oracle in fuzz_http.cpp still hold over the corpus.
//
// It answers correctness and nothing else. qemu translates instructions
// and models no pipeline, so a time taken here is not a time, and no row
// of bench/results is ever made this way.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace
{

#if defined(__AVX2__)
constexpr std::string_view kArm = "AVX2";
#elif defined(__ARM_NEON)
constexpr std::string_view kArm = "NEON";
#else
constexpr std::string_view kArm = "byte loop";
#endif

constexpr size_t kLongestRun = 40;

size_t scalar_run_length(const std::string_view text, const std::array<bool, 256> &allowed)
{
    const auto found = std::ranges::find_if_not(text, [&allowed](const char letter) {
        return allowed.at(static_cast<unsigned char>(letter));
    });
    return static_cast<size_t>(std::distance(text.begin(), found));
}

char first_allowed_byte(const std::array<bool, 256> &allowed)
{
    for (unsigned value = 0; value < 256; value++)
        if (allowed.at(value))
            return static_cast<char>(value);
    return 'a';
}

struct Table {
    std::string_view name;
    const std::array<bool, 256> &allowed;
    const std::array<unsigned char, 16> &low_bits;
};

long refusals_the_wide_scan_missed(const Table &table)
{
    long missed = 0;
    const char filler = first_allowed_byte(table.allowed);
    for (size_t length = 0; length <= kLongestRun; length++) {
        std::string held(length + http::kWidePadding, filler);
        for (size_t at = 0; at < length; at++) {
            for (unsigned value = 0; value < 256; value++) {
                held.at(at) = static_cast<char>(value);
                const std::string_view text(held.data(), length);
                if (http::allowed_run_length(text, table.allowed, table.low_bits) !=
                    scalar_run_length(text, table.allowed))
                    missed++;
            }
            held.at(at) = filler;
        }
        const std::string_view whole(held.data(), length);
        if (http::allowed_run_length(whole, table.allowed, table.low_bits) != length)
            missed++;
    }
    return missed;
}

long walk_the_tables()
{
    const std::vector<Table> tables = {
        {"tchar", http::kTchar, http::kTcharLowBits},
        {"lowercase-tchar", http::kLowercaseTchar, http::kLowercaseTcharLowBits},
        {"reg-name", http::kRegName, http::kRegNameLowBits},
        {"path-byte", http::kPathByte, http::kPathByteLowBits},
        {"query-byte", http::kQueryByte, http::kQueryByteLowBits},
    };
    long missed = 0;
    for (const Table &table : tables) {
        const long wrong = refusals_the_wide_scan_missed(table);
        std::printf("  %-16s %s\n", table.name.data(), wrong == 0 ? "agrees" : "DISAGREES");
        missed += wrong;
    }
    return missed;
}

size_t walk_the_corpus(const std::filesystem::path &corpus)
{
    size_t walked = 0;
    for (const auto &entry : std::filesystem::directory_iterator(corpus)) {
        if (!entry.is_regular_file())
            continue;
        std::ifstream file(entry.path(), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
        walked++;
    }
    return walked;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fputs("usage: crosscheck <corpus directory>\n", stderr);
        return 2;
    }
    // A corpus input that breaks an oracle aborts inside the harness, and
    // a static binary under qemu keeps its stdout to itself when it does.
    // So what the tables answered is flushed before the corpus is walked.
    std::printf("the wide scan of this build is %s\n", kArm.data());
    const long missed = walk_the_tables();
    std::fflush(stdout);
    if (missed != 0) {
        std::fputs("the tables and the byte loop do not answer the same\n", stderr);
        return 1;
    }
    const size_t walked = walk_the_corpus(argv[1]);
    std::printf("%zu corpus inputs walked, every oracle held\n", walked);
    return 0;
}
