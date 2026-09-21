#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

extern "C" {
#include "picohttpparser/picohttpparser.h"
}

namespace
{

constexpr uint8_t folded(const char byte)
{
    return static_cast<uint8_t>(static_cast<unsigned char>(byte) | 0x20U);
}

std::string head_with_cookie_of(const size_t cookie_bytes)
{
    std::string cookie = "session=";
    while (cookie.size() < cookie_bytes)
        cookie += "7f3a9c1e40b2d5a8; consent=1; cart=4711.2.998; ab=v3; ";
    cookie.resize(cookie_bytes);
    std::string head;
    head += "Host: shop.example\r\n";
    head += "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:141.0) Gecko/20100101 Firefox/141.0\r\n";
    head += "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,"
            "image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7\r\n";
    head += "Accept-Language: de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7\r\n";
    head += "Accept-Encoding: gzip, deflate, br, zstd\r\n";
    head += "Connection: keep-alive\r\n";
    head += "Cookie: " + cookie + "\r\n";
    head += "If-None-Match: \"a1b2c3d4e5f6\"\r\n";
    head += "If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT\r\n";
    head += "Cache-Control: max-age=0\r\n";
    head += "Referer: https://shop.example/orders?page=2&sort=added\r\n";
    head += "Sec-Fetch-Mode: navigate\r\n";
    head += "Sec-Fetch-Site: same-origin\r\n";
    head += "Sec-Fetch-Dest: document\r\n";
    head += "Priority: u=0, i\r\n";
    head += "Upgrade-Insecure-Requests: 1\r\n";
    head += "\r\n";
    head.append(http::kWidePadding, '\0');
    return head;
}

constexpr size_t kCookieSizes[] = {40, 800, 4000};

const std::string &head_of(const size_t which)
{
    static const std::array<std::string, 3> every = {head_with_cookie_of(kCookieSizes[0]),
                                                     head_with_cookie_of(kCookieSizes[1]),
                                                     head_with_cookie_of(kCookieSizes[2])};
    return every.at(which);
}

std::string_view field_block_of(const size_t which)
{
    const std::string &head = head_of(which);
    return std::string_view(head.data(), head.size() - http::kWidePadding);
}

// picohttpparser wants the request line in front of the section, so the
// libreactor arm reads a head that carries one.
const std::string &whole_head_of(const size_t which)
{
    static const std::array<std::string, 3> every = {
        "GET /orders/4711/items HTTP/1.1\r\n" + std::string(field_block_of(0)),
        "GET /orders/4711/items HTTP/1.1\r\n" + std::string(field_block_of(1)),
        "GET /orders/4711/items HTTP/1.1\r\n" + std::string(field_block_of(2))};
    return every.at(which);
}

constexpr std::string_view kNamePool[] = {
    "accept",           "accept-encoding",  "accept-language", "if-none-match",
    "if-modified-since", "content-length",  "content-type",    "content-encoding",
    "authorization",    "if-match",         "if-unmodified-since", "if-range",
    "range",            "expect",           "host",            "referer",
};

constexpr size_t kPoolSize = sizeof kNamePool / sizeof kNamePool[0];
constexpr uint16_t kFirstFieldSlot = 2;
constexpr size_t kSlotLimit = kFirstFieldSlot + kPoolSize + 1;

struct AskedField {
    uint8_t first_byte;
    uint8_t name_length;
    uint8_t discriminator_at;
    uint8_t discriminator;
    uint16_t slot;
};

constexpr uint8_t sketch_bit_of(const uint8_t first_byte, const uint8_t name_length)
{
    return static_cast<uint8_t>((first_byte * 5U + name_length) & 63U);
}

struct Asked {
    std::vector<AskedField> field;
    uint64_t sketch;
};

uint8_t discriminator_at_of(const std::string_view name, const std::vector<std::string_view> &every)
{
    uint8_t at = 0;
    for (const std::string_view rival : every) {
        if (rival.data() == name.data() || rival.size() != name.size() ||
            folded(rival.front()) != folded(name.front()))
            continue;
        for (uint8_t byte = 1; byte < name.size(); byte++)
            if (folded(rival[byte]) != folded(name[byte])) {
                at = std::max(at, byte);
                break;
            }
    }
    return at;
}

Asked asked_of(const size_t count, const bool cookie_is_asked)
{
    std::vector<std::string_view> names;
    for (size_t which = 0; which < count; which++)
        names.push_back(kNamePool[which]);
    if (cookie_is_asked)
        names.push_back("cookie");
    Asked asked{};
    asked.sketch = 0;
    for (size_t which = 0; which < names.size(); which++) {
        const std::string_view name = names.at(which);
        const uint8_t at = discriminator_at_of(name, names);
        asked.field.push_back(AskedField{folded(name.front()), static_cast<uint8_t>(name.size()),
                                         at, folded(name[at]),
                                         static_cast<uint16_t>(kFirstFieldSlot + which)});
        asked.sketch |= uint64_t{1} << sketch_bit_of(folded(name.front()),
                                                     static_cast<uint8_t>(name.size()));
    }
    return asked;
}

const Asked &asked_for(const size_t count, const bool cookie_is_asked)
{
    static std::array<Asked, (kPoolSize + 1) * 2> every = [] {
        std::array<Asked, (kPoolSize + 1) * 2> built{};
        for (size_t count = 0; count <= kPoolSize; count++) {
            built.at(count * 2) = asked_of(count, false);
            built.at(count * 2 + 1) = asked_of(count, true);
        }
        return built;
    }();
    return every.at(count * 2 + (cookie_is_asked ? 1 : 0));
}

bool name_is_the_asked_one(const std::string_view name, const AskedField field)
{
    return name.size() == field.name_length && folded(name.front()) == field.first_byte &&
           folded(name.at(field.discriminator_at)) == field.discriminator;
}

std::string_view without_surrounding_whitespace(const std::string_view text)
{
    size_t from = 0;
    while (from < text.size() && (text[from] == ' ' || text[from] == '\t'))
        from++;
    size_t until = text.size();
    while (until > from && (text[until - 1] == ' ' || text[until - 1] == '\t'))
        until--;
    return text.substr(from, until - from);
}

using Answer = std::array<std::string_view, kSlotLimit>;

constexpr size_t kNameLimit = 40;

size_t colon_within_the_name(const std::string_view rest)
{
    const size_t limit = std::min(kNameLimit, rest.size());
    for (size_t at = 0; at < limit; at++)
        if (rest[at] == ':')
            return at;
    return std::string_view::npos;
}

void take_the_value(Answer &answer, const Asked &asked, const std::string_view name,
                    const std::string_view value)
{
    if (name.empty())
        return;
    const uint8_t bit = sketch_bit_of(folded(name.front()), static_cast<uint8_t>(name.size()));
    if ((asked.sketch >> bit & 1) == 0)
        return;
    for (const AskedField field : asked.field)
        if (name_is_the_asked_one(name, field)) {
            answer.at(field.slot) = without_surrounding_whitespace(value);
            return;
        }
}

// 1. Our form, which the walk measurement decided: the line end is found
// first, and the colon only inside that line.
Answer walk_by_line_end_first(const size_t which, const Asked &asked)
{
    Answer answer{};
    std::string_view rest = field_block_of(which);
    while (!rest.starts_with("\r\n")) {
        const size_t line_end = rest.find('\n');
        const std::string_view line = rest.substr(0, line_end - 1);
        rest = rest.substr(line_end + 1);
        const size_t colon = colon_within_the_name(line);
        if (colon == std::string_view::npos)
            continue;
        take_the_value(answer, asked, line.substr(0, colon), line.substr(colon + 1));
    }
    return answer;
}

// 2. What libreactor does: picohttpparser reads the whole head into an
// array, and each asked name is then looked for over that array.
Answer walk_like_libreactor(const size_t which, const Asked &asked)
{
    const std::string &head = whole_head_of(which);
    std::array<phr_header, 64> every{};
    size_t count = every.size();
    const char *method = nullptr;
    const char *path = nullptr;
    size_t method_length = 0;
    size_t path_length = 0;
    int minor_version = 0;
    phr_parse_request(head.data(), head.size(), &method, &method_length, &path, &path_length,
                      &minor_version, every.data(), &count, 0);
    Answer answer{};
    for (const AskedField field : asked.field)
        for (size_t at = 0; at < count; at++) {
            const std::string_view name(every.at(at).name, every.at(at).name_len);
            if (name_is_the_asked_one(name, field)) {
                answer.at(field.slot) =
                    std::string_view(every.at(at).value, every.at(at).value_len);
                break;
            }
        }
    return answer;
}

// 3. What mrhttp does: one pass over 64 bytes at a time whose mask holds
// the colons and the line ends together, so both fall out of one scan.
Answer walk_by_combined_mask(const size_t which, const Asked &asked)
{
    const std::string_view field_block = field_block_of(which);
    Answer answer{};
    const __m256i carriage_return = _mm256_set1_epi8('\r');
    const __m256i colon = _mm256_set1_epi8(':');
    const char *const from = field_block.data();
    const char *const until = from + field_block.size();
    const char *span_start = from;
    const char *at = from;
    std::string_view name;
    bool reads_a_value = false;
    for (const char *block = from; block < until; block += 64) {
        const __m256i low = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(block));
        const __m256i high = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(block + 32));
        const uint64_t mask =
            static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_or_si256(_mm256_cmpeq_epi8(low, carriage_return),
                                _mm256_cmpeq_epi8(low, colon)))) |
            (static_cast<uint64_t>(static_cast<uint32_t>(_mm256_movemask_epi8(
                 _mm256_or_si256(_mm256_cmpeq_epi8(high, carriage_return),
                                 _mm256_cmpeq_epi8(high, colon))))) << 32);
        for (;;) {
            const size_t shifted = static_cast<size_t>(at - block);
            if (shifted >= 64)
                break;
            const uint64_t left = mask >> shifted;
            if (left == 0) {
                at = block + 64;
                break;
            }
            at += static_cast<size_t>(__builtin_ctzll(left));
            if (at >= until)
                return answer;
            if (reads_a_value) {
                if (*at == ':') {
                    at += 1;
                    continue;
                }
                take_the_value(answer, asked, name,
                               std::string_view(span_start, static_cast<size_t>(at - span_start)));
                reads_a_value = false;
                at += 2;
                if (at < until && *at == '\r')
                    return answer;
            } else {
                name = std::string_view(span_start, static_cast<size_t>(at - span_start));
                reads_a_value = true;
                at += 2;
            }
            span_start = at;
        }
    }
    return answer;
}

