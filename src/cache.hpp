#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <nmmintrin.h>

namespace cache
{

/* crc32c of eight bytes into the running value. The one instruction
 * this file keeps: there is no standard form of it, so the span is what
 * makes it safe - eight bytes is the type, not a length beside a
 * pointer. */
inline uint64_t crc_step(const uint64_t taken, const std::span<const std::byte, 8> word)
{
    return _mm_crc32_u64(taken, std::bit_cast<uint64_t>(std::array<std::byte, 8>{
                                    word[0], word[1], word[2], word[3], word[4], word[5],
                                    word[6], word[7]}));
}

/* The number a route is filed under: crc32c over the target, two
 * streams so the two instructions overlap, folded with the golden ratio
 * so the low bits of a short target still spread over the whole key
 * space. The seeds are cache_key_of's, so a key already in a database
 * is the same key here. */
inline uint64_t route_of(const std::string_view target)
{
    const std::span<const std::byte> bytes = std::as_bytes(std::span(target));
    uint64_t low = ~uint64_t{0};
    uint64_t high = 0x9e3779b97f4a7c15ULL;
    size_t at = 0;
    for (; at + 16 <= bytes.size(); at += 16) {
        low = crc_step(low, bytes.subspan(at).first<8>());
        high = crc_step(high, bytes.subspan(at + 8).first<8>());
    }
    for (; at + 8 <= bytes.size(); at += 8)
        low = crc_step(low, bytes.subspan(at).first<8>());
    for (; at < bytes.size(); at++)
        low = _mm_crc32_u8(static_cast<uint32_t>(low), std::to_integer<uint8_t>(bytes[at]));
    return (low * 0x9e3779b97f4a7c15ULL) ^ (high << 32) ^ high;
}

}  // namespace cache
