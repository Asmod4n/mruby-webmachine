#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#if defined(__cpp_impl_reflection)
#include <mustache/reflect.hpp>
#else
#include <mustache/render.hpp>
#include <mustache/std.hpp>
#endif

#include "http.hpp"

namespace problem
{

struct Details {
    std::string                type;
    std::string                title;
    uint16_t                   status;
    std::optional<std::string> instance;
#if defined(MRB_DEBUG)
    std::optional<std::string> exception;
    std::optional<std::string> message;
    std::vector<std::string>   backtrace;
#endif
};

struct Fields {
    std::string                type;
    std::string                title;
    std::string                status;
    std::optional<std::string> instance;
    std::optional<std::string> exception;
    std::optional<std::string> message;
    std::vector<std::string>   backtrace;
    std::optional<std::string> has_backtrace;
};

inline constexpr std::string_view kMediaTypeHtml = "text/html; charset=utf-8";
inline constexpr std::string_view kMediaTypeJson = "application/problem+json";
inline constexpr std::string_view kMediaTypeXml = "application/problem+xml";
inline constexpr std::string_view kMediaTypeText = "text/plain; charset=utf-8";

inline constexpr char kHtml[] =
    "<!doctype html>\n"
    "<html lang=en>\n"
    "<meta charset=utf-8>\n"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">\n"
    "<title>{{status}} {{title}}</title>\n"
    "<main>\n"
    "<p>{{status}}</p>\n"
    "<h1>{{title}}</h1>\n"
    "{{#exception}}\n"
    "<p>{{exception}}</p>\n"
    "{{/exception}}\n"
    "{{#message}}\n"
    "<pre>{{message}}</pre>\n"
    "{{/message}}\n"
    "{{#backtrace}}\n"
    "<pre>{{.}}</pre>\n"
    "{{/backtrace}}\n"
    "{{#instance}}\n"
    "<p>Reference {{instance}}</p>\n"
    "{{/instance}}\n"
    "</main>\n";

inline constexpr char kXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<problem xmlns=\"urn:ietf:rfc:7807\">\n"
    "<type>{{type}}</type>\n"
    "<title>{{title}}</title>\n"
    "<status>{{status}}</status>\n"
    "{{#instance}}\n"
    "<instance>{{instance}}</instance>\n"
    "{{/instance}}\n"
    "{{#exception}}\n"
    "<exception>{{exception}}</exception>\n"
    "{{/exception}}\n"
    "{{#message}}\n"
    "<message>{{message}}</message>\n"
    "{{/message}}\n"
    "{{#has_backtrace}}\n"
    "<backtrace>\n"
    "{{#backtrace}}\n"
    "<i>{{.}}</i>\n"
    "{{/backtrace}}\n"
    "</backtrace>\n"
    "{{/has_backtrace}}\n"
    "</problem>\n";

inline constexpr char kText[] =
    "{{status}} {{{title}}}\n"
    "{{#exception}}\n"
    "{{{exception}}}\n"
    "{{/exception}}\n"
    "{{#message}}\n"
    "{{{message}}}\n"
    "{{/message}}\n"
    "{{#backtrace}}\n"
    "{{{.}}}\n"
    "{{/backtrace}}\n"
    "{{#instance}}\n"
    "Reference {{{instance}}}\n"
    "{{/instance}}\n";

inline Details
details_of(const uint16_t status)
{
    return {.type = "about:blank", .title = std::string(http::reason_phrase(status)), .status = status};
}

inline Fields
fields_of(const Details &d)
{
#if defined(MRB_DEBUG)
    return {d.type,      d.title,   std::to_string(d.status), d.instance,
            d.exception, d.message, d.backtrace,
            d.backtrace.empty() ? std::nullopt : std::optional<std::string>(std::in_place)};
#else
    return {d.type, d.title, std::to_string(d.status), d.instance, std::nullopt, std::nullopt, {}, std::nullopt};
#endif
}

inline size_t
page_bound_of(const std::string_view source, const Fields &f)
{
    size_t values = f.type.size() + f.title.size() + f.status.size() + f.instance.value_or("").size();
    values += f.exception.value_or("").size() + f.message.value_or("").size();
    for (const std::string &line : f.backtrace) values += line.size();
    const size_t lines = f.backtrace.size();
    return source.size() * (lines + 1) + mustache::kEntityMax * values + mustache::kEscapeSlack;
}

#if defined(__cpp_impl_reflection)

template <mustache::fixed_string Source, class T>
std::optional<std::string>
page_of(const T &fields, const size_t bound)
{
    std::string page;
    page.resize_and_overwrite(bound, [&](char *const p, const size_t n) {
        mustache::Out out{p, p + n - mustache::kEscapeSlack};
        mustache::render<Source>(out, fields);
        return out.full ? size_t{0} : static_cast<size_t>(out.w - p);
    });
    if (page.empty()) [[unlikely]] return std::nullopt;
    return page;
}

inline std::optional<std::string>
html_of(const Details &d)
{
    const Fields f = fields_of(d);
    return page_of<mustache::fixed_string{kHtml}>(f, page_bound_of(std::string_view(kHtml), f));
}

inline std::optional<std::string>
xml_of(const Details &d)
{
    const Fields f = fields_of(d);
    return page_of<mustache::fixed_string{kXml}>(f, page_bound_of(std::string_view(kXml), f));
}

inline std::optional<std::string>
text_of(const Details &d)
{
    const Fields f = fields_of(d);
    return page_of<mustache::fixed_string{kText}>(f, page_bound_of(std::string_view(kText), f));
}

#else

inline mustache::std_host::Value
value_of(const Fields &f)
{
    using mustache::std_host::List;
    using mustache::std_host::Map;
    using mustache::std_host::Value;
    Map m;
    m.set("type", Value{f.type});
    m.set("title", Value{f.title});
    m.set("status", Value{f.status});
    if (f.instance) m.set("instance", Value{*f.instance});
    if (f.exception) m.set("exception", Value{*f.exception});
    if (f.message) m.set("message", Value{*f.message});
    List lines;
    for (const std::string &line : f.backtrace) lines.push_back(Value{line});
    m.set("backtrace", Value{std::move(lines)});
    if (f.has_backtrace) m.set("has_backtrace", Value{*f.has_backtrace});
    return Value{std::move(m)};
}

inline std::optional<std::string>
page_of(const std::string_view source, const Fields &f)
{
    using mustache::std_host::Host;
    using mustache::std_host::Key;
    using mustache::std_host::Value;
    Host host;
    const std::optional<mustache::Program<Key>> program = mustache::program_of<Key>(host, source, source.size());
    if (!program) [[unlikely]] return std::nullopt;
    const Value root = value_of(f);
    std::string page;
    page.resize_and_overwrite(page_bound_of(source, f), [&](char *const p, const size_t n) {
        mustache::Out out{p, p + n - mustache::kEscapeSlack};
        mustache::Walk<Host, const Value *, Key, mustache::Out> walk(host, out);
        const mustache::Fault fault = walk.run(*program, &root);
        return fault == mustache::Fault::none ? static_cast<size_t>(out.w - p) : size_t{0};
    });
    if (page.empty()) [[unlikely]] return std::nullopt;
    return page;
}

inline std::optional<std::string>
html_of(const Details &d)
{
    return page_of(std::string_view(kHtml), fields_of(d));
}

inline std::optional<std::string>
xml_of(const Details &d)
{
    return page_of(std::string_view(kXml), fields_of(d));
}

inline std::optional<std::string>
text_of(const Details &d)
{
    return page_of(std::string_view(kText), fields_of(d));
}

#endif

inline std::optional<std::string>
json_of(const Details &d)
{
    simdjson::builder::string_builder b;
    b.append_raw(R"({"type":)");
    b.escape_and_append_with_quotes(d.type);
    b.append_raw(R"(,"title":)");
    b.escape_and_append_with_quotes(d.title);
    b.append_raw(R"(,"status":)");
    b.append(d.status);
    if (d.instance) {
        b.append_raw(R"(,"instance":)");
        b.escape_and_append_with_quotes(*d.instance);
    }
#if defined(MRB_DEBUG)
    if (d.exception) {
        b.append_raw(R"(,"exception":)");
        b.escape_and_append_with_quotes(*d.exception);
    }
    if (d.message) {
        b.append_raw(R"(,"message":)");
        b.escape_and_append_with_quotes(*d.message);
    }
    if (!d.backtrace.empty()) {
        b.append_raw(R"(,"backtrace":[)");
        for (size_t i = 0; i < d.backtrace.size(); i++) {
            if (i != 0) b.append(',');
            b.escape_and_append_with_quotes(d.backtrace.at(i));
        }
        b.append(']');
    }
#endif
    b.append('}');
    std::string_view json;
    if (b.view().get(json)) [[unlikely]] return std::nullopt;
    return std::string(json);
}

}
