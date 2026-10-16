#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mustache/mustache.hpp>

#include "http.hpp"
#include "http1.hpp"
#include "walk.hpp"
#include "webmachine.hpp"

namespace home
{

constexpr webmachine::MediaTypeHandler kHomeTypes[] = {{"text/html; charset=utf-8", "to_html"}};

class Home final : public webmachine::Resource
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

inline constexpr char kPageSource[] = R"(<!doctype html>
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
<footer>{{count}} nodes, rendered by mbedded-mustache, served from one io_uring ring.</footer>
</main>
)";

inline constexpr std::string_view kPage{kPageSource, sizeof kPageSource - 1};

inline std::string_view
reason_of(const uint16_t status)
{
    const std::string_view reason = http::reason_phrase(status);
    return reason.empty() ? std::string_view("Unknown") : reason;
}

struct StepFields {
    std::string_view node;
    std::string_view question;
    std::string_view answer;
    std::string_view clause;
};

struct PageFields {
    std::string_view        method;
    std::string_view        target;
    std::string             status;
    std::string_view        reason;
    std::string             count;
    std::vector<StepFields> steps;
};

inline PageFields
fields_of(const http1::Request &request, const flow::Outcome &outcome)
{
    PageFields f{http::method_name_of(request.request_line.method), request.request_line.request_target,
                 std::to_string(outcome.status), reason_of(outcome.status), {}, {}};
#if defined(MRB_DEBUG)
    f.steps.reserve(outcome.path_length);
    for (size_t i = 0; i < outcome.path_length; i++) {
        const flow::Step step = outcome.path.at(i);
        const flow::FlowNode &node = flow::node_of(step.node);
        f.steps.push_back({flow::name_of(step.node), node.callback.empty() ? std::string_view("request") : node.callback,
                           step.answer ? "yes" : "no", node.clause});
    }
#endif
    f.count = std::to_string(f.steps.size());
    return f;
}

#if defined(__cpp_impl_reflection)

inline std::string
page_of(const http1::Request &request, const flow::Outcome &outcome)
{
    auto page = mustache::render<mustache::fixed_string{kPageSource}>(fields_of(request, outcome));
    if (!page) [[unlikely]] return {};
    return std::move(*page);
}

#else

inline std::string
page_of(const http1::Request &request, const flow::Outcome &outcome)
{
    const auto program = mustache::compile<PageFields>(kPage);
    if (!program) [[unlikely]] return {};
    auto page = mustache::render(*program, fields_of(request, outcome));
    if (!page) [[unlikely]] return {};
    return std::move(*page);
}

#endif

}
