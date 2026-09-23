#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>

#include <mustache/render.hpp>
#include <mustache/std.hpp>
#include <slipstream_syscall.h>

#include "../../src/http1.hpp"
#include "../../src/problem.hpp"
#include "../../src/ring.hpp"
#include "../../src/walk.hpp"
#include "../../src/webmachine.hpp"

namespace
{

constexpr webmachine::MediaTypeHandler kHomeTypes[] = {{"text/html; charset=utf-8", "to_html"}};

class Home : public webmachine::Resource
{
  public:
    bool resource_exists(const http1::Request &request) const override
    {
        const std::string_view target = request.request_line.request_target;
        return target == "/" || target.starts_with("/?");
    }
    std::span<const webmachine::MediaTypeHandler> content_types_provided() const override
    {
        return kHomeTypes;
    }
    std::string_view generate_etag(const http1::Request &) const override
    {
        return {};
    }
};

constexpr std::string_view kPage = R"(<!doctype html>
<html lang=en>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>mruby-webmachine</title>
<style>
:root{color-scheme:light dark;--bg:#f6f5f2;--fg:#17181c;--dim:#6b6d76;--line:#e1dfd9;--card:#fff;--yes:#1f7a4d;--no:#b3401f;--accent:#3d5afe}
@media (prefers-color-scheme:dark){:root{--bg:#101116;--fg:#e9e8e4;--dim:#8d8f99;--line:#262833;--card:#171922;--yes:#4fd18b;--no:#ff8a65;--accent:#8c9eff}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.55 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:52rem;margin:0 auto;padding:4rem 1.25rem 5rem}
.kicker{font:600 .8rem/1 ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:.14em;text-transform:uppercase;color:var(--accent)}
h1{font-size:clamp(2.2rem,6vw,3.6rem);line-height:1.05;letter-spacing:-.03em;margin:.6rem 0 1rem}
.lead{font-size:1.15rem;color:var(--dim);max-width:38rem;margin:0 0 2.5rem}
.status{display:inline-flex;gap:.6rem;align-items:baseline;background:var(--card);border:1px solid var(--line);border-radius:999px;padding:.45rem 1rem;margin-bottom:2rem;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.status b{font-size:1.2rem;color:var(--yes)}
ol{list-style:none;margin:0;padding:0;border-left:2px solid var(--line)}
li{position:relative;padding:.55rem 0 .55rem 1.4rem}
li::before{content:"";position:absolute;left:-7px;top:1.05rem;width:12px;height:12px;border-radius:50%;background:var(--card);border:2px solid var(--accent)}
.node{font:700 .95rem/1 ui-monospace,SFMono-Regular,Menlo,monospace;display:inline-block;min-width:3.4rem}
.cb{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.92rem}
.yes{color:var(--yes)}.no{color:var(--no)}
.clause{display:block;color:var(--dim);font-size:.86rem;margin-top:.1rem}
footer{margin-top:3rem;color:var(--dim);font-size:.85rem;border-top:1px solid var(--line);padding-top:1.2rem}
</style>
<main>
<div class=kicker>mruby-webmachine</div>
<h1>Every answer walks the graph.</h1>
<p class=lead>This page is the representation of <code>/</code>. Before a byte of it was written, your request went through the webmachine decision graph, node by node, each question answered by the resource or by your request. This is that path.</p>
<div class=status><span>{{method}} {{target}}</span><b>{{status}} {{reason}}</b></div>
<ol>
{{#steps}}
<li><span class=node>{{node}}</span> <span class=cb>{{question}}</span> <span class={{answer}}>{{answer}}</span><span class=clause>{{clause}}</span></li>
{{/steps}}
</ol>
<footer>{{count}} nodes, rendered by mruby-mustache, served from one io_uring ring.</footer>
</main>
)";

std::string_view
reason_of(const uint16_t status)
{
    const std::string_view reason = http::reason_phrase(status);
    return reason.empty() ? std::string_view("Unknown") : reason;
}

std::string
page_of(const http1::Request &request, const flow::Outcome &outcome)
{
    using mustache::std_host::Host;
    using mustache::std_host::Key;
    using mustache::std_host::List;
    using mustache::std_host::Map;
    using mustache::std_host::Value;
    List steps;
#if defined(MRB_DEBUG)
    for (size_t i = 0; i < outcome.path_length; i++) {
        const flow::Step step = outcome.path.at(i);
        const flow::FlowNode &node = flow::node_of(step.node);
        Map m;
        m.set("node", Value{std::string(flow::name_of(step.node))});
        m.set("question", Value{std::string(node.callback.empty() ? std::string_view("request") : node.callback)});
        m.set("answer", Value{std::string(step.answer ? "yes" : "no")});
        m.set("clause", Value{std::string(node.clause)});
        steps.push_back(Value{std::move(m)});
    }
    const size_t walked = outcome.path_length;
#else
    const size_t walked = 0;
#endif
    Map root;
    root.set("method", Value{std::string(http::method_name_of(request.request_line.method))});
    root.set("target", Value{std::string(request.request_line.request_target)});
    root.set("status", Value{std::to_string(outcome.status)});
    root.set("reason", Value{std::string(reason_of(outcome.status))});
    root.set("count", Value{std::to_string(walked)});
    root.set("steps", Value{std::move(steps)});
    const Value data{std::move(root)};
    Host host;
    const auto program = mustache::program_of<Key>(host, kPage, kPage.size());
    if (!program) [[unlikely]] return {};
    std::string page;
    page.resize_and_overwrite(1 << 16, [&](char *const p, const size_t n) {
        mustache::Out out{p, p + n - mustache::kEscapeSlack};
        mustache::Walk<Host, const Value *, Key, mustache::Out> walk(host, out);
        const mustache::Fault fault = walk.run(*program, &data);
        return fault == mustache::Fault::none ? static_cast<size_t>(out.w - p) : size_t{0};
    });
    return page;
}

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
