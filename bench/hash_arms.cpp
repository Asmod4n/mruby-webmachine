#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <immintrin.h>

namespace
{

std::string key_at(const size_t which)
{
    char room[64];
    const int length =
        std::snprintf(room, sizeof room, "GET /articles/%zu?param=xyz&foo=bar", which);
    return std::string(room, static_cast<size_t>(length));
}

const std::vector<std::string> &keys()
{
    static const std::vector<std::string> made = [] {
        std::vector<std::string> one;
        one.reserve(4096);
        for (size_t at = 0; at < 4096; at++)
            one.push_back(key_at(at * 7919));
        return one;
    }();
    return made;
}

uint64_t word_at(const char *const from)
{
    uint64_t word = 0;
    std::memcpy(&word, from, sizeof word);
    return word;
}

uint64_t by_fnv(const std::string_view text)
{
    uint64_t number = 1469598103934665603ULL;
    for (const char one : text) {
        number ^= static_cast<unsigned char>(one);
        number *= 1099511628211ULL;
    }
    return number;
}

uint64_t by_crc32c(const std::string_view text)
{
    uint64_t number = ~uint64_t{0};
    size_t at = 0;
    for (; at + 8 <= text.size(); at += 8)
        number = _mm_crc32_u64(number, word_at(text.data() + at));
    for (; at < text.size(); at++)
        number = _mm_crc32_u8(static_cast<uint32_t>(number),
                              static_cast<unsigned char>(text[at]));
    return number;
}

uint64_t by_two_crc32c(const std::string_view text)
{
    uint64_t low = ~uint64_t{0};
    uint64_t high = 0x9e3779b97f4a7c15ULL;
    size_t at = 0;
    for (; at + 16 <= text.size(); at += 16) {
        low = _mm_crc32_u64(low, word_at(text.data() + at));
        high = _mm_crc32_u64(high, word_at(text.data() + at + 8));
    }
    for (; at + 8 <= text.size(); at += 8)
        low = _mm_crc32_u64(low, word_at(text.data() + at));
    for (; at < text.size(); at++)
        low = _mm_crc32_u8(static_cast<uint32_t>(low), static_cast<unsigned char>(text[at]));
    return (low * 0x9e3779b97f4a7c15ULL) ^ (high << 32) ^ high;
}

uint64_t fold(uint64_t number)
{
    number ^= number >> 33;
    number *= 0xff51afd7ed558ccdULL;
    number ^= number >> 33;
    number *= 0xc4ceb9fe1a85ec53ULL;
    number ^= number >> 33;
    return number;
}

uint64_t by_multiply_fold(const std::string_view text)
{
    uint64_t number = 0x9e3779b97f4a7c15ULL ^ text.size();
    size_t at = 0;
    for (; at + 8 <= text.size(); at += 8) {
        number ^= word_at(text.data() + at);
        number *= 0xff51afd7ed558ccdULL;
        number ^= number >> 29;
    }
    if (at < text.size()) {
        uint64_t last = 0;
        std::memcpy(&last, text.data() + at, text.size() - at);
        number ^= last;
        number *= 0xff51afd7ed558ccdULL;
    }
    return fold(number);
}


uint64_t rotate(const uint64_t number, const int by)
{
    return (number << by) | (number >> (64 - by));
}

uint64_t by_siphash13(const std::string_view text)
{
    uint64_t v0 = 0x736f6d6570736575ULL ^ 0x0706050403020100ULL;
    uint64_t v1 = 0x646f72616e646f6dULL ^ 0x0f0e0d0c0b0a0908ULL;
    uint64_t v2 = 0x6c7967656e657261ULL ^ 0x0706050403020100ULL;
    uint64_t v3 = 0x7465646279746573ULL ^ 0x0f0e0d0c0b0a0908ULL;
    const auto round = [&] {
        v0 += v1; v1 = rotate(v1, 13); v1 ^= v0; v0 = rotate(v0, 32);
        v2 += v3; v3 = rotate(v3, 16); v3 ^= v2;
        v0 += v3; v3 = rotate(v3, 21); v3 ^= v0;
        v2 += v1; v1 = rotate(v1, 17); v1 ^= v2; v2 = rotate(v2, 32);
    };
    size_t at = 0;
    for (; at + 8 <= text.size(); at += 8) {
        const uint64_t word = word_at(text.data() + at);
        v3 ^= word;
        round();
        v0 ^= word;
    }
    uint64_t last = static_cast<uint64_t>(text.size() & 0xff) << 56;
    std::memcpy(&last, text.data() + at, text.size() - at);
    last |= static_cast<uint64_t>(text.size() & 0xff) << 56;
    v3 ^= last;
    round();
    v0 ^= last;
    v2 ^= 0xff;
    round(); round(); round();
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t by_murmur3(const std::string_view text)
{
    constexpr uint64_t c1 = 0x87c37b91114253d5ULL;
    constexpr uint64_t c2 = 0x4cf5ad432745937fULL;
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    size_t at = 0;
    for (; at + 16 <= text.size(); at += 16) {
        uint64_t k1 = word_at(text.data() + at);
        uint64_t k2 = word_at(text.data() + at + 8);
        k1 *= c1; k1 = rotate(k1, 31); k1 *= c2; h1 ^= k1;
        h1 = rotate(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;
        k2 *= c2; k2 = rotate(k2, 33); k2 *= c1; h2 ^= k2;
        h2 = rotate(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
    }
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    const size_t left = text.size() - at;
    if (left > 8) {
        std::memcpy(&k1, text.data() + at, 8);
        std::memcpy(&k2, text.data() + at + 8, left - 8);
    } else if (left > 0) {
        std::memcpy(&k1, text.data() + at, left);
    }
    k1 *= c1; k1 = rotate(k1, 31); k1 *= c2; h1 ^= k1;
    k2 *= c2; k2 = rotate(k2, 33); k2 *= c1; h2 ^= k2;
    h1 ^= text.size(); h2 ^= text.size();
    h1 += h2; h2 += h1;
    h1 = fold(h1); h2 = fold(h2);
    h1 += h2;
    return h1;
}

void check_the_arms_disagree()
{
    for (const auto &one : {by_fnv, by_crc32c, by_two_crc32c, by_multiply_fold, by_siphash13, by_murmur3}) {
        std::vector<uint64_t> seen;
        seen.reserve(keys().size());
        for (const std::string &text : keys())
            seen.push_back(one(text));
        std::sort(seen.begin(), seen.end());
        if (std::adjacent_find(seen.begin(), seen.end()) != seen.end())
            std::abort();
    }
}

#define HASH_ARM(name, call)                                                                       \
    void name(benchmark::State &state)                                                             \
    {                                                                                              \
        check_the_arms_disagree();                                                                 \
        size_t at = 0;                                                                             \
        for (auto _ : state) {                                                                     \
            const uint64_t got = call(keys().at(at));                                               \
            if (++at == keys().size())                                                             \
                at = 0;                                                                            \
            benchmark::DoNotOptimize(got);                                                         \
        }                                                                                          \
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));                         \
    }                                                                                              \
    BENCHMARK(name)

HASH_ARM(hash_by_fnv, by_fnv);
HASH_ARM(hash_by_crc32c, by_crc32c);
HASH_ARM(hash_by_two_crc32c, by_two_crc32c);
HASH_ARM(hash_by_multiply_fold, by_multiply_fold);
HASH_ARM(hash_by_siphash13, by_siphash13);
HASH_ARM(hash_by_murmur3, by_murmur3);

}
