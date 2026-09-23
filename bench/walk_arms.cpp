#include <benchmark/benchmark.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/cache.h"
#include "../src/cache_datagram.h"
#include "../src/home.hpp"
#include "../src/http1.hpp"
#include "../src/stored.hpp"
#include "../src/walk.hpp"

extern char **environ;

namespace
{

constexpr std::string_view kAsked = "GET / HTTP/1.1\r\n"
                                    "Host: example.com\r\n"
                                    "User-Agent: Mozilla/5.0 (X11; Linux x86_64)\r\n"
                                    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                                    "Accept-Language: en-US,en;q=0.9,de;q=0.8\r\n"
                                    "Accept-Encoding: gzip, deflate, br, zstd\r\n"
                                    "\r\n";

constexpr std::string_view kAskedLikeHtgen = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

constexpr std::string_view kRoute = "/";
constexpr std::string_view kEntityTag = "\"home-1\"";
constexpr std::string_view kLastModified = "Tue, 22 Sep 2026 10:00:00 GMT";
constexpr uint32_t kFreshnessLifetime = 3600;

const http1::Request &
the_request()
{
    static const http1::Request request = [] {
        const auto parsed = http1::parse_request(kAsked);
        if (!parsed) [[unlikely]] std::abort();
        return *parsed;
    }();
    return request;
}

std::chrono::sys_seconds
now_is()
{
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

std::chrono::year
year_of(const std::chrono::sys_seconds now)
{
    return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now)}.year();
}

std::string
head_of(const uint16_t status, const std::string_view content_type, const size_t body_length)
{
    const auto date = http::spell_imf_fixdate(now_is());
    std::string head;
    head.reserve(256);
    head += "HTTP/1.1 ";
    head += std::to_string(status);
    head += ' ';
    head += home::reason_of(status);
    head += "\r\nDate: ";
    head.append(date.data(), date.size());
    head += "\r\nContent-Type: ";
    head += content_type;
    head += "\r\nContent-Length: ";
    head += std::to_string(body_length);
    head += "\r\nVary: Accept\r\n\r\n";
    return head;
}

void
handed_over(const int to_the_writer, const uint64_t route, const uint8_t field, const std::string_view value)
{
    cache_datagram_header header = {};
    header.route = route;
    header.field = field;
    header.freshness_lifetime = kFreshnessLifetime;
    header.body = kCacheBodyIsInline;
    const auto bytes = std::bit_cast<std::array<char, sizeof header>>(header);
    std::string datagram(bytes.begin(), bytes.end());
    datagram += value;
    if (send(to_the_writer, datagram.data(), datagram.size(), 0) != static_cast<ssize_t>(datagram.size()))
        [[unlikely]] std::abort();
}

struct Filled {
    cache *of_app;
    cache_reader *of_thread;
    uint64_t route;
    std::string page;
};

Filled &
filled()
{
    static Filled made = [] {
        const char *const writer = std::getenv("WM_CACHE_WRITER") != nullptr
                                       ? std::getenv("WM_CACHE_WRITER")
                                       : "mruby/build/release/bin/webmachine-cache";
        const std::string file = "/tmp/wm-bench-walk.mdb";
        std::remove(file.c_str());
        std::remove((file + "-lock").c_str());

        const auto now = now_is();
        const auto outcome = flow::walk(home::Home{}, the_request(), flow::facts_of(the_request(), year_of(now)));
        if (!outcome || outcome->status != 200) [[unlikely]] std::abort();
        std::string page = home::page_of(the_request(), *outcome);

        int pair[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) [[unlikely]] std::abort();
        const int theirs = fcntl(pair[1], F_DUPFD_CLOEXEC, 4);
        close(pair[1]);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, theirs, 3);
        posix_spawn_file_actions_addclosefrom_np(&actions, 4);
        char *child[] = {const_cast<char *>(writer), const_cast<char *>(file.c_str()), const_cast<char *>("1"),
                         const_cast<char *>("0"),    const_cast<char *>("64"),         const_cast<char *>("2"),
                         const_cast<char *>("536870912"), nullptr};
        pid_t spawned = 0;
        if (posix_spawn(&spawned, writer, &actions, nullptr, child, environ) != 0) [[unlikely]] std::abort();
        posix_spawn_file_actions_destroy(&actions);
        close(theirs);
        int32_t standing = 0;
        if (recv(pair[0], &standing, sizeof standing, 0) != sizeof standing || standing <= 0) [[unlikely]]
            std::abort();
        if (page.size() + sizeof(cache_datagram_header) + 256 > static_cast<size_t>(standing)) [[unlikely]]
            std::abort();

        const uint64_t route = cache_key_of(reinterpret_cast<const uint8_t *>(kRoute.data()), kRoute.size());
        handed_over(pair[0], route, kCacheFieldStatus, "200");
        handed_over(pair[0], route, kCacheFieldEntityTag, kEntityTag);
        handed_over(pair[0], route, kCacheFieldLastModified, kLastModified);
        handed_over(pair[0], route, kCacheFieldContentType, home::kHomeTypes[0].media_type);
        handed_over(pair[0], route, kCacheFieldBody, page);
        close(pair[0]);
        int left = 0;
        waitpid(spawned, &left, 0);
        if (!WIFEXITED(left) || WEXITSTATUS(left) != 0) [[unlikely]] std::abort();

        cache *const of_app = cache_open("wm-bench-walk", "/tmp", 64);
        if (of_app == nullptr) [[unlikely]] std::abort();
        cache_reader *const of_thread = cache_reader_opened(of_app);
        if (of_thread == nullptr) [[unlikely]] std::abort();
        return Filled{of_app, of_thread, route, std::move(page)};
    }();
    return made;
}

