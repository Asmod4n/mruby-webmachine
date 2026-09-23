#include <benchmark/benchmark.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "../src/serve.hpp"

namespace
{

struct HeadFields {
    std::string_view status;
    std::string_view reason;
    std::string_view date;
    std::string_view content_type;
    std::string_view content_length;
};

inline constexpr char kHeadSource[] = "HTTP/1.1 {{{status}}} {{{reason}}}\r\n"
                                      "Date: {{{date}}}\r\n"
                                      "Content-Type: {{{content_type}}}\r\n"
                                      "Content-Length: {{{content_length}}}\r\n"
                                      "\r\n";

constexpr std::string_view kContentType = "text/html; charset=utf-8";
constexpr size_t kLength = 39;

serve::Today
today_now()
{
    serve::Today today{};
    serve::brought_up_to_date(today);
    return today;
}

size_t
head_by_template(const std::span<char> room, const serve::Today &today)
{
    std::array<char, 20> digits{};
    const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), kLength);
    const HeadFields fields{"200", "OK", std::string_view(today.date.data(), today.date.size()), kContentType,
                            std::string_view(digits.data(), static_cast<size_t>(end - digits.data()))};
    mustache::Out out{room.data(), room.data() + room.size() - mustache::kEscapeSlack};
    mustache::render<mustache::fixed_string{kHeadSource}>(out, fields);
    return out.full ? 0 : static_cast<size_t>(out.w - room.data());
}

size_t
head_spelled(const std::span<char> room, const serve::Today &today)
{
    serve::Spelled out(room);
    serve::spelled_head(out, today, 200, kContentType, kLength, "");
    return out.size();
}

size_t
head_kept(const std::span<char> room, serve::Heads &heads, const serve::Today &today)
{
    serve::Spelled out(room);
    serve::head_added(out, heads, today, 200, kContentType, kLength, "");
    return out.size();
}

inline constexpr std::string_view kPrefix = "HTTP/1.1 200 OK\r\nDate: ";
inline constexpr std::string_view kSuffix = "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 39\r\n\r\n";

size_t
head_in_pieces(const std::span<char> room, const serve::Today &today)
{
    char *w = room.data();
    w = std::copy(kPrefix.begin(), kPrefix.end(), w);
    w = std::copy(today.date.begin(), today.date.end(), w);
    w = std::copy(kSuffix.begin(), kSuffix.end(), w);
    return static_cast<size_t>(w - room.data());
}

const std::string &
the_head()
{
    static const std::string spelled = [] {
        std::array<char, 512> room{};
        const serve::Today today = today_now();
        return std::string(room.data(), head_spelled(room, today));
    }();
    return spelled;
}

void
same_or_abort(const std::span<const char> room, const size_t size)
{
    const serve::Today today = today_now();
    std::array<char, 512> spelled{};
    const size_t n = head_spelled(spelled, today);
    if (std::string_view(room.data(), size) != std::string_view(spelled.data(), n)) [[unlikely]] std::abort();
}

void
BM_head_by_template(benchmark::State &state)
{
    std::array<char, 512> room{};
    const serve::Today today = today_now();
    same_or_abort(room, head_by_template(room, today));
    for (auto _ : state) {
        size_t n = head_by_template(room, today);
        benchmark::DoNotOptimize(n);
        benchmark::DoNotOptimize(room.data());
    }
}

void
BM_head_spelled(benchmark::State &state)
{
    std::array<char, 512> room{};
    const serve::Today today = today_now();
    same_or_abort(room, head_spelled(room, today));
    for (auto _ : state) {
        size_t n = head_spelled(room, today);
        benchmark::DoNotOptimize(n);
        benchmark::DoNotOptimize(room.data());
    }
}

void
BM_head_kept(benchmark::State &state)
{
    std::array<char, 512> room{};
    serve::Heads heads{};
    const serve::Today today = today_now();
    same_or_abort(room, head_kept(room, heads, today));
    for (auto _ : state) {
        size_t n = head_kept(room, heads, today);
        benchmark::DoNotOptimize(n);
        benchmark::DoNotOptimize(room.data());
    }
}

void
BM_head_in_pieces(benchmark::State &state)
{
    std::array<char, 512> room{};
    const serve::Today today = today_now();
    same_or_abort(room, head_in_pieces(room, today));
    for (auto _ : state) {
        size_t n = head_in_pieces(room, today);
        benchmark::DoNotOptimize(n);
        benchmark::DoNotOptimize(room.data());
    }
}

}

BENCHMARK(BM_head_by_template);
BENCHMARK(BM_head_spelled);
BENCHMARK(BM_head_kept);
BENCHMARK(BM_head_in_pieces);
