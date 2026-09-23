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

struct HelloFields {
    std::string_view greeting;
};

constexpr HelloFields
hello_fields()
{
    return {"Hello, World!"};
}

inline constexpr char kHelloSource[] = "<html><body>{{greeting}}</body></html>";

#if defined(__cpp_impl_reflection)
inline constexpr auto kHelloPage = mustache::static_page_of<mustache::fixed_string{kHelloSource}, hello_fields>;
inline constexpr std::string_view kHelloBody{
    kHelloPage.data(), mustache::static_length<mustache::fixed_string{kHelloSource}, hello_fields>()};
#else
inline constexpr std::string_view kHelloBody = "<html><body>Hello, World!</body></html>";
#endif

class Hello final : public webmachine::Resource
{
  public:
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
    int64_t                                    tick;
};

inline void
brought_up_to_date(Today &today)
{
    timespec coarse{};
    clock_gettime(CLOCK_REALTIME_COARSE, &coarse);
    today.tick = static_cast<int64_t>(coarse.tv_sec) * 1000000000 + coarse.tv_nsec;
    const std::chrono::sys_seconds now{std::chrono::seconds{coarse.tv_sec}};
    if (now == today.now) return;
    today.now = now;
    today.year = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now)}.year();
    today.date = http::spell_imf_fixdate(now);
}

inline constexpr size_t kHeadBytesMost = 256;
inline constexpr size_t kHeadsKept = 4;

struct Head {
    std::chrono::sys_seconds             at;
    uint16_t                             status;
    std::string_view                     content_type;
    size_t                               body_length;
    std::string_view                     extra_fields;
    size_t                               size;
    std::array<char, kHeadBytesMost>     bytes;
};

struct Heads {
    std::array<Head, kHeadsKept> kept{};
    size_t                       next = 0;
};

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

inline void
head_added(Spelled &out, Heads &heads, const Today &today, const uint16_t status, const std::string_view content_type,
           const size_t body_length, const std::string_view extra_fields)
{
    for (const Head &h : heads.kept)
        if (h.size != 0 && h.at == today.now && h.status == status && h.body_length == body_length &&
            h.content_type.data() == content_type.data() && h.content_type.size() == content_type.size() &&
            h.extra_fields.data() == extra_fields.data() && h.extra_fields.size() == extra_fields.size()) {
            out.add(std::string_view(h.bytes.data(), h.size));
            return;
        }
    Head &h = heads.kept[heads.next];
    heads.next = (heads.next + 1) % kHeadsKept;
    Spelled into(h.bytes);
    spelled_head(into, today, status, content_type, body_length, extra_fields);
    h = Head{today.now, status, content_type, body_length, extra_fields, into.size(), h.bytes};
    if (h.size == 0) [[unlikely]] {
        spelled_head(out, today, status, content_type, body_length, extra_fields);
        return;
    }
    out.add(std::string_view(h.bytes.data(), h.size));
}

inline wm::Answered
made(const std::span<char> room, const Today &today, const uint16_t status, const std::string_view content_type,
     const std::string_view body, const bool head, const std::string_view extra_fields, cache_held *const held,
     const size_t taken)
{
    Spelled out(room);
    spelled_head(out, today, status, content_type, body.size(), extra_fields);
    if (!head) out.add(body);
    return {out.size(), {}, held, taken};
}

inline constexpr size_t kSendBodyWithoutCopyAbove = 4096;

inline constexpr std::string_view kVaryAccept = "Vary: Accept\r\n";

inline constexpr std::array<std::string_view, 16> kVaryFields = {{
    "",
    "Vary: Accept\r\n",
    "Vary: Accept-Charset\r\n",
    "Vary: Accept, Accept-Charset\r\n",
    "Vary: Accept-Encoding\r\n",
    "Vary: Accept, Accept-Encoding\r\n",
    "Vary: Accept-Charset, Accept-Encoding\r\n",
    "Vary: Accept, Accept-Charset, Accept-Encoding\r\n",
    "Vary: Accept-Language\r\n",
    "Vary: Accept, Accept-Language\r\n",
    "Vary: Accept-Charset, Accept-Language\r\n",
    "Vary: Accept, Accept-Charset, Accept-Language\r\n",
    "Vary: Accept-Encoding, Accept-Language\r\n",
    "Vary: Accept, Accept-Encoding, Accept-Language\r\n",
    "Vary: Accept-Charset, Accept-Encoding, Accept-Language\r\n",
    "Vary: Accept, Accept-Charset, Accept-Encoding, Accept-Language\r\n",
}};

