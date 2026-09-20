#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "http.hpp"

namespace
{

// The inputs change from call to call, so the compiler cannot compute
// the answer once and lift it out of the loop.
const std::string_view kTimestamps[16] = {
    "Sun, 06 Nov 1994 08:49:37 GMT", "Mon, 07 Dec 2020 23:59:59 GMT",
    "Tue, 01 Jan 1980 00:00:00 GMT", "Wed, 29 Feb 2020 12:30:01 GMT",
    "Thu, 15 Mar 2001 06:06:06 GMT", "Fri, 31 Aug 1999 17:45:00 GMT",
    "Sat, 22 Jun 2011 09:09:09 GMT", "Sun, 05 May 2030 21:00:59 GMT",
    "Mon, 11 Apr 1975 03:14:15 GMT", "Tue, 19 Jan 2038 03:14:07 GMT",
    "Wed, 02 Jul 2024 13:37:00 GMT", "Thu, 25 Oct 1990 08:00:00 GMT",
    "Fri, 14 Sep 2007 19:20:21 GMT", "Sat, 30 Nov 2013 01:02:03 GMT",
    "Sun, 08 Feb 1998 22:22:22 GMT", "Mon, 27 Dec 2049 16:05:44 GMT"};

const std::string_view kFieldNames[16] = {
    "host", "user-agent", "accept", "accept-encoding", "connection", "content-type",
    "content-length", "if-none-match", "if-modified-since", "authorization", "cookie",
    "referer", "x-forwarded-for", "cache-control", "sec-fetch-mode", "origin"};

const std::string_view kQuoted[4] = {"\"utf-8\"", "\"a b c\"", "\"x\\\"y\"", "\"\""};

const std::string_view kParameters[4] = {";charset=utf-8", ";q=0.8", ";charset=\"utf-8\"",
                                         "; level = 1"};

void is_tchar(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        const std::string_view name = kFieldNames[at++ & 15];
        bool all = true;
        for (const char letter : name)
            all = all && http::is_tchar(letter);
        benchmark::DoNotOptimize(all);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// The field names of a real request, as views into the buffer that
// carries it, so the wide load has the rest of the request behind it.
const std::string kChrome =
    "GET /index.html HTTP/1.1\r\n"
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
    "\r\n";

std::vector<std::string_view> field_names_of(const std::string &request)
{
    std::vector<std::string_view> names;
    const std::string_view whole(request);
    size_t at = whole.find("\r\n") + 2;
    while (at + 1 < whole.size() && whole.substr(at, 2) != "\r\n") {
        names.push_back(whole.substr(at, whole.find(':', at) - at));
        at = whole.find("\r\n", at) + 2;
    }
    return names;
}

const std::vector<std::string_view> kChromeFieldNames = field_names_of(kChrome);

void is_token_over_field_names(benchmark::State &state)
{
    for (auto _ : state) {
        bool all = true;
        for (const std::string_view name : kChromeFieldNames)
            all = all && http::is_token(name, kChrome.size() -
                                                  static_cast<size_t>(name.data() -
                                                                      kChrome.data()));
        benchmark::DoNotOptimize(all);
    }
}

void every_byte_over_field_names(benchmark::State &state)
{
    for (auto _ : state) {
        bool all = true;
        for (const std::string_view name : kChromeFieldNames)
            all = all && http::is_token(name, name.size());
        benchmark::DoNotOptimize(all);
    }
}

const std::string_view kHostOfChrome =
    std::string_view(kChrome).substr(kChrome.find("Host: ") + 6,
                                     kChrome.find("\r\n", kChrome.find("Host: ")) -
                                         kChrome.find("Host: ") - 6);

void is_reg_name_over_a_host(benchmark::State &state)
{
    const size_t readable =
        kChrome.size() - static_cast<size_t>(kHostOfChrome.data() - kChrome.data());
    for (auto _ : state) {
        bool good = http::is_reg_name(kHostOfChrome, readable);
        benchmark::DoNotOptimize(good);
    }
}

void every_byte_over_a_host(benchmark::State &state)
{
    for (auto _ : state) {
        bool good = http::is_reg_name(kHostOfChrome, kHostOfChrome.size());
        benchmark::DoNotOptimize(good);
    }
}

void parse_host_of_chrome(benchmark::State &state)
{
    const size_t readable =
        kChrome.size() - static_cast<size_t>(kHostOfChrome.data() - kChrome.data());
    for (auto _ : state) {
        auto got = http::parse_host(kHostOfChrome, readable);
        benchmark::DoNotOptimize(got);
    }
}

void parse_quoted_string(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_quoted_string(kQuoted[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_field_value_parameter(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_field_value_parameter(kParameters[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_imf_fixdate(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_imf_fixdate(kTimestamps[at++ & 15]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_http_date(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_http_date(kTimestamps[at++ & 15], std::chrono::year{2026});
        benchmark::DoNotOptimize(got);
    }
}

BENCHMARK(is_tchar);
BENCHMARK(every_byte_over_field_names);
BENCHMARK(is_token_over_field_names);
BENCHMARK(every_byte_over_a_host);
BENCHMARK(is_reg_name_over_a_host);
BENCHMARK(parse_host_of_chrome);
BENCHMARK(parse_quoted_string);
BENCHMARK(parse_field_value_parameter);
BENCHMARK(parse_imf_fixdate);
BENCHMARK(parse_http_date);

} // namespace

BENCHMARK_MAIN();
