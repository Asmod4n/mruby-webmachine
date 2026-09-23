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

// One question: does reading the first 32 bytes with AVX2 and the rest
// with AVX-512 beat either form alone? One test, every run of a browser
// request: its field names against tchar, its host against reg-name, and
// a target with a query, 200 bytes as a tracking link sends by default.
// One form per binary, and RUN_ARM picks it.
const Padded kHost("shop.example.com");
// TARGET_BYTES sets how long the target is; a query fills it up.
#if !defined(TARGET_BYTES)
#define TARGET_BYTES 200
#endif
const Padded kTarget = [] {
    std::string target = "/orders/4711/items?utm_source=newsletter&utm_medium=email&utm_campaign=autumn-sale-2026"
                         "&utm_content=hero-banner&session=7f3a9c1e40b2d5a8c3e1f0a9b8c7d6e5&ref=home&page=2";
    while (target.size() < TARGET_BYTES)
        target += "&filter=open&sort=added";
    target.resize(TARGET_BYTES);
    return Padded(target);
}();

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
size_t
every_run_of_a_request()
{
    size_t sum = all_names<Run>(field_names());
    sum += Run(kHost.text(), http::kRegName, http::kRegNameLowBits);
    sum += Run(kTarget.text(), http::kQueryByte, http::kQueryByteLowBits);
    return sum;
}

template <size_t (*Run)(std::string_view, const std::array<bool, 256> &, const http::NibbleTable &)>
void
request(benchmark::State &state)
{
    the_same_as_the_floor_or_abort<Run>();
    if (every_run_of_a_request<Run>() != every_run_of_a_request<by_the_floor>()) [[unlikely]]
        std::abort();
    for (auto _ : state) {
        size_t sum = every_run_of_a_request<Run>();
        benchmark::DoNotOptimize(sum);
    }
}

// Built without RUN_ARM, as rake bench builds every file, it holds no arm.
#if defined(__AVX512BW__) && defined(RUN_ARM)
#if RUN_ARM == 1
void run_avx2(benchmark::State &state) { request<by_avx2>(state); }
BENCHMARK(run_avx2);
#elif RUN_ARM == 2
void run_avx512(benchmark::State &state) { request<by_avx512>(state); }
BENCHMARK(run_avx512);
#elif RUN_ARM == 3
void run_avx2_then_avx512(benchmark::State &state) { request<by_avx2_then_avx512>(state); }
BENCHMARK(run_avx2_then_avx512);
#endif
#endif

}