void check_the_arms_agree(const size_t which, const Asked &asked)
{
    const Answer reference = walk_by_line_end_first(which, asked);
    if (reference.at(kFirstFieldSlot).empty())
        std::abort();
    for (const Answer got : {walk_like_libreactor(which, asked), walk_by_combined_mask(which, asked)})
        for (size_t slot = 0; slot < kSlotLimit; slot++)
            if (got.at(slot) != reference.at(slot))
                std::abort();
}

#define FEED_ARM(name, call)                                                                       \
    void name(benchmark::State &state)                                                             \
    {                                                                                              \
        const size_t which = static_cast<size_t>(state.range(0));                                  \
        const Asked &asked = asked_for(static_cast<size_t>(state.range(1)), state.range(2) != 0);  \
        check_the_arms_agree(which, asked);                                                        \
        for (auto _ : state) {                                                                     \
            Answer answer = call(which, asked);                                                    \
            benchmark::DoNotOptimize(answer);                                                      \
        }                                                                                          \
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));                         \
    }                                                                                              \
    BENCHMARK(name)->Args({0, 5, 0})->Args({2, 5, 0})->Args({2, 16, 0})

FEED_ARM(feed_walk_by_line_end_first, walk_by_line_end_first);
FEED_ARM(feed_walk_like_libreactor, walk_like_libreactor);
FEED_ARM(feed_walk_by_combined_mask, walk_by_combined_mask);

}
