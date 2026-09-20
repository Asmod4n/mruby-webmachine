#include <benchmark/benchmark.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

#ifdef __SSE4_2__
#include <x86intrin.h>
#endif

// The 91 percent. A head of thirteen fields parses in 233 ns and the
// whole field lookup is 22.6 of that, so the question that decides
// whether a parser of ours can be faster is this one: how fast does the
// scan find the byte that ends a run.
//
// picohttpparser finds it with SSE4.2 _mm_cmpestri over eight ranges,
// sixteen bytes at a time, and then walks the tail byte by byte because
// pcmpestri takes no more than eight ranges and tchar needs more.
//
// This tree finds it with an AVX2 nibble table - _mm256_shuffle_epi8
// twice and a movemask - thirty two bytes at a time, and the mask names
// the first refused byte with std::countr_zero. It is exact: no tail
// loop confirms it. The load reads thirty two bytes whatever the run is
// long, which picohttpparser may not do and this tree may, because the
// ring leaves kWidePadding free behind every byte of its pool.
namespace
{

const std::string kHead = std::string("GET /index.html HTTP/1.1\r\n"
                                      "Host: www.example.com\r\n"
                                      "Connection: keep-alive\r\n"
                                      "sec-ch-ua: \"Chromium\";v=\"131\", \"Not_A Brand\";v=\"24\"\r\n"
                                      "sec-ch-ua-mobile: ?0\r\n"
                                      "sec-ch-ua-platform: \"Linux\"\r\n"
                                      "Upgrade-Insecure-Requests: 1\r\n"
                                      "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36\r\n"
                                      "Accept: text/html,application/xhtml+xml;q=0.9\r\n"
                                      "Sec-Fetch-Site: none\r\n"
                                      "Sec-Fetch-Mode: navigate\r\n"
                                      "Accept-Encoding: gzip, deflate, br, zstd\r\n"
                                      "Accept-Language: en-US,en;q=0.9\r\n"
                                      "\r\n") +
                          std::string(http::kWidePadding, '\0');

const size_t kHeadSize = kHead.size() - http::kWidePadding;

// Where each field name begins, which is where a scan for the colon
// starts. Thirteen short runs is the pattern a head really has.
std::vector<size_t> name_starts_of(const std::string_view whole)
{
    std::vector<size_t> starts;
    size_t at = whole.find("\r\n") + 2;
    while (at + 1 < whole.size() && whole.substr(at, 2) != "\r\n") {
        starts.push_back(at);
        at = whole.find("\r\n", at) + 2;
    }
    return starts;
}

const std::vector<size_t> kNameStarts = name_starts_of(std::string_view(kHead).substr(0, kHeadSize));

#ifdef __SSE4_2__
// picohttpparser's own ranges for a field name, copied from parse_token.
// `|` and `~` fall through them, which is why the caller must still walk
// the bytes pcmpestri stopped before.
alignas(16) const char kTokenStopRanges[] = "\x00 "  /* control chars and up to SP */
                                            "\"\""   /* 0x22 */
                                            "()"     /* 0x28,0x29 */
                                            ",,"     /* 0x2c */
                                            "//"     /* 0x2f */
                                            ":@"     /* 0x3a-0x40 */
                                            "[]"     /* 0x5b-0x5d */
                                            "{\xff"; /* 0x7b-0xff */

size_t first_stop_pcmpestri(const std::string_view text)
{
    const __m128i ranges = _mm_loadu_si128(reinterpret_cast<const __m128i *>(kTokenStopRanges));
    const size_t range_size = sizeof(kTokenStopRanges) - 1;
    size_t at = 0;
    for (; at + 16 <= text.size(); at += 16) {
        const __m128i bytes =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(std::next(text.data(), at)));
        const int found = _mm_cmpestri(ranges, static_cast<int>(range_size), bytes, 16,
                                       _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES |
                                           _SIDD_UBYTE_OPS);
        if (found != 16) {
            at += static_cast<size_t>(found);
            break;
        }
    }
    // parse_token's own while(1): pcmpestri stops on `|` and `~` as well,
    // which are tchar, so the byte loop has the last word. Leaving it out
    // makes this arm faster than picohttpparser and wrong.
    for (; at < text.size(); at++)
        if (!http::is_tchar(text.at(at)))
            return at;
    return text.size();
}
#endif

#if defined(__AVX2__)
size_t first_stop_nibble(const std::string_view text)
{
    const __m256i low_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(http::kTcharLowBits.data())));
    const __m256i high_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(http::kHighNibbleBit.data())));
    size_t at = 0;
    for (;; at += 32) {
        const uint32_t refused =
            http::avx2_block_refusals(std::next(text.data(), at), low_table, high_table);
        if (refused != 0)
            return at + static_cast<size_t>(std::countr_zero(refused));
    }
}
#endif

// A run of 512 tchars, then the padding, whose zero byte is not a tchar
// and stops both scanners. The head itself cannot answer the throughput
// question: its first space stands at byte three.
const std::string kLongToken = std::string(512, 'a') + std::string(http::kWidePadding, '\0');

// A scan of one long run. That is the throughput question.
void scan_block_pcmpestri(benchmark::State &state)
{
#ifdef __SSE4_2__
    const std::string_view run(kLongToken);
    for (auto _ : state) {
        size_t got = first_stop_pcmpestri(run);
        benchmark::DoNotOptimize(got);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * 512));
#else
    state.SkipWithError("no SSE4.2");
#endif
}

void scan_block_nibble(benchmark::State &state)
{
#if defined(__AVX2__)
    const std::string_view run(kLongToken);
    for (auto _ : state) {
        size_t got = first_stop_nibble(run);
        benchmark::DoNotOptimize(got);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * 512));
#else
    state.SkipWithError("no AVX2");
#endif
}

// Thirteen short runs, one per field name, each stopping at its colon.
// This is the shape a head really has, and it is where a long latency
// instruction is paid thirteen times over.
void scan_names_pcmpestri(benchmark::State &state)
{
#ifdef __SSE4_2__
    const std::string_view head(kHead.data(), kHead.size());
    for (auto _ : state) {
        size_t sum = 0;
        for (const size_t start : kNameStarts)
            sum += first_stop_pcmpestri(head.substr(start));
        benchmark::DoNotOptimize(sum);
    }
#else
    state.SkipWithError("no SSE4.2");
#endif
}

void scan_names_nibble(benchmark::State &state)
{
#if defined(__AVX2__)
    const std::string_view head(kHead.data(), kHead.size());
    for (auto _ : state) {
        size_t sum = 0;
        for (const size_t start : kNameStarts)
            sum += first_stop_nibble(head.substr(start));
        benchmark::DoNotOptimize(sum);
    }
#else
    state.SkipWithError("no AVX2");
#endif
}

BENCHMARK(scan_block_pcmpestri);
BENCHMARK(scan_block_nibble);
BENCHMARK(scan_names_pcmpestri);
BENCHMARK(scan_names_nibble);

} // namespace
