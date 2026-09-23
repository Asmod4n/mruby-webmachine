#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
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
#include "zip.hpp"

namespace problem
{

struct Details {
    std::string                type;
    std::string                title;
    uint16_t                   status;
    std::optional<std::string> instance;
    std::optional<std::string> fingerprint;
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
    std::optional<std::string> fingerprint;
    std::optional<std::string> picture;
    std::optional<std::string> exception;
    std::optional<std::string> message;
    std::vector<std::string>   backtrace;
    std::optional<std::string> has_backtrace;
};

inline constexpr std::string_view kMediaTypeHtml = "text/html; charset=utf-8";
inline constexpr std::string_view kMediaTypeJson = "application/problem+json";
inline constexpr std::string_view kMediaTypeXml = "application/problem+xml";
inline constexpr std::string_view kMediaTypeText = "text/plain; charset=utf-8";
inline constexpr std::string_view kMediaTypeJpeg = "image/jpeg";
inline constexpr std::string_view kMediaTypePlainJson = "application/json";

enum class Form : uint8_t { kHtml, kProblemJson, kJpeg, kJson, kProblemXml, kText };

struct Offer {
    Form            form;
    http::MediaType media_type;
};

inline constexpr std::array<Offer, 6> kOffers{{
    {Form::kHtml, {"text", "html", ""}},
    {Form::kProblemJson, {"application", "problem+json", ""}},
    {Form::kJpeg, {"image", "jpeg", ""}},
    {Form::kJson, {"application", "json", ""}},
    {Form::kProblemXml, {"application", "problem+xml", ""}},
    {Form::kText, {"text", "plain", ""}},
}};

inline constexpr uint16_t kExtraImgTag = 0x574d;

struct Picture {
    std::string_view           img;
    std::span<const std::byte> jpeg;
};

inline constexpr char kHtml[] =
    "<!doctype html>\n"
    "<html lang=en>\n"
    "<meta charset=utf-8>\n"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">\n"
    "<title>{{status}} {{title}}</title>\n"
    "<main>\n"
    "<p>{{status}}</p>\n"
    "<h1>{{title}}</h1>\n"
    "{{#picture}}\n"
    "{{{picture}}}\n"
    "{{/picture}}\n"
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
    "{{#fingerprint}}\n"
    "<p>Fingerprint {{fingerprint}}</p>\n"
    "{{/fingerprint}}\n"
    "{{#picture}}\n"
    "<p>Cat by <a href=\"https://girliemac.com/blog/2011/12/18/the-day-i-seized-the-interweb-http-status-cats/\">"
    "Tomomi Imura</a>, <a href=\"https://creativecommons.org/licenses/by/2.0/\">CC BY 2.0</a>, unchanged</p>\n"
    "{{/picture}}\n"
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
    "{{#fingerprint}}\n"
    "<fingerprint>{{fingerprint}}</fingerprint>\n"
    "{{/fingerprint}}\n"
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
    "{{/instance}}\n"
    "{{#fingerprint}}\n"
    "Fingerprint {{{fingerprint}}}\n"
    "{{/fingerprint}}\n";

inline Details
details_of(const uint16_t status)
{
    return {.type = "about:blank", .title = std::string(http::reason_phrase(status)), .status = status};
}

inline Fields
fields_of(const Details &d)
{
#if defined(MRB_DEBUG)
    return {d.type,      d.title,   std::to_string(d.status), d.instance, d.fingerprint, std::nullopt,
            d.exception, d.message, d.backtrace,
            d.backtrace.empty() ? std::nullopt : std::optional<std::string>(std::in_place)};
#else
    return {d.type,       d.title,      std::to_string(d.status), d.instance, d.fingerprint, std::nullopt,
            std::nullopt, std::nullopt, {},                       std::nullopt};
#endif
}

inline size_t
page_bound_of(const std::string_view source, const Fields &f)
{
    size_t values = f.type.size() + f.title.size() + f.status.size() + f.instance.value_or("").size() +
                    f.fingerprint.value_or("").size() + f.picture.value_or("").size();
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
html_of(const Details &d, const std::optional<std::string_view> picture)
{
    Fields f = fields_of(d);
    if (picture) f.picture = std::string(*picture);
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
    if (f.fingerprint) m.set("fingerprint", Value{*f.fingerprint});
    if (f.picture) m.set("picture", Value{*f.picture});
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
html_of(const Details &d, const std::optional<std::string_view> picture)
{
    Fields f = fields_of(d);
    if (picture) f.picture = std::string(*picture);
    return page_of(std::string_view(kHtml), f);
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
    if (d.fingerprint) {
        b.append_raw(R"(,"fingerprint":)");
        b.escape_and_append_with_quotes(*d.fingerprint);
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

inline std::string
instance_of(const std::span<const std::byte, 16> random)
{
    constexpr std::string_view kHex = "0123456789abcdef";
    std::array<uint8_t, 16> octets{};
    for (size_t i = 0; i < octets.size(); i++) octets.at(i) = std::to_integer<uint8_t>(random[i]);
    octets.at(6) = static_cast<uint8_t>((octets.at(6) & 0x0f) | 0x40);
    octets.at(8) = static_cast<uint8_t>((octets.at(8) & 0x3f) | 0x80);
    std::string out = "urn:uuid:";
    for (size_t i = 0; i < octets.size(); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex.at(octets.at(i) >> 4));
        out.push_back(kHex.at(octets.at(i) & 0xf));
    }
    return out;
}

inline std::optional<Picture>
picture_of(const std::span<const zip::Entry> pictures, const uint16_t status)
{
    const std::string name = std::to_string(status) + ".jpg";
    for (const zip::Entry &entry : pictures) {
        if (entry.name != name) continue;
        const std::optional<std::span<const std::byte>> img = zip::extra_field_of(entry.extra, kExtraImgTag);
        if (!img) [[unlikely]] return std::nullopt;
        return Picture{std::string_view(reinterpret_cast<const char *>(img->data()), img->size()), entry.data};
    }
    return std::nullopt;
}

inline std::expected<Form, http::Refusal>
form_of(const std::optional<std::string_view> accept, const bool has_picture)
{
    if (!accept) return Form::kHtml;
    std::array<http::MediaType, kOffers.size()> provided{};
    std::array<Form, kOffers.size()> forms{};
    size_t count = 0;
    for (const Offer &offer : kOffers) {
        if (offer.form == Form::kJpeg && !has_picture) continue;
        provided.at(count) = offer.media_type;
        forms.at(count) = offer.form;
        count++;
    }
    const auto chosen = http::choose_media_type(std::span(provided).first(count), *accept);
    if (!chosen) [[unlikely]] return std::unexpected(chosen.error());
    if (!*chosen) return Form::kText;
    return forms.at((*chosen)->at);
}

inline std::string_view
media_type_of(const Form form)
{
    switch (form) {
        case Form::kHtml: return kMediaTypeHtml;
        case Form::kProblemJson: return kMediaTypeJson;
        case Form::kJpeg: return kMediaTypeJpeg;
        case Form::kJson: return kMediaTypePlainJson;
        case Form::kProblemXml: return kMediaTypeXml;
        case Form::kText: return kMediaTypeText;
    }
    return kMediaTypeText;
}

}
