#include <benchmark/benchmark.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "http.hpp"

namespace
{

using http::Method;

constexpr uint64_t packed_number(const std::string_view method)
{
    if (method.empty() || method.size() > sizeof(uint64_t))
        return 0;
    uint64_t number = 0;
    std::memcpy(&number, method.data(), sizeof number);
    return number & (~uint64_t{0} >> (8 * (sizeof number - method.size())));
}

constexpr uint64_t packed_of(const char (&name)[8])
{
    uint64_t number = 0;
    for (size_t at = 0; at + 1 < sizeof name; at++)
        number |= static_cast<uint64_t>(static_cast<unsigned char>(name[at])) << (at * 8);
    return number;
}

Method by_packed_word(const std::string_view text)
{
    switch (packed_number(text)) {
        case packed_of("GET\0\0\0\0"):
            return Method::kGet;
        case packed_of("HEAD\0\0\0"):
            return Method::kHead;
        case packed_of("POST\0\0\0"):
            return Method::kPost;
        case packed_of("PUT\0\0\0\0"):
            return Method::kPut;
        case packed_of("DELETE\0"):
            return Method::kDelete;
        case packed_of("CONNECT"):
            return Method::kConnect;
        case packed_of("OPTIONS"):
            return Method::kOptions;
        case packed_of("TRACE\0\0"):
            return Method::kTrace;
        case packed_of("QUERY\0\0"):
            return Method::kQuery;
        default:
            return Method::kUnknown;
    }
}

Method by_length(const std::string_view text)
{
    switch (text.size()) {
        case 3:
            if (text == "GET")
                return Method::kGet;
            if (text == "PUT")
                return Method::kPut;
            return Method::kUnknown;
        case 4:
            if (text == "HEAD")
                return Method::kHead;
            if (text == "POST")
                return Method::kPost;
            return Method::kUnknown;
        case 5:
            if (text == "TRACE")
                return Method::kTrace;
            if (text == "QUERY")
                return Method::kQuery;
            return Method::kUnknown;
        case 6:
            return text == "DELETE" ? Method::kDelete : Method::kUnknown;
        case 7:
            if (text == "CONNECT")
                return Method::kConnect;
            if (text == "OPTIONS")
                return Method::kOptions;
            return Method::kUnknown;
        default:
            return Method::kUnknown;
    }
}

Method by_first_byte(const std::string_view text)
{
    if (text.empty())
        return Method::kUnknown;
    switch (text.front()) {
        case 'G':
            return text == "GET" ? Method::kGet : Method::kUnknown;
        case 'H':
            return text == "HEAD" ? Method::kHead : Method::kUnknown;
        case 'P':
            if (text == "POST")
                return Method::kPost;
            if (text == "PUT")
                return Method::kPut;
            return Method::kUnknown;
        case 'D':
            return text == "DELETE" ? Method::kDelete : Method::kUnknown;
        case 'C':
            return text == "CONNECT" ? Method::kConnect : Method::kUnknown;
        case 'O':
            return text == "OPTIONS" ? Method::kOptions : Method::kUnknown;
        case 'T':
            return text == "TRACE" ? Method::kTrace : Method::kUnknown;
        case 'Q':
            return text == "QUERY" ? Method::kQuery : Method::kUnknown;
        default:
            return Method::kUnknown;
    }
}

std::string held_with_padding(const std::string_view text)
{
    return std::string(text) + std::string(http::kWidePadding, 'a');
}

const std::string kHeld[16] = {
    held_with_padding("GET"),     held_with_padding("GET"),   held_with_padding("GET"),
    held_with_padding("POST"),    held_with_padding("GET"),   held_with_padding("GET"),
    held_with_padding("HEAD"),    held_with_padding("GET"),   held_with_padding("POST"),
    held_with_padding("GET"),     held_with_padding("PUT"),   held_with_padding("GET"),
    held_with_padding("OPTIONS"), held_with_padding("QUERY"), held_with_padding("GET"),
    held_with_padding("PROPFIND")};

std::string_view token_of(const size_t at)
{
    const std::string &held = kHeld[at & 15];
    return std::string_view(held).substr(0, held.size() - http::kWidePadding);
}

const std::string_view kTokens[16] = {token_of(0),  token_of(1),  token_of(2),  token_of(3),
                                      token_of(4),  token_of(5),  token_of(6),  token_of(7),
                                      token_of(8),  token_of(9),  token_of(10), token_of(11),
                                      token_of(12), token_of(13), token_of(14), token_of(15)};

void check_the_arms_agree()
{
    for (const std::string_view token : kTokens)
        if (by_packed_word(token) != by_length(token) || by_length(token) != by_first_byte(token) ||
            by_first_byte(token) != http::method_of(token))
            std::abort();
    const std::string padded = std::string("POST", 4) + std::string(4, '\0');
    const std::string_view four_nuls(padded.data(), padded.size());
    if (by_packed_word(four_nuls) != Method::kPost)
        std::abort();
    if (by_length(four_nuls) != Method::kUnknown || by_first_byte(four_nuls) != Method::kUnknown ||
        http::method_of(four_nuls) != Method::kUnknown)
        std::abort();
}

void method_packed(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        Method got = by_packed_word(kTokens[at++ & 15]);
        benchmark::DoNotOptimize(got);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void method_by_length(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        Method got = by_length(kTokens[at++ & 15]);
        benchmark::DoNotOptimize(got);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void method_by_first_byte(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        Method got = by_first_byte(kTokens[at++ & 15]);
        benchmark::DoNotOptimize(got);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(method_packed);
BENCHMARK(method_by_length);
BENCHMARK(method_by_first_byte);

}
