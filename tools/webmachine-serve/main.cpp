#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>

#include <slipstream_syscall.h>

#include "../../src/home.hpp"
#include "../../src/http1.hpp"
#include "../../src/problem.hpp"
#include "../../src/ring.hpp"
#include "../../src/walk.hpp"
#include "../../src/webmachine.hpp"

namespace
{

using home::Home;
using home::kHomeTypes;
using home::page_of;
using home::reason_of;

std::string
response_of(const uint16_t status, const std::string_view content_type, const std::string_view body,
            const bool head, const std::string_view extra_fields)
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto date = http::spell_imf_fixdate(now);
    std::string answer;
    answer.reserve(256 + body.size());
    answer += "HTTP/1.1 ";
    answer += std::to_string(status);
    answer += ' ';
    answer += reason_of(status);
    answer += "\r\nDate: ";
    answer.append(date.data(), date.size());
    if (!content_type.empty()) {
        answer += "\r\nContent-Type: ";
        answer += content_type;
    }
    answer += "\r\nContent-Length: ";
    answer += std::to_string(body.size());
    answer += "\r\n";
    answer += extra_fields;
    answer += "\r\n";
    if (!head) answer += body;
    return answer;
}

std::string
answer_to(const http1::Request &request)
{
    const auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year year = std::chrono::year_month_day{now}.year();
    const bool head = request.request_line.method == http::Method::kHead;
    const Home home;
    const flow::Facts facts = flow::facts_of(request, year);
    const auto outcome = flow::walk(home, request, facts);
    if (!outcome) [[unlikely]] {
        const auto page = problem::text_of(problem::details_of(400));
        return response_of(400, problem::kMediaTypeText, page.value_or(""), head, "");
    }
    if (outcome->status == 200 && outcome->media_type) {
        const std::string page = page_of(request, *outcome);
        return response_of(200, kHomeTypes[*outcome->media_type].media_type, page, head, "Vary: Accept\r\n");
    }
    if (outcome->status < 400) return response_of(outcome->status, "", "", head, "");
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
    return response_of(outcome->status, problem::media_type_of(chosen), page.value_or(""), head, "Vary: Accept\r\n");
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
    std::string answered;
    try {
        ring.serves(
            [&](const std::string_view asked) -> wm::Answered {
                if (asked.starts_with("STOP")) enough = true;
                const size_t bytes = http1::bytes_before_the_body(asked);
                if (bytes == 0) return {{}, 0};
                const std::expected<http1::Request, http::Refusal> request = http1::parse_request(asked);
                if (!request) [[unlikely]] {
                    answered = response_of(400, problem::kMediaTypeText,
                                           problem::text_of(problem::details_of(400)).value_or(""), false, "");
                    return {answered, bytes};
                }
                answered = answer_to(*request);
                return {answered, request->bytes};
            },
            enough);
    } catch (const wm::QueueIsFull &full) {
        fprintf(stderr, "webmachine-serve: %s\n", full.what());
        return 1;
    }
    return 0;
}
