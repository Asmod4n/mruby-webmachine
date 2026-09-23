#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <optional>
#include <span>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"
#include "cache_datagram.h"
#include "cache_file.h"
#include "home.hpp"
#include "http.hpp"
#include "http1.hpp"
#include "problem.hpp"
#include "ring.hpp"
#include "stored.hpp"
#include "walk.hpp"
#include "webmachine.hpp"

extern char **environ;

namespace serve
{

inline constexpr webmachine::MediaTypeHandler kHelloTypes[] = {{"text/html; charset=utf-8", "to_html"}};
inline constexpr std::string_view kHelloBody = "<html><body>Hello, World!</body></html>";

class Hello final : public webmachine::Resource
{
  public:
    bool resource_exists(const http1::Request &request) const override
    {
        return request.request_line.request_target == "/hello";
    }
    std::span<const webmachine::MediaTypeHandler> content_types_provided() const override
    {
        return kHelloTypes;
    }
};

class Spelled
{
  public:
    explicit Spelled(const std::span<char> room) : room_(room)
    {
    }
    void add(const std::string_view text)
    {
        if (!fits_ || text.size() > room_.size() - at_) [[unlikely]] {
            fits_ = false;
            return;
        }
        std::ranges::copy(text, room_.begin() + static_cast<std::ptrdiff_t>(at_));
        at_ += text.size();
    }
    void add_number(const size_t number)
    {
        std::array<char, 20> digits{};
        const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), number);
        add(std::string_view(digits.data(), static_cast<size_t>(end - digits.data())));
    }
    size_t size() const
    {
        return fits_ ? at_ : 0;
    }

  private:
    const std::span<char> room_;
    size_t at_ = 0;
    bool fits_ = true;
};

struct Today {
    std::chrono::sys_seconds                   now;
    std::chrono::year                          year;
    std::array<char, http::kFixdateLength>     date;
};

inline void
brought_up_to_date(Today &today)
{
    timespec coarse{};
    clock_gettime(CLOCK_REALTIME_COARSE, &coarse);
    const std::chrono::sys_seconds now{std::chrono::seconds{coarse.tv_sec}};
    if (now == today.now) return;
    today.now = now;
    today.year = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now)}.year();
    today.date = http::spell_imf_fixdate(now);
}

inline void
spelled_head(Spelled &out, const Today &today, const uint16_t status, const std::string_view content_type,
             const size_t body_length, const std::string_view extra_fields)
{
    const auto &date = today.date;
    out.add("HTTP/1.1 ");
    out.add_number(status);
    out.add(" ");
    out.add(home::reason_of(status));
    out.add("\r\nDate: ");
    out.add(std::string_view(date.data(), date.size()));
    if (!content_type.empty()) {
        out.add("\r\nContent-Type: ");
        out.add(content_type);
    }
    out.add("\r\nContent-Length: ");
    out.add_number(body_length);
    out.add("\r\n");
    out.add(extra_fields);
    out.add("\r\n");
}

inline wm::Answered
made(const std::span<char> room, const Today &today, const uint16_t status, const std::string_view content_type,
     const std::string_view body, const bool head, const std::string_view extra_fields, cache_held *const held)
{
    Spelled out(room);
    spelled_head(out, today, status, content_type, body.size(), extra_fields);
    if (!head) out.add(body);
    return {out.size(), {}, held, 0};
}

inline constexpr size_t kSendBodyWithoutCopyAbove = 4096;

inline wm::Answered
stored_made(const std::span<char> room, const Today &today, const std::string_view content_type, const std::string_view body,
            const bool head, cache_held *const held)
{
    Spelled out(room);
    spelled_head(out, today, 200, content_type, body.size(), "Vary: Accept\r\n");
    if (head || body.size() > kSendBodyWithoutCopyAbove) return {out.size(), head ? std::string_view{} : body, held, 0};
    out.add(body);
    return {out.size(), {}, held, 0};
}

inline constexpr std::string_view kAppName = "webmachine-serve";
inline constexpr uint32_t kFreshnessLifetime = 60;

struct Cache {
    cache *of_app;
    cache_reader *of_thread;
    int to_the_writer;
    std::chrono::sys_seconds handed_over_at;
};

inline void
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
    send(to_the_writer, datagram.data(), datagram.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
}

inline void
stored_if_due(Cache &c, const uint64_t route, const std::chrono::sys_seconds now, const std::string_view content_type,
              const std::string_view page)
{
    if (c.to_the_writer < 0 || now < c.handed_over_at + std::chrono::seconds{1}) return;
    c.handed_over_at = now;
    handed_over(c.to_the_writer, route, kCacheFieldStatus, "200");
    handed_over(c.to_the_writer, route, kCacheFieldContentType, content_type);
    handed_over(c.to_the_writer, route, kCacheFieldBody, page);
}