template <class Inner>
std::string_view
vary_of(const Inner &inner, std::string &spelled)
{
    const size_t offered = (inner.content_types_provided().size() > 1 ? 1u : 0u) |
                           (inner.charsets_provided().size() > 1 ? 2u : 0u) |
                           (inner.encodings_provided().size() > 1 ? 4u : 0u) |
                           (inner.languages_provided().size() > 1 ? 8u : 0u);
    const std::span<const std::string_view> variances = inner.variances();
    if (variances.empty()) [[likely]] return kVaryFields[offered];
    spelled = kVaryFields[offered].empty() ? std::string("Vary: ")
                                           : std::string(kVaryFields[offered].substr(0, kVaryFields[offered].size() - 2)) + ", ";
    for (size_t at = 0; at < variances.size(); at++) {
        if (at > 0) spelled += ", ";
        spelled += variances[at];
    }
    spelled += "\r\n";
    return spelled;
}

inline wm::Answered
stored_made(const std::span<char> room, Heads &heads, const Today &today, const std::string_view content_type,
            const std::string_view vary, const std::string_view body, const bool head, cache_held *const held,
            const size_t taken)
{
    Spelled out(room);
    head_added(out, heads, today, 200, content_type, body.size(), vary);
    if (head || body.size() > kSendBodyWithoutCopyAbove)
        return {out.size(), head ? std::string_view{} : body, held, taken};
    out.add(body);
    return {out.size(), {}, held, taken};
}

inline wm::Answered
constant_made(const std::span<char> room, Heads &heads, const Today &today, const std::string_view content_type,
              const std::string_view vary, const std::string_view body, const bool head, const size_t taken)
{
    Spelled out(room);
    head_added(out, heads, today, 200, content_type, body.size(), vary);
    return {out.size(), head ? std::string_view{} : body, nullptr, taken};
}

inline constexpr std::string_view kAppName = "webmachine-serve";
inline constexpr uint32_t kFreshnessLifetime = 60;

inline constexpr size_t kWindows = 8;
inline constexpr size_t kRememberedRoutes = 8;

struct Remembered {
    uint64_t                        route;
    std::optional<std::string_view> body;
};

struct Window {
    cache_held                                   *held;
    uint32_t                                      users;
    int64_t                                       tick;
    std::array<Remembered, kRememberedRoutes>     remembered;
    size_t                                        remembered_count;
};

struct Cache {
    cache                          *of_app;
    cache_reader                   *of_thread;
    int                             to_the_writer;
    std::chrono::sys_seconds        handed_over_at;
    std::array<Window, kWindows>    windows{};
    size_t                          current = kWindows;
    Heads                           heads{};
    std::string                     vary{};
};

inline cache_held *
taken(Cache &c, const Today &today)
{
    if (c.of_thread == nullptr) return nullptr;
    if (c.current < kWindows) {
        Window &open = c.windows[c.current];
        if (open.tick == today.tick) {
            open.users++;
            return open.held;
        }
        if (open.users == 0) {
            cache_sent(c.of_thread, open.held);
            open = Window{};
        }
        c.current = kWindows;
    }
    for (size_t at = 0; at < kWindows; at++) {
        Window &free = c.windows[at];
        if (free.held != nullptr) continue;
        cache_held *const held = cache_taken(c.of_thread);
        if (held == nullptr) [[unlikely]] return nullptr;
        free = Window{held, 1, today.tick, {}, 0};
        c.current = at;
        return held;
    }
    return cache_taken(c.of_thread);
}

inline void
released(Cache &c, cache_held *const held)
{
    for (size_t at = 0; at < kWindows; at++) {
        Window &w = c.windows[at];
        if (w.held != held) continue;
        if (--w.users == 0 && at != c.current) {
            cache_sent(c.of_thread, held);
            w = Window{};
        }
        return;
    }
    cache_sent(c.of_thread, held);
}

inline std::optional<std::string_view>
body_remembered(Cache &c, cache_held *const held, const uint64_t route, const uint64_t seconds)
{
    Window *const open = c.current < kWindows && c.windows[c.current].held == held ? &c.windows[c.current] : nullptr;
    if (open != nullptr)
        for (size_t at = 0; at < open->remembered_count; at++)
            if (open->remembered[at].route == route) return open->remembered[at].body;
    const std::optional<std::string_view> body = stored::body_of(held, route, seconds);
    if (open != nullptr && open->remembered_count < kRememberedRoutes)
        open->remembered[open->remembered_count++] = Remembered{route, body};
    return body;
}

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

[[gnu::cold, gnu::noinline]] inline wm::Answered
problem_made(const std::span<char> room, const Today &today, const uint16_t status, const std::string_view accept,
             const bool head, cache_held *const held, const size_t taken)
{
    const problem::Details details = problem::details_of(status);
    const auto form = problem::form_of(flow::is_present(accept) ? std::optional(accept) : std::nullopt, false);
    const problem::Form chosen = form.value_or(problem::Form::kText);
    std::optional<std::string> page;
    switch (chosen) {
        case problem::Form::kHtml: page = problem::html_of(details, std::nullopt); break;
        case problem::Form::kProblemJson:
        case problem::Form::kJson: page = problem::json_of(details); break;
        case problem::Form::kProblemXml: page = problem::xml_of(details); break;
        default: page = problem::text_of(details); break;
    }
    return made(room, today, status, problem::media_type_of(chosen), page.value_or(""), head, kVaryAccept, held, taken);
}

