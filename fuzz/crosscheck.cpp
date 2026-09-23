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

#if defined(__AVX512BW__)
constexpr std::string_view kArm = "AVX-512";
#elif defined(__AVX2__)
constexpr std::string_view kArm = "AVX2";
#elif defined(__ARM_NEON)
constexpr std::string_view kArm = "NEON";
#else
constexpr std::string_view kArm = "byte loop";
#endif

// Past two blocks of the widest form, so a refusal in the second and in
// the third block of 64 is asked for too.
constexpr size_t kLongestRun = 130;

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
    const http::NibbleTable &low_bits;
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
                const size_t floor = http::floor_run_length(text, table.allowed);
                if (http::allowed_run_length(text, table.allowed, table.low_bits) != floor)
                    missed++;
#if defined(__AVX2__)
                if (http::avx2_run_length(text, table.low_bits) != floor)
                    missed++;
#endif
#if defined(__AVX512BW__)
                if (http::avx512_run_length(text, table.low_bits) != floor)
                    missed++;
#endif
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

}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fputs("usage: crosscheck <corpus directory>\n", stderr);
        return 2;
    }

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
