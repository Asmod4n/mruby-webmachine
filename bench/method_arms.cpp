#include <benchmark/benchmark.h>

#include <cstdlib>
#include <string>
#include <string_view>

#include "http.hpp"

// Three ways to read a method token, and the reason there is a question.
//
// The tree packs the bytes into a uint64_t and switches over nine
// constants. The packing pads with zero, so "POST\0\0\0\0" packs to the
// number "POST" packs to and method_of answers kPost for four bytes
// nobody sent. A fuzzer found that. The packing also loads eight bytes
// for a three byte token, which is right only because the ring leaves
// kWidePadding behind every byte of its pool.
//
// Two arms answer without either property. Both read exactly the bytes
// of the token, and both refuse a token that is not one of the nine
// whatever stands behind it.
//
// The nine names are three to seven bytes long and no two of one length
// share a first byte, so a switch on either one leaves at most two
// comparisons.
namespace
{

using http::Method;

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

// The padding is not zero. Zero behind a token is the one case the
// packed arm masks away for free, and a bench that only feeds it zeros
// measures the easy half.
std::string held_with_padding(const std::string_view text)
{
    return std::string(text) + std::string(http::kWidePadding, 'a');
}

const std::string kHeld[16] = {
    held_with_padding("GET"),     held_with_padding("GET"),
    held_with_padding("GET"),     held_with_padding("POST"),
    held_with_padding("GET"),     held_with_padding("GET"),
    held_with_padding("HEAD"),    held_with_padding("GET"),
    held_with_padding("POST"),    held_with_padding("GET"),
    held_with_padding("PUT"),     held_with_padding("GET"),
    held_with_padding("OPTIONS"), held_with_padding("QUERY"),
    held_with_padding("GET"),     held_with_padding("PROPFIND")};

std::string_view token_of(const size_t at)
{
    const std::string &held = kHeld[at & 15];
    return std::string_view(held).substr(0, held.size() - http::kWidePadding);
}

const std::string_view kTokens[16] = {
    token_of(0),  token_of(1),  token_of(2),  token_of(3),
    token_of(4),  token_of(5),  token_of(6),  token_of(7),
    token_of(8),  token_of(9),  token_of(10), token_of(11),
    token_of(12), token_of(13), token_of(14), token_of(15)};

// Two arms that answer differently measure nothing, so this runs before
// every row. The second loop is the finding: the two new arms refuse a
// padded name and the packed one takes it.
void check_the_arms_agree()
{
    for (const std::string_view token : kTokens)
        if (http::method_of(token) != by_length(token) ||
            by_length(token) != by_first_byte(token))
            std::abort();
    const std::string padded = std::string("POST", 4) + std::string(4, '\0');
    const std::string_view four_nuls(padded.data(), padded.size());
    if (http::method_of(four_nuls) != Method::kPost)
        std::abort();
    if (by_length(four_nuls) != Method::kUnknown ||
        by_first_byte(four_nuls) != Method::kUnknown)
        std::abort();
}

void method_packed(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        Method got = http::method_of(kTokens[at++ & 15]);
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

} // namespace
