#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#include <slipstream_syscall.h>

#include "../../src/cache.h"
#include "../../src/cache_datagram.h"
#include "../../src/cache_file.h"
#include "../../src/home.hpp"
#include "../../src/http1.hpp"
#include "../../src/problem.hpp"
#include "../../src/ring.hpp"
#include "../../src/stored.hpp"
#include "../../src/walk.hpp"
#include "../../src/webmachine.hpp"

namespace
{

using home::Home;
using home::kHomeTypes;
using home::page_of;
using home::reason_of;

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

void
spelled_head(Spelled &out, const uint16_t status, const std::string_view content_type, const size_t body_length,
             const std::string_view extra_fields)
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto date = http::spell_imf_fixdate(now);
    out.add("HTTP/1.1 ");
    out.add_number(status);
    out.add(" ");
    out.add(reason_of(status));
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

wm::Answered
made(const std::span<char> room, const uint16_t status, const std::string_view content_type,
     const std::string_view body, const bool head, const std::string_view extra_fields, cache_held *const held)
{
    Spelled out(room);
    spelled_head(out, status, content_type, body.size(), extra_fields);
    if (!head) out.add(body);
    return {out.size(), {}, held, 0};
}

wm::Answered
stored_made(const std::span<char> room, const std::string_view content_type, const std::string_view body,
            const bool head, cache_held *const held)
{
    Spelled out(room);
    spelled_head(out, 200, content_type, body.size(), "Vary: Accept\r\n");
    return {out.size(), head ? std::string_view{} : body, held, 0};
}

constexpr std::string_view kAppName = "webmachine-serve";
constexpr uint32_t kFreshnessLifetime = 60;

struct Cache {
    cache *of_app;
    cache_reader *of_thread;
    int to_the_writer;
    std::chrono::sys_seconds handed_over_at;
};

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
    send(to_the_writer, datagram.data(), datagram.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
}

void
stored_if_due(Cache &c, const uint64_t route, const std::chrono::sys_seconds now, const std::string_view content_type,
              const std::string_view page)
{
    if (c.to_the_writer < 0 || now < c.handed_over_at + std::chrono::seconds{1}) return;
    c.handed_over_at = now;
    handed_over(c.to_the_writer, route, kCacheFieldStatus, "200");
    handed_over(c.to_the_writer, route, kCacheFieldContentType, content_type);
    handed_over(c.to_the_writer, route, kCacheFieldBody, page);
}

wm::Answered
answer_to(const http1::Request &request, Cache &c, const std::span<char> room)
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::chrono::year year = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now)}.year();
    const bool head = request.request_line.method == http::Method::kHead;
    const std::string_view target = request.request_line.request_target;
    const uint64_t route = cache_key_of(reinterpret_cast<const uint8_t *>(target.data()), target.size());
    const Home home;
    cache_held *const held = c.of_thread != nullptr ? cache_taken(c.of_thread) : nullptr;
    const stored::Resource resource(home, held, route, now, year);
    const webmachine::Resource &asked =
        held != nullptr ? static_cast<const webmachine::Resource &>(resource) : static_cast<const webmachine::Resource &>(home);
    const flow::Facts facts = flow::facts_of(request, year);
    const auto outcome = flow::walk(asked, request, facts);
    if (!outcome) [[unlikely]] {
        const auto page = problem::text_of(problem::details_of(400));
        return made(room, 400, problem::kMediaTypeText, page.value_or(""), head, "", held);
    }
    if (outcome->status == 200 && outcome->media_type) {
        const std::string_view content_type = kHomeTypes[*outcome->media_type].media_type;
        const uint64_t seconds = static_cast<uint64_t>(now.time_since_epoch().count());
        const auto stored_body = held != nullptr ? stored::body_of(held, route, seconds) : std::nullopt;
        if (stored_body) return stored_made(room, content_type, *stored_body, head, held);
        const std::string page = page_of(request, *outcome);
        stored_if_due(c, route, now, content_type, page);
        return made(room, 200, content_type, page, head, "Vary: Accept\r\n", held);
    }
    if (outcome->status < 400) return made(room, outcome->status, "", "", head, "", held);
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
    return made(room, outcome->status, problem::media_type_of(chosen), page.value_or(""), head, "Vary: Accept\r\n",
                held);
}

pid_t
the_writer_stands(const std::filesystem::path &directory, Cache &c)
{
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) [[unlikely]] return -1;
    const std::string writer = (self.parent_path() / "webmachine-cache").string();
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

}

int
main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: webmachine-serve <port|unix path> [engine]\n");
        return 2;
    }
    const bool on_a_path = strchr(argv[1], '/') != nullptr;
    const uint16_t port = (uint16_t) strtoul(argv[1], nullptr, 10);
    if (argc > 2 && strcmp(argv[2], "engine") == 0) slipstream_syscall_set_engine(1);

    Cache c{nullptr, nullptr, -1, {}};
    const pid_t writer = the_writer_stands(std::filesystem::temp_directory_path(), c);
    if (writer < 0 || c.of_thread == nullptr) fprintf(stderr, "webmachine-serve: no cache, every page is rendered\n");

    wm::Ring ring;
    const int stood = ring.stood_up(4096);
    if (stood < 0) {
        fprintf(stderr, "webmachine-serve: stood_up: %s\n", strerror(-stood));
        return 1;
    }
    const int listening = on_a_path ? ring.listens_on(argv[1]) : ring.listens_on(port);
    if (listening < 0) {
        fprintf(stderr, "webmachine-serve: listens_on: %s\n", strerror(-listening));
        return 1;
    }
    printf("%s\n", argv[1]);

    bool enough = false;
    try {
        ring.serves(
            [&](const std::string_view asked, const std::span<char> room) -> wm::Answered {
                if (asked.starts_with("STOP")) enough = true;
                const size_t bytes = http1::bytes_before_the_body(asked);
                if (bytes == 0) return {0, {}, nullptr, 0};
                const std::expected<http1::Request, http::Refusal> request = http1::parse_request(asked);
                if (!request) [[unlikely]] {
                    wm::Answered refused = made(room, 400, problem::kMediaTypeText,
                                                problem::text_of(problem::details_of(400)).value_or(""), false, "",
                                                nullptr);
                    refused.taken = bytes;
                    return refused;
                }
                wm::Answered answered = answer_to(*request, c, room);
                answered.taken = request->bytes;
                return answered;
            },
            [&](const void *const held) {
                cache_sent(c.of_thread, static_cast<cache_held *>(const_cast<void *>(held)));
            },
            enough);
    } catch (const wm::QueueIsFull &full) {
        fprintf(stderr, "webmachine-serve: %s\n", full.what());
        return 1;
    }
    if (c.of_thread != nullptr) cache_reader_closed(c.of_thread);
    if (c.of_app != nullptr) cache_close(c.of_app);
    if (writer > 0) {
        close(c.to_the_writer);
        waitpid(writer, nullptr, 0);
    }
    return 0;
}
