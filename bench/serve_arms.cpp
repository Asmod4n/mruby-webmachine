#include <benchmark/benchmark.h>

#include <cstring>
#include <string>

#include "../src/head.hpp"

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

void end_of_head_found(benchmark::State &state)
{
    const std::string &asked = one_request();
    for (auto _ : state)
        benchmark::DoNotOptimize(wm::end_of_head(asked, 0));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void head_of_one_request(benchmark::State &state)
{
    const std::string &asked = one_request();
    for (auto _ : state) {
        wm::Head head;
        benchmark::DoNotOptimize(wm::head_of(asked, head));
        benchmark::DoNotOptimize(head);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void head_of_a_browser_request(benchmark::State &state)
{
    const std::string &asked = a_browser_request();
    for (auto _ : state) {
        wm::Head head;
        benchmark::DoNotOptimize(wm::head_of(asked, head));
        benchmark::DoNotOptimize(head);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void split_one_field(benchmark::State &state)
{
    const std::string_view line = "User-Agent: htgen";
    for (auto _ : state) {
        wm::Field field;
        benchmark::DoNotOptimize(wm::split_at_colon(line, field));
        benchmark::DoNotOptimize(field);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void value_of_the_last_field(benchmark::State &state)
{
    const std::string &asked = a_browser_request();
    wm::Head head;
    wm::head_of(asked, head);
    for (auto _ : state)
        benchmark::DoNotOptimize(wm::value_of(head, "Sec-Fetch-User"));
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
        for (;;) {
            wm::Head head;
            const wm::Reading read = wm::head_of(left, head);
            if (read != wm::Reading::kRead)
                break;
            std::memcpy(into + written, kAnswer, sizeof kAnswer - 1);
            written += sizeof kAnswer - 1;
            left.remove_prefix(head.bytes);
            if (left.empty())
                break;
        }
        benchmark::DoNotOptimize(written);
        benchmark::DoNotOptimize(into);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(end_of_head_found);
BENCHMARK(head_of_one_request);
BENCHMARK(head_of_a_browser_request);
BENCHMARK(split_one_field);
BENCHMARK(value_of_the_last_field);
BENCHMARK(the_whole_answer_loop);

}
