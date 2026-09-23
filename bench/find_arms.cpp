#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#if !(defined(__clang__) && defined(__AVX512BW__))
// libstdc++ 16 asserts on char lanes in simd_x86.h when clang builds
// with AVX-512 (is_same_v<char, signed char>), so that pairing has no
// std::simd arm, and the table says so.
#if !(defined(__clang__) && defined(__AVX512BW__))
#define WITH_STD_SIMD 1
#include <experimental/simd>
#endif
#endif
#include <string_view>

#include <immintrin.h>

namespace
{

constexpr std::string_view kAskedLikeHtgen = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";

constexpr std::string_view kAskedLikeABrowser =
    "GET /articles/42?param=xyz&foo=bar HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/140.0.0.0 Safari/537.36\r\n"
    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,"
    "image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n"
    "Accept-Language: en-US,en;q=0.9,de;q=0.8\r\n"
    "Accept-Encoding: gzip, deflate, br, zstd\r\n"
    "Connection: keep-alive\r\n"
    "Upgrade-Insecure-Requests: 1\r\n"
    "Sec-Fetch-Dest: document\r\n"
    "Sec-Fetch-Mode: navigate\r\n"
    "Sec-Fetch-Site: none\r\n"
    "Sec-Fetch-User: ?1\r\n"
    "\r\n";

size_t
by_the_standard_library(const std::string_view text, const size_t from, const char wanted)
{
    return text.find(wanted, from);
}

size_t
by_a_loop_the_compiler_vectorizes(const std::string_view text, const size_t from, const char wanted)
{
    for (size_t at = from; at < text.size(); at++)
        if (text[at] == wanted) return at;
    return std::string_view::npos;
}

// libstdc++ 16 asserts on char lanes in simd_x86.h when clang builds with
// AVX-512, so that pairing has no std::simd arm.
#if !(defined(__clang__) && defined(__AVX512BW__))
#define WITH_STD_SIMD 1
#if defined(WITH_STD_SIMD)
size_t
by_std_simd(const std::string_view text, const size_t from, const char wanted)
{
    namespace stdx = std::experimental;
    using Bytes = stdx::native_simd<char>;
    size_t at = from;
    for (; at + Bytes::size() <= text.size(); at += Bytes::size()) {
        const Bytes block(text.data() + at, stdx::element_aligned);
        const auto found = block == wanted;
        if (stdx::any_of(found)) return at + static_cast<size_t>(stdx::find_first_set(found));
    }
    if (at >= text.size()) return std::string_view::npos;
    if (text.size() < Bytes::size()) return text.find(wanted, at);
    const size_t base = text.size() - Bytes::size();
    const Bytes block(text.data() + base, stdx::element_aligned);
    const Bytes lane([](const auto i) { return static_cast<char>(i); });
    const auto found = block == wanted && lane >= static_cast<char>(at - base);
    return stdx::any_of(found) ? base + static_cast<size_t>(stdx::find_first_set(found)) : std::string_view::npos;
}
#endif
#endif

#if defined(__AVX2__)
size_t
by_avx2(const std::string_view text, const size_t from, const char wanted)
{
    const __m256i needle = _mm256_set1_epi8(wanted);
    size_t at = from;
    for (; at + 32 <= text.size(); at += 32) {
        const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(text.data() + at));
        const uint32_t found = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(block, needle)));
        if (found != 0) return at + static_cast<size_t>(std::countr_zero(found));
    }
    if (at >= text.size()) return std::string_view::npos;
    if (text.size() < 32) return text.find(wanted, at);
    const size_t base = text.size() - 32;
    const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(text.data() + base));
    const uint32_t found = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(block, needle))) &
                           (~uint32_t{0} << (at - base));
    return found != 0 ? base + static_cast<size_t>(std::countr_zero(found)) : std::string_view::npos;
}
#endif