template <class Inner, class Rendering>
wm::Answered
answered_by(const Inner &inner, Rendering rendering, const http1::Request &request, Cache &c, const Today &today,
            const std::span<char> room)
{
    const std::chrono::sys_seconds now = today.now;
    const std::chrono::year year = today.year;
    const bool head = request.request_line.method == http::Method::kHead;
    const std::string_view target = request.request_line.request_target;
    const uint64_t route = cache_key_of(reinterpret_cast<const uint8_t *>(target.data()), target.size());
    cache_held *const held = c.of_thread != nullptr ? cache_taken(c.of_thread) : nullptr;
    const flow::Facts facts = flow::facts_of(request, year);
    const auto outcome = held != nullptr
                             ? flow::walk(stored::Resource<Inner>(inner, held, route, now, year), request, facts)
                             : flow::walk(inner, request, facts);
    if (!outcome) [[unlikely]] {
        const auto page = problem::text_of(problem::details_of(400));
        return made(room, today, 400, problem::kMediaTypeText, page.value_or(""), head, "", held);
    }
    if (outcome->status == 200 && outcome->media_type) {
        const std::string_view content_type = inner.content_types_provided()[*outcome->media_type].media_type;
        const uint64_t seconds = static_cast<uint64_t>(now.time_since_epoch().count());
        const auto stored_body = held != nullptr ? stored::body_of(held, route, seconds) : std::nullopt;
        if (stored_body) return stored_made(room, today, content_type, *stored_body, head, held);
        const std::string page = rendering(request, *outcome);
        stored_if_due(c, route, now, content_type, page);
        return made(room, today, 200, content_type, page, head, "Vary: Accept\r\n", held);
    }
    if (outcome->status < 400) return made(room, today, outcome->status, "", "", head, "", held);
    const problem::Details details = problem::details_of(outcome->status);
    const auto form = problem::form_of(flow::is_present(facts.accept) ? std::optional(facts.accept) : std::nullopt, false);
    const problem::Form chosen = form.value_or(problem::Form::kText);
    std::optional<std::string> page;
    switch (chosen) {
        case problem::Form::kHtml: page = problem::html_of(details, std::nullopt); break;
        case problem::Form::kProblemJson:
        case problem::Form::kJson: page = problem::json_of(details); break;
        case problem::Form::kProblemXml: page = problem::xml_of(details); break;
        default: page = problem::text_of(details); break;
    }
    return made(room, today, outcome->status, problem::media_type_of(chosen), page.value_or(""), head, "Vary: Accept\r\n",
                held);
}

inline wm::Answered
answer_to(const http1::Request &request, Cache &c, const Today &today, const std::span<char> room)
{
    if (request.request_line.request_target == "/hello")
        return answered_by(
            Hello{}, [](const http1::Request &, const flow::Outcome &) { return std::string(kHelloBody); }, request, c,
            today, room);
    return answered_by(home::Home{}, home::page_of, request, c, today, room);
}

inline pid_t
the_writer_stands(const std::filesystem::path &writer_path, const std::filesystem::path &directory, Cache &c)
{
    const std::string writer = writer_path.string();
    char *const file = cache_file_of(std::string(kAppName).c_str(), directory.c_str());
    if (file == nullptr) [[unlikely]] return -1;
    const std::string file_name(file);
    std::free(file);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) [[unlikely]] return -1;
    const int theirs = fcntl(pair[1], F_DUPFD_CLOEXEC, 4);
    close(pair[1]);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, theirs, 3);
    posix_spawn_file_actions_addclosefrom_np(&actions, 4);
    char *child[] = {const_cast<char *>(writer.c_str()), const_cast<char *>(file_name.c_str()),
                     const_cast<char *>("1"),            const_cast<char *>("0"),
                     const_cast<char *>("64"),           const_cast<char *>("1"),
                     const_cast<char *>("536870912"),    nullptr};
    pid_t spawned = 0;
    const int spawning = posix_spawn(&spawned, writer.c_str(), &actions, nullptr, child, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(theirs);
    if (spawning != 0) [[unlikely]] {
        close(pair[0]);
        return -1;
    }
    int32_t standing = 0;
    if (recv(pair[0], &standing, sizeof standing, 0) != sizeof standing || standing <= 0) [[unlikely]] {
        close(pair[0]);
        waitpid(spawned, nullptr, 0);
        return -1;
    }
    c.to_the_writer = pair[0];
    c.of_app = cache_open(std::string(kAppName).c_str(), directory.c_str(), 64);
    if (c.of_app != nullptr) c.of_thread = cache_reader_opened(c.of_app);
    return spawned;
}


inline wm::Answered
answered(const std::string_view asked, const std::span<char> room, Cache &c, const Today &today)
{
    const size_t bytes = http1::bytes_before_the_body(asked);
    if (bytes == 0) return {0, {}, nullptr, 0};
    const std::expected<http1::Request, http::Refusal> request = http1::parse_request(asked);
    if (!request) [[unlikely]] {
        wm::Answered refused = made(room, today, 400, problem::kMediaTypeText,
                                    problem::text_of(problem::details_of(400)).value_or(""), false, "", nullptr);
        refused.taken = bytes;
        return refused;
    }
    wm::Answered answer = answer_to(*request, c, today, room);
    answer.taken = request->bytes;
    return answer;
}

}
