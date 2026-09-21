#include <benchmark/benchmark.h>

#include <string>

#include "http.hpp"
#include "phr_wanted.hpp"
#include "picohttpparser/picohttpparser.h"

// Two ways to get the four fields a negotiated GET wants out of one request
// head, both through picohttpparser, both in this binary.
//
//   phr_then_lookup   phr fills its array, we walk the array afterwards
//   phr_then_sink     phr calls a hook at the field, where the name and the
//                     value are still in registers, and nothing is walked
//
// The vendored copy is the Asmod4n fork at adc9666 plus one hook in
// parse_headers, which expands to nothing unless PHR_ON_FIELD is defined.
extern "C" {
int hooked_phr_parse_request(const char *buf, size_t len, const char **method, size_t *method_len,
                             const char **path, size_t *path_len, int *minor_version,
                             struct phr_header *headers, size_t *num_headers, size_t last_len);
}

namespace wanted
{
extern Wanted *sink;
}

namespace
{

// The request Chrome sends, with the padding the ring leaves behind every
// byte of its pool.
const std::string kHead =
    std::string("GET /index.html HTTP/1.1\r\n"
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

constexpr size_t kMaxFields = 32;

wanted::Wanted phr_then_lookup_once()
{
    const char *method = nullptr;
    const char *target = nullptr;
    size_t method_len = 0;
    size_t target_len = 0;
    int minor_version = 0;
    struct phr_header fields[kMaxFields];
    size_t field_count = kMaxFields;
    wanted::Wanted found{};
    const int read = phr_parse_request(kHead.data(), kHeadSize, &method, &method_len, &target,
                                       &target_len, &minor_version, fields, &field_count, 0);
    if (read < 0)
        return found;
    for (size_t at = 0; at < field_count; at++)
        wanted::note_field(std::string_view(fields[at].name, fields[at].name_len),
                           std::string_view(fields[at].value, fields[at].value_len), found);
    return found;
}

wanted::Wanted phr_then_sink_once()
{
    const char *method = nullptr;
    const char *target = nullptr;
    size_t method_len = 0;
    size_t target_len = 0;
    int minor_version = 0;
    struct phr_header fields[kMaxFields];
    size_t field_count = kMaxFields;
    wanted::Wanted found{};
    wanted::sink = &found;
    const int read =
        hooked_phr_parse_request(kHead.data(), kHeadSize, &method, &method_len, &target,
                                 &target_len, &minor_version, fields, &field_count, 0);
    if (read < 0)
        return wanted::Wanted{};
    return found;
}

void phr_then_lookup(benchmark::State &state)
{
    for (auto _ : state) {
        const wanted::Wanted found = phr_then_lookup_once();
        size_t seen = found.host.size() + found.accept.size() + found.accept_encoding.size() +
                      found.accept_language.size();
        if (seen == 0) [[unlikely]]
            state.SkipWithError("no field was found");
        benchmark::DoNotOptimize(seen);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kHeadSize));
}

void phr_then_sink(benchmark::State &state)
{
    for (auto _ : state) {
        const wanted::Wanted found = phr_then_sink_once();
        size_t seen = found.host.size() + found.accept.size() + found.accept_encoding.size() +
                      found.accept_language.size();
        if (seen == 0) [[unlikely]]
            state.SkipWithError("no field was found");
        benchmark::DoNotOptimize(seen);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kHeadSize));
}

BENCHMARK(phr_then_lookup);
BENCHMARK(phr_then_sink);

} // namespace
