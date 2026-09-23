#include <benchmark/benchmark.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/cache.h"
#include "../src/serve.hpp"

namespace
{

constexpr std::string_view kAskedForHello = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
constexpr std::string_view kAskedForHome = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

serve::Cache &
the_cache()
{
    static serve::Cache made = [] {
        serve::Cache c{nullptr, nullptr, -1, {}};
        const char *const writer = std::getenv("WM_CACHE_WRITER") != nullptr
                                       ? std::getenv("WM_CACHE_WRITER")
                                       : "mruby/build/release/bin/webmachine-cache";
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wm-bench-answer";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        if (serve::the_writer_stands(writer, directory, c) < 0 || c.of_thread == nullptr) [[unlikely]] std::abort();
        return c;
    }();
    return made;
}

bool
stored(serve::Cache &c, const serve::Today &today, const std::string_view target)
{
    cache_held *const held = cache_taken(c.of_thread);
    const uint64_t route = cache_key_of(reinterpret_cast<const uint8_t *>(target.data()), target.size());
    const bool there =
        cache_body_asked(held, route, static_cast<uint64_t>(today.now.time_since_epoch().count())).value != nullptr;
    cache_sent(c.of_thread, held);
    return there;
}

void
answered_until_stored(serve::Cache &c, serve::Today &today, const std::string_view asked,
                      const std::string_view target)
{
    std::array<char, wm::kAnswerBytes> room{};
    for (int tries = 0; tries < 200; tries++) {
        serve::brought_up_to_date(today);
        const wm::Answered answer = serve::answered(asked, room, c, today);
        if (answer.held != nullptr) serve::released(c, static_cast<cache_held *>(const_cast<void *>(answer.held)));
        if (stored(c, today, target)) return;
        c.handed_over_at = {};
        usleep(10000);
    }
    std::abort();
}

void
answered_from_the_cache(benchmark::State &state, const std::string_view asked, const std::string_view target)
{
    serve::Cache &c = the_cache();
    serve::Today today{};
    answered_until_stored(c, today, asked, target);
    std::array<char, wm::kAnswerBytes> room{};
    for (auto _ : state) {
        serve::brought_up_to_date(today);
        const wm::Answered answer = serve::answered(asked, room, c, today);
        if (answer.head == 0 || answer.held == nullptr) [[unlikely]] std::abort();
        size_t head = answer.head;
        benchmark::DoNotOptimize(head);
        serve::released(c, static_cast<cache_held *>(const_cast<void *>(answer.held)));
    }
}

void
BM_answered_hello(benchmark::State &state)
{
    answered_from_the_cache(state, kAskedForHello, "/hello");
}

void
BM_answered_home(benchmark::State &state)
{
    answered_from_the_cache(state, kAskedForHome, "/");
}

}

BENCHMARK(BM_answered_hello);
BENCHMARK(BM_answered_home);