void
BM_walk_alone(benchmark::State &state)
{
    const http1::Request &request = the_request();
    const home::Home resource;
    for (auto _ : state) {
        const auto now = now_is();
        const auto outcome = flow::walk(resource, request, flow::facts_of(request, year_of(now)));
        if (!outcome || outcome->status != 200) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(outcome->status);
    }
}

void
BM_walk_alone_asked_like_htgen(benchmark::State &state)
{
    static const http1::Request request = [] {
        const auto parsed = http1::parse_request(kAskedLikeHtgen);
        if (!parsed) [[unlikely]] std::abort();
        return *parsed;
    }();
    const home::Home resource;
    for (auto _ : state) {
        const auto now = now_is();
        const auto outcome = flow::walk(resource, request, flow::facts_of(request, year_of(now)));
        if (!outcome || outcome->status != 200) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(outcome->status);
    }
}

void
BM_walk_then_render(benchmark::State &state)
{
    const http1::Request &request = the_request();
    const home::Home resource;
    const size_t expected = filled().page.size();
    for (auto _ : state) {
        const auto now = now_is();
        const auto outcome = flow::walk(resource, request, flow::facts_of(request, year_of(now)));
        if (!outcome || outcome->status != 200) [[unlikely]] std::abort();
        const std::string page = home::page_of(request, *outcome);
        const std::string head = head_of(200, home::kHomeTypes[*outcome->media_type].media_type, page.size());
        if (page.size() != expected) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(page.data());
        benchmark::DoNotOptimize(head.data());
    }
}

void
BM_walk_then_cache(benchmark::State &state)
{
    Filled &cached = filled();
    const http1::Request &request = the_request();
    const home::Home inner;
    for (auto _ : state) {
        const auto now = now_is();
        cache_held *const held = cache_taken(cached.of_thread);
        if (held == nullptr) [[unlikely]] std::abort();
        const stored::Resource resource(inner, held, cached.route, now, year_of(now));
        const auto outcome = flow::walk(resource, request, flow::facts_of(request, year_of(now)));
        if (!outcome || outcome->status != 200) [[unlikely]] std::abort();
        const auto body = stored::body_of(held, cached.route, static_cast<uint64_t>(now.time_since_epoch().count()));
        if (!body || body->size() != cached.page.size()) [[unlikely]] std::abort();
        const std::string head = head_of(200, home::kHomeTypes[*outcome->media_type].media_type, body->size());
        benchmark::DoNotOptimize(body->data());
        benchmark::DoNotOptimize(head.data());
        cache_sent(cached.of_thread, held);
    }
}

void
BM_walk_then_cache_answers_304(benchmark::State &state)
{
    Filled &cached = filled();
    static const std::string asked = [] {
        std::string text(kAsked.substr(0, kAsked.size() - 2));
        text += "If-None-Match: ";
        text += kEntityTag;
        text += "\r\n\r\n";
        return text;
    }();
    static const http1::Request request = [] {
        const auto parsed = http1::parse_request(asked);
        if (!parsed) [[unlikely]] std::abort();
        return *parsed;
    }();
    const home::Home inner;
    for (auto _ : state) {
        const auto now = now_is();
        cache_held *const held = cache_taken(cached.of_thread);
        if (held == nullptr) [[unlikely]] std::abort();
        const stored::Resource resource(inner, held, cached.route, now, year_of(now));
        const auto outcome = flow::walk(resource, request, flow::facts_of(request, year_of(now)));
        if (!outcome || outcome->status != 304) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(outcome->status);
        cache_sent(cached.of_thread, held);
    }
}

}

BENCHMARK(BM_walk_alone);
BENCHMARK(BM_walk_alone_asked_like_htgen);
BENCHMARK(BM_walk_then_render);
BENCHMARK(BM_walk_then_cache);
BENCHMARK(BM_walk_then_cache_answers_304);