template <class Rendering>
[[gnu::cold, gnu::noinline]] wm::Answered
rendered(Rendering rendering, const http1::Request &request, const flow::Outcome &outcome, Cache &c,
         const uint64_t route, const Today &today, const std::string_view content_type, const std::string_view vary,
         const bool head, cache_held *const held, const std::span<char> room)
{
    const std::string page = rendering(request, outcome);
    stored_if_due(c, route, today.now, content_type, page);
    return made(room, today, 200, content_type, page, head, vary, held, request.bytes);
}

template <class Inner, class Rendering>
wm::Answered
answered_by(const Inner &inner, Rendering rendering, const http1::Request &request, const flow::Facts &facts, Cache &c,
            const Today &today, const std::span<char> room)
{
    const std::chrono::sys_seconds now = today.now;
    const std::chrono::year year = today.year;
    const bool head = request.request_line.method == http::Method::kHead;
    const std::string_view target = request.request_line.request_target;
    const uint64_t route = cache_key_of(reinterpret_cast<const uint8_t *>(target.data()), target.size());
    cache_held *const held = taken(c, today);
    const auto outcome = held != nullptr
                             ? flow::walk(stored::Resource<Inner>(inner, held, route, now, year), request, facts)
                             : flow::walk(inner, request, facts);
    if (!outcome) [[unlikely]] return problem_made(room, today, 400, {}, head, held, request.bytes);
    if (outcome->status == 200 && outcome->media_type) {
        const std::string_view content_type = inner.content_types_provided()[*outcome->media_type].media_type;
        const uint64_t seconds = static_cast<uint64_t>(now.time_since_epoch().count());
        const auto stored_body = held != nullptr ? body_remembered(c, held, route, seconds) : std::nullopt;
        const std::string_view vary = vary_of(inner, c.vary);
        if (stored_body)
            return stored_made(room, c.heads, today, content_type, vary, *stored_body, head, held, request.bytes);
        return rendered(rendering, request, *outcome, c, route, today, content_type, vary, head, held, room);
    }
    if (outcome->status < 400) return made(room, today, outcome->status, "", "", head, "", held, request.bytes);
    return problem_made(room, today, outcome->status, facts.accept, head, held, request.bytes);
}

template <class Inner>
wm::Answered
answered_constant(const Inner &inner, const std::string_view body, const http1::Request &request,
                  const flow::Facts &facts, Cache &c, const Today &today, const std::span<char> room)
{
    const bool head = request.request_line.method == http::Method::kHead;
    const auto outcome = flow::walk(inner, request, facts);
    if (!outcome) [[unlikely]] return problem_made(room, today, 400, {}, head, nullptr, request.bytes);
    if (outcome->status == 200 && outcome->media_type) {
        const std::string_view content_type = inner.content_types_provided()[*outcome->media_type].media_type;
        return constant_made(room, c.heads, today, content_type, vary_of(inner, c.vary), body, head, request.bytes);
    }
    if (outcome->status < 400) return made(room, today, outcome->status, "", "", head, "", nullptr, request.bytes);
    return problem_made(room, today, outcome->status, facts.accept, head, nullptr, request.bytes);
}

inline wm::Answered
answer_to(const http1::Request &request, const flow::Facts &facts, Cache &c, const Today &today,
          const std::span<char> room)
{
    if (http::spelled_as(request.request_line.request_target, "/hello"))
        return answered_constant(Hello{}, kHelloBody, request, facts, c, today, room);
    return answered_by(home::Home{}, home::page_of, request, facts, c, today, room);
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


[[gnu::cold, gnu::noinline]] inline wm::Answered
refused(const std::span<char> room, const Today &today, const size_t bytes)
{
    return made(room, today, 400, problem::kMediaTypeText, problem::text_of(problem::details_of(400)).value_or(""), false,
                "", nullptr, bytes);
}

[[gnu::cold, gnu::noinline]] inline wm::Answered
not_answered(const std::string_view asked, const std::span<char> room, const Today &today,
             const http::Refusal refusal)
{
    if (refusal.problem == http1::kNotWholeYet) return {0, {}, nullptr, 0};
    const size_t bytes = http1::bytes_before_the_body(asked);
    return refused(room, today, bytes != 0 ? bytes : asked.size());
}

inline wm::Answered
answered(const std::string_view asked, const std::span<char> room, Cache &c, const Today &today)
{
    flow::Facts facts{};
    facts.current_year = today.year;
    const std::expected<http1::Request, http::Refusal> request =
        http1::parse_request(asked, [&facts](const http1::FieldLine &field) { flow::fact_taken(facts, field); });
    if (!request) [[unlikely]] return not_answered(asked, room, today, request.error());
    facts.method = request->request_line.method;
    facts.request_target = request->request_line.request_target;
    return answer_to(*request, facts, c, today, room);
}

}
