#include <benchmark/benchmark.h>

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

// How long a run of allowed bytes is, every form in one binary: the
// standard library form that is the floor, AVX2, AVX-512, AVX-512 with a
// masked load, and AVX2 for the first 32 bytes then AVX-512. The runs are
// the field names a browser sends, and tokens of a fixed length, to find
// where the wider form starts to pay.

namespace
{

struct Padded {
    std::string held;
    size_t length;

    explicit Padded(const std::string_view text) : held(text), length(text.size())
    {
        held.append(http::kWidePadding, '\0');
    }

    [[nodiscard]] std::string_view text() const { return std::string_view(held.data(), length); }
};

const std::vector<Padded> &
field_names()
{
    static const std::vector<Padded> every = [] {
        std::vector<Padded> names;
        for (const std::string_view name :
             {"Host", "User-Agent", "Accept", "Accept-Language", "Accept-Encoding", "Connection",
              "Upgrade-Insecure-Requests", "Sec-Fetch-Dest", "Sec-Fetch-Mode", "Sec-Fetch-Site", "Sec-Fetch-User",
              "Cookie", "If-None-Match", "Cache-Control", "Referer", "Priority"})
            names.emplace_back(name);
        return names;
    }();
    return every;
}

const Padded kPath("/articles/42/comments/7/replies");
const Padded kLongQuery("/search?q=" + std::string(400, 'x') + "&page=2&sort=added&filter=open");

// A token of the given length, every byte a tchar, so the run is the whole
// view and a form reads it to its end.
Padded
token_of(const size_t length)
{
    constexpr std::string_view kTchars = "abcdefghijklmnopqrstuvwxyz0123456789-_.~!#$%&'*+^`|ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string token;
    for (size_t at = 0; at < length; at++)
        token += kTchars.at(at % kTchars.size());
    return Padded(token);
}

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
size_t
all_names(const std::vector<Padded> &names)
{
    size_t sum = 0;
    for (const Padded &name : names)
        sum += Run(name.text(), http::kTchar, http::kTcharLowBits);
    return sum;
}

// The same loop, which the compiler may not unroll: clang -O3 made one
// copy of the byte loop per name and read slower than -Os, which made one.
template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
size_t
all_names_not_unrolled(const std::vector<Padded> &names)
{
    size_t sum = 0;
#pragma GCC unroll 1
    for (const Padded &name : names)
        sum += Run(name.text(), http::kTchar, http::kTcharLowBits);
    return sum;
}

size_t
by_the_floor(const std::string_view text, const std::array<bool, 256> &allowed, const http::NibbleTable &)
{
    return http::floor_run_length(text, allowed);
}

#if defined(__AVX2__)
size_t
by_avx2(const std::string_view text, const std::array<bool, 256> &, const http::NibbleTable &low_bits)
{
    return http::avx2_run_length(text, low_bits);
}
#endif

#if defined(__AVX512BW__)
size_t
by_avx512_masked(const std::string_view text, const std::array<bool, 256> &, const http::NibbleTable &low_bits)
{
    return http::avx512_masked_run_length(text, low_bits);
}

size_t
by_avx2_then_avx512(const std::string_view text, const std::array<bool, 256> &, const http::NibbleTable &low_bits)
{
    return http::avx2_then_avx512_run_length(text, low_bits);
}

size_t
by_avx512(const std::string_view text, const std::array<bool, 256> &, const http::NibbleTable &low_bits)
{
    return http::avx512_run_length(text, low_bits);
}
#endif

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
void
the_same_as_the_floor_or_abort()
{
    for (const Padded &name : field_names())
        if (Run(name.text(), http::kTchar, http::kTcharLowBits) != http::floor_run_length(name.text(), http::kTchar))
            [[unlikely]] std::abort();
    for (const Padded *const one : {&kPath, &kLongQuery})
        if (Run(one->text(), http::kQueryByte, http::kQueryByteLowBits) !=
            http::floor_run_length(one->text(), http::kQueryByte)) [[unlikely]]
            std::abort();
}

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
void
names(benchmark::State &state)
{
    the_same_as_the_floor_or_abort<Run>();
    const std::vector<Padded> &every = field_names();
    for (auto _ : state) {
        size_t sum = all_names<Run>(every);
        benchmark::DoNotOptimize(sum);
    }
}

void
names_not_unrolled(benchmark::State &state)
{
    const std::vector<Padded> &every = field_names();
    for (auto _ : state) {
        size_t sum = all_names_not_unrolled<by_the_floor>(every);
        benchmark::DoNotOptimize(sum);
    }
}

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
void
one_run(benchmark::State &state, const Padded &padded, const std::array<bool, 256> &allowed,
        const http::NibbleTable &low_bits)
{
    the_same_as_the_floor_or_abort<Run>();
    if (Run(padded.text(), allowed, low_bits) != http::floor_run_length(padded.text(), allowed)) [[unlikely]]
        std::abort();
    for (auto _ : state) {
        std::string_view text = padded.text();
        benchmark::DoNotOptimize(text);
        size_t length = Run(text, allowed, low_bits);
        benchmark::DoNotOptimize(length);
    }
}

// One kind of test per binary: RUN_TEST picks it.
//   0  every field name of a browser request, against tchar
//   N  a token of N bytes, against tchar
#if defined(RUN_TEST) && RUN_TEST == 0
#define ARM(name, Run) void name(benchmark::State &state) { names<Run>(state); }
#elif defined(RUN_TEST)
const Padded kToken = token_of(RUN_TEST);
#define ARM(name, Run) void name(benchmark::State &state) { one_run<Run>(state, kToken, http::kTchar, http::kTcharLowBits); }
#endif

// Built without RUN_TEST, as rake bench builds every file, it holds no arm.
#if defined(ARM)
ARM(run_floor, by_the_floor)
BENCHMARK(run_floor);
#if defined(__AVX2__)
ARM(run_avx2, by_avx2)
BENCHMARK(run_avx2);
#endif
#if defined(__AVX512BW__)
ARM(run_avx512, by_avx512)
BENCHMARK(run_avx512);
ARM(run_avx2_then_avx512, by_avx2_then_avx512)
BENCHMARK(run_avx2_then_avx512);
ARM(run_avx512_masked, by_avx512_masked)
BENCHMARK(run_avx512_masked);
#endif
#if RUN_TEST == 0
void run_floor_not_unrolled(benchmark::State &state) { names_not_unrolled(state); }
BENCHMARK(run_floor_not_unrolled);
#endif
#endif

}
