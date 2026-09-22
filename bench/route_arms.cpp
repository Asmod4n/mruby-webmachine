#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

#include <nmmintrin.h>

#include "cache.hpp"

// cache_key_of as src/cache.c writes it, copied so that both forms sit
// in one binary: memcpy into a uint64_t against std::bit_cast from a
// span of eight bytes. Same seeds, so the two must agree on every
// input, and that is checked once before the arms run.

namespace
{

uint64_t key_of_old(const uint8_t *const route, const size_t route_length)
{
    uint64_t low = ~(uint64_t) 0;
    uint64_t high = 0x9e3779b97f4a7c15ULL;
    size_t at = 0;
    for (; at + 16 <= route_length; at += 16) {
        uint64_t one = 0;
        uint64_t two = 0;
        memcpy(&one, route + at, sizeof one);
        memcpy(&two, route + at + 8, sizeof two);
        low = _mm_crc32_u64(low, one);
        high = _mm_crc32_u64(high, two);
    }
    for (; at + 8 <= route_length; at += 8) {
        uint64_t one = 0;
        memcpy(&one, route + at, sizeof one);
        low = _mm_crc32_u64(low, one);
    }
    for (; at < route_length; at++)
        low = _mm_crc32_u8((uint32_t) low, route[at]);
    return (low * 0x9e3779b97f4a7c15ULL) ^ (high << 32) ^ high;
}

const std::string kShort = "/a/b";                                  // 4
const std::string kIndex = "/index.html";                          // 11
const std::string kLong = "/api/v2/customers/8f1c2d/orders?page=3&sort=created_at&dir=desc";  // 66

const bool kAgree = [] {
    std::mt19937_64 gen(7);
    for (int round = 0; round < 100000; round++) {
        std::string s(gen() % 96, '\0');
        for (auto &c : s) c = static_cast<char>(gen());
        const uint64_t a = key_of_old(reinterpret_cast<const uint8_t *>(s.data()), s.size());
        const uint64_t b = cache::route_of(s);
        if (a != b) {
            std::fprintf(stderr, "route_of disagrees with cache_key_of at length %zu\n", s.size());
            std::abort();
        }
    }
    return true;
}();

void old_arm(benchmark::State &state, const std::string &target)
{
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            key_of_old(reinterpret_cast<const uint8_t *>(target.data()), target.size()));
    }
}

void new_arm(benchmark::State &state, const std::string &target)
{
    for (auto _ : state) benchmark::DoNotOptimize(cache::route_of(target));
}

void route_short_memcpy(benchmark::State &s) { old_arm(s, kShort); }
void route_short_bitcast(benchmark::State &s) { new_arm(s, kShort); }
void route_index_memcpy(benchmark::State &s) { old_arm(s, kIndex); }
void route_index_bitcast(benchmark::State &s) { new_arm(s, kIndex); }
void route_long_memcpy(benchmark::State &s) { old_arm(s, kLong); }
void route_long_bitcast(benchmark::State &s) { new_arm(s, kLong); }

}  // namespace

BENCHMARK(route_short_memcpy);
BENCHMARK(route_short_bitcast);
BENCHMARK(route_index_memcpy);
BENCHMARK(route_index_bitcast);
BENCHMARK(route_long_memcpy);
BENCHMARK(route_long_bitcast);
