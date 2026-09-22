#include <benchmark/benchmark.h>

#include <cstring>
#include <string>

#include "../src/http1.hpp"

namespace
{

const std::string &one_request()
{
    static const std::string asked =
        "GET /articles/42?param=xyz&foo=bar HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: htgen\r\n"
        "Accept: */*\r\n"
        "\r\n";
    return asked;
}

const std::string &a_browser_request()
{
    static const std::string asked =
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
    return asked;
}

void bytes_before_the_body_of_one(benchmark::State &state)
{
    const std::string &asked = one_request();
    for (auto _ : state)
        benchmark::DoNotOptimize(http1::bytes_before_the_body(asked));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void parse_request_of_one(benchmark::State &state)
{
    const std::string &asked = one_request();
    for (auto _ : state)
        benchmark::DoNotOptimize(http1::parse_request(asked));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void parse_request_of_a_browser(benchmark::State &state)
{
    const std::string &asked = a_browser_request();
    for (auto _ : state)
        benchmark::DoNotOptimize(http1::parse_request(asked));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void parse_one_field_line(benchmark::State &state)
{
    const std::string_view line = "User-Agent: htgen";
    for (auto _ : state)
        benchmark::DoNotOptimize(http1::parse_field_line(line));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void field_value_of_the_last(benchmark::State &state)
{
    const std::string &asked = a_browser_request();
    const http1::Request request = *http1::parse_request(asked);
    for (auto _ : state)
        benchmark::DoNotOptimize(http1::field_value_of(request, "Sec-Fetch-User"));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void the_whole_answer_loop(benchmark::State &state)
{
    static const char kAnswer[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    const std::string &asked = one_request();
    static uint8_t into[16384];
    for (auto _ : state) {
        std::string_view left(asked);
        size_t written = 0;
        while (!left.empty()) {
            const std::expected<http1::Request, http::Refusal> request =
                http1::parse_request(left);
            if (!request)
                break;
            std::memcpy(into + written, kAnswer, sizeof kAnswer - 1);
            written += sizeof kAnswer - 1;
            left.remove_prefix(request->bytes);
        }
        benchmark::DoNotOptimize(written);
        benchmark::DoNotOptimize(into);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(bytes_before_the_body_of_one);
BENCHMARK(parse_request_of_one);
BENCHMARK(parse_request_of_a_browser);
BENCHMARK(parse_one_field_line);
BENCHMARK(field_value_of_the_last);
BENCHMARK(the_whole_answer_loop);

}