#if defined(__AVX512BW__)
size_t
by_avx512(const std::string_view text, const size_t from, const char wanted)
{
    const __m512i needle = _mm512_set1_epi8(wanted);
    size_t at = from;
    for (; at + 64 <= text.size(); at += 64) {
        const __m512i block = _mm512_loadu_si512(text.data() + at);
        const uint64_t found = _mm512_cmpeq_epi8_mask(block, needle);
        if (found != 0) return at + static_cast<size_t>(std::countr_zero(found));
    }
    if (at >= text.size()) return std::string_view::npos;
    if (text.size() < 64) {
        const __mmask64 inside = at >= text.size() ? 0 : (~uint64_t{0} >> (64 - (text.size() - at)));
        const __m512i block = _mm512_maskz_loadu_epi8(inside, text.data() + at);
        const uint64_t found = _mm512_mask_cmpeq_epi8_mask(inside, block, needle);
        return found != 0 ? at + static_cast<size_t>(std::countr_zero(found)) : std::string_view::npos;
    }
    const size_t base = text.size() - 64;
    const __m512i block = _mm512_loadu_si512(text.data() + base);
    const uint64_t found = _mm512_cmpeq_epi8_mask(block, needle) & (~uint64_t{0} << (at - base));
    return found != 0 ? base + static_cast<size_t>(std::countr_zero(found)) : std::string_view::npos;
}
#endif

template <size_t (*Find)(std::string_view, size_t, char)>
size_t
every_line_and_colon(const std::string_view asked)
{
    size_t sum = 0;
    size_t at = 0;
    for (;;) {
        const size_t ends = Find(asked, at, '\n');
        if (ends == std::string_view::npos) return sum;
        sum += ends;
        const std::string_view line = asked.substr(at, ends - at);
        const size_t colon = Find(line, 0, ':');
        if (colon != std::string_view::npos) sum += colon;
        at = ends + 1;
    }
}

template <size_t (*Find)(std::string_view, size_t, char)>
void
the_same_as_the_floor_or_abort(const std::string_view asked)
{
    for (size_t from = 0; from <= asked.size(); from++)
        for (const char wanted : {'\n', ':', ' ', 'z', '\0'})
            if (Find(asked, from, wanted) != by_the_standard_library(asked, from, wanted)) [[unlikely]] std::abort();
}

template <size_t (*Find)(std::string_view, size_t, char)>
void
parsed(benchmark::State &state, const std::string_view asked)
{
    the_same_as_the_floor_or_abort<Find>(kAskedLikeHtgen);
    the_same_as_the_floor_or_abort<Find>(kAskedLikeABrowser);
    for (auto _ : state) {
        std::string_view text = asked;
        benchmark::DoNotOptimize(text);
        size_t sum = every_line_and_colon<Find>(text);
        benchmark::DoNotOptimize(sum);
    }
}

// One kind of test per binary: FIND_TEST picks it.
//   1  the request htgen sends, 42 bytes
//   2  the request a browser sends, about 700 bytes
#if FIND_TEST == 1
#define ARM(name, Find) void name(benchmark::State &state) { parsed<Find>(state, kAskedLikeHtgen); }
#elif FIND_TEST == 2
#define ARM(name, Find) void name(benchmark::State &state) { parsed<Find>(state, kAskedLikeABrowser); }
#endif

// Built without FIND_TEST, as rake bench builds every file, it holds no arm.
#if defined(ARM)
ARM(find_standard_library, by_the_standard_library)
BENCHMARK(find_standard_library);
ARM(find_loop, by_a_loop_the_compiler_vectorizes)
BENCHMARK(find_loop);
#if defined(WITH_STD_SIMD)
ARM(find_std_simd, by_std_simd)
BENCHMARK(find_std_simd);
#endif
#if defined(__AVX2__)
ARM(find_avx2, by_avx2)
BENCHMARK(find_avx2);
#endif
#if defined(__AVX512BW__)
ARM(find_avx512, by_avx512)
BENCHMARK(find_avx512);
#endif
#endif

}
