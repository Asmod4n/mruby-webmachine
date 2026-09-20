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

// The other half of the head. The names are scanned by parse_token and
// measured in scan_arms.cpp; the values are scanned by get_token_to_eol,
// which looks for the byte that ends the line.
//
// RFC 9110 5.5 says what may stand in a value:
//
//   field-vchar = VCHAR / obs-text
//   obs-text    = %x80-FF
//
// So 0x80 to 0xFF are allowed, and "a recipient SHOULD treat other
// allowed octets in field content (i.e., obs-text) as opaque data". The
// nibble table this tree uses for tchar cannot say that: its mask holds
// eight bits for sixteen high nibbles, so it is ASCII only and every byte
// above 0x7f is refused.
//
// It does not need to. The stop set here is not a scattered set of
// characters, it is a range: below SP except HT, plus DEL. AVX2 compares
// ranges without any table, and unsigned comparison leaves 0x80 to 0xFF
// allowed by itself.
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

// Where each field value begins, which is where a scan for the end of the
// line starts.
std::vector<size_t> value_starts_of(const std::string_view whole)
{
    std::vector<size_t> starts;
    size_t at = whole.find("\r\n") + 2;
    while (at + 1 < whole.size() && whole.substr(at, 2) != "\r\n") {
        size_t value = whole.find(':', at) + 1;
        while (value < whole.size() && (whole.at(value) == ' ' || whole.at(value) == '\t'))
            value++;
        starts.push_back(value);
        at = whole.find("\r\n", at) + 2;
    }
    return starts;
}

const std::vector<size_t> kValueStarts =
    value_starts_of(std::string_view(kHead).substr(0, kHeadSize));

size_t value_bytes_of(const std::string_view whole, const std::vector<size_t> &starts)
{
    size_t sum = 0;
    for (const size_t start : starts)
        sum += whole.find("\r\n", start) - start;
    return sum;
}

const size_t kValueBytes = value_bytes_of(std::string_view(kHead).substr(0, kHeadSize),
                                          kValueStarts);

#ifdef __SSE4_2__
// get_token_to_eol's own ranges, copied from picohttpparser.
alignas(16) const char kEolStopRanges[16] = "\0\010"     /* allow HT */
                                            "\012\037"   /* allow SP and up to but not DEL */
                                            "\177\177";  /* allow chars w. MSB set */

size_t first_stop_pcmpestri(const std::string_view text)
{
    const __m128i ranges = _mm_loadu_si128(reinterpret_cast<const __m128i *>(kEolStopRanges));
    size_t at = 0;
    for (; at + 16 <= text.size(); at += 16) {
        const __m128i bytes =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(std::next(text.data(), at)));
        const int found = _mm_cmpestri(ranges, 6, bytes, 16,
                                       _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES |
                                           _SIDD_UBYTE_OPS);
        if (found != 16) {
            at += static_cast<size_t>(found);
            break;
        }
    }
    for (; at < text.size(); at++) {
        const unsigned char byte = static_cast<unsigned char>(text.at(at));
        if ((byte < 0x20 && byte != '\t') || byte == 0x7f)
            return at;
    }
    return text.size();
}
#endif

#if defined(__AVX2__)
// Refused: below SP and not HT, or DEL. _mm256_max_epu8 answers the
// unsigned "at most" that _mm256_cmpgt_epi8 cannot, and that is what
// leaves obs-text alone.
uint32_t avx2_value_refusals(const char *at)
{
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(at));
    const __m256i below_space =
        _mm256_cmpeq_epi8(_mm256_max_epu8(bytes, _mm256_set1_epi8(0x1f)),
                          _mm256_set1_epi8(0x1f));
    const __m256i horizontal_tab = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8('\t'));
    const __m256i delete_byte = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(0x7f));
    const __m256i refused = _mm256_or_si256(
        _mm256_andnot_si256(horizontal_tab, below_space), delete_byte);
    return static_cast<uint32_t>(_mm256_movemask_epi8(refused));
}

size_t first_stop_range(const std::string_view text)
{
    for (size_t at = 0;; at += 32) {
        const uint32_t refused = avx2_value_refusals(std::next(text.data(), at));
        if (refused != 0)
            return at + static_cast<size_t>(std::countr_zero(refused));
    }
}
#endif

void scan_values_pcmpestri(benchmark::State &state)
{
#ifdef __SSE4_2__
    const std::string_view head(kHead.data(), kHead.size());
    for (auto _ : state) {
        size_t sum = 0;
        for (const size_t start : kValueStarts)
            sum += first_stop_pcmpestri(head.substr(start));
        benchmark::DoNotOptimize(sum);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kValueBytes));
#else
    state.SkipWithError("no SSE4.2");
#endif
}

void scan_values_range(benchmark::State &state)
{
#if defined(__AVX2__)
    const std::string_view head(kHead.data(), kHead.size());
    for (auto _ : state) {
        size_t sum = 0;
        for (const size_t start : kValueStarts)
            sum += first_stop_range(head.substr(start));
        benchmark::DoNotOptimize(sum);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kValueBytes));
#else
    state.SkipWithError("no AVX2");
#endif
}

BENCHMARK(scan_values_pcmpestri);
BENCHMARK(scan_values_range);

} // namespace
