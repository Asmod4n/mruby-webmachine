#pragma once

#include <cstddef>
#include <experimental/simd>
#include <expected>
#include <string_view>

#include "http.hpp"

namespace http1
{

inline constexpr uint16_t kFieldLineProblem = 27;
inline constexpr uint16_t kStartLineProblem = 28;
inline constexpr uint16_t kNotWholeYet = 29;

inline constexpr size_t kMostRequestBytes = 8192;

struct RequestLine {
    http::Method method;
    std::string_view request_target;
    std::string_view http_version;
};

struct FieldLine {
    std::string_view field_name;
    std::string_view field_value;
};

struct Request {
    RequestLine request_line;
    std::string_view field_lines;
    size_t bytes;
};

inline size_t byte_at(const std::string_view text, const size_t from, const char wanted)
{
    namespace stdx = std::experimental;
    using Bytes = stdx::native_simd<char>;
    size_t at = from;
    for (; at + Bytes::size() <= text.size(); at += Bytes::size()) {
        const Bytes block(text.data() + at, stdx::element_aligned);
        const auto found = block == wanted;
        if (stdx::any_of(found)) return at + static_cast<size_t>(stdx::find_first_set(found));
    }
    if (at >= text.size()) return std::string_view::npos;
    if (text.size() >= Bytes::size()) {
        const size_t base = text.size() - Bytes::size();
        const Bytes block(text.data() + base, stdx::element_aligned);
        const Bytes lane([](const auto i) { return static_cast<char>(i); });
        const auto found = block == wanted && lane >= static_cast<char>(at - base);
        return stdx::any_of(found) ? base + static_cast<size_t>(stdx::find_first_set(found)) : std::string_view::npos;
    }
    for (; at < text.size(); at++)
        if (text[at] == wanted) return at;
    return std::string_view::npos;
}

inline size_t line_feed_at(const std::string_view text, const size_t from)
{
    return byte_at(text, from, '\n');
}

inline size_t byte_before(const std::string_view within, const size_t from, const size_t ends, const char wanted)
{
    const size_t at = byte_at(within, from, wanted);
    return at < ends ? at : std::string_view::npos;
}

inline std::expected<RequestLine, http::Refusal>
parse_request_line(const std::string_view within, const size_t ends)
{
    const std::string_view line = within.substr(0, ends);
    const size_t after_method = byte_before(within, 0, ends, ' ');
    if (after_method == std::string_view::npos) [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, 0});
    const size_t after_target = byte_before(within, after_method + 1, ends, ' ');
    if (after_target == std::string_view::npos) [[unlikely]]
        return std::unexpected(
            http::Refusal{kStartLineProblem, static_cast<uint32_t>(after_method)});
    const std::string_view request_target =
        line.substr(after_method + 1, after_target - after_method - 1);
    const std::string_view http_version = line.substr(after_target + 1);
    if (request_target.empty()) [[unlikely]]
        return std::unexpected(
            http::Refusal{http::kRequestTargetProblem, static_cast<uint32_t>(after_method + 1)});
    if (http_version.size() != 8 || !http_version.starts_with("HTTP/")) [[unlikely]]
        return std::unexpected(
            http::Refusal{kStartLineProblem, static_cast<uint32_t>(after_target + 1)});
    return RequestLine{http::method_of(line.substr(0, after_method)), request_target,
                       http_version};
}

inline std::expected<RequestLine, http::Refusal>
parse_request_line(const std::string_view line)
{
    return parse_request_line(line, line.size());
}

inline std::expected<FieldLine, http::Refusal> parse_field_line(const std::string_view within, const size_t from,
                                                                const size_t ends)
{
    const std::string_view line = within.substr(from, ends - from);
    const size_t found = byte_before(within, from, ends, ':');
    if (found == std::string_view::npos || found == from) [[unlikely]]
        return std::unexpected(http::Refusal{kFieldLineProblem, 0});
    const size_t colon = found - from;
    const std::string_view field_name = line.substr(0, colon);
    std::string_view field_value = line.substr(colon + 1);
    while (!field_value.empty() && (field_value.front() == ' ' || field_value.front() == '\t'))
        field_value.remove_prefix(1);
    while (!field_value.empty() && (field_value.back() == ' ' || field_value.back() == '\t'))
        field_value.remove_suffix(1);
    return FieldLine{field_name, field_value};
}

inline std::expected<FieldLine, http::Refusal> parse_field_line(const std::string_view line)
{
    return parse_field_line(line, 0, line.size());
}

constexpr size_t bytes_before_the_body(const std::string_view asked)
{
    const size_t ends = asked.find("\r\n\r\n");
    return ends == std::string_view::npos ? 0 : ends + 4;
}


constexpr http::Refusal not_whole_yet(const std::string_view asked)
{
    return asked.size() > kMostRequestBytes ? http::Refusal{kStartLineProblem, 0} : http::Refusal{kNotWholeYet, 0};
}

template <class Seen>
std::expected<Request, http::Refusal> parse_request(const std::string_view asked, Seen &&seen)
{
    const size_t first = line_feed_at(asked, 0);
    if (first == std::string_view::npos) [[unlikely]]
        return std::unexpected(not_whole_yet(asked));
    if (first == 0 || asked[first - 1] != '\r') [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, static_cast<uint32_t>(first)});
    const std::expected<RequestLine, http::Refusal> line = parse_request_line(asked, first - 1);
    if (!line) [[unlikely]]
        return std::unexpected(line.error());
    const size_t fields_from = first + 1;
    size_t at = fields_from;
    for (;;) {
        if (at > kMostRequestBytes) [[unlikely]]
            return std::unexpected(http::Refusal{kStartLineProblem, 0});
        const size_t ends = line_feed_at(asked, at);
        if (ends == std::string_view::npos) [[unlikely]]
            return std::unexpected(not_whole_yet(asked));
        if (ends == at || asked[ends - 1] != '\r') [[unlikely]]
            return std::unexpected(http::Refusal{kFieldLineProblem, static_cast<uint32_t>(ends)});
        if (ends == at + 1)
            return Request{*line, asked.substr(fields_from, at - fields_from), ends + 1};
        const std::expected<FieldLine, http::Refusal> field = parse_field_line(asked, at, ends - 1);
        if (field) seen(*field);
        at = ends + 1;
    }
}

inline std::expected<Request, http::Refusal> parse_request(const std::string_view asked)
{
    return parse_request(asked, [](const FieldLine &) {});
}

constexpr std::string_view field_value_of(const Request &request, const std::string_view name)
{
    std::string_view left = request.field_lines;
    while (!left.empty()) {
        const size_t ends = left.find("\r\n");
        if (ends == std::string_view::npos)
            return {};
        const std::expected<FieldLine, http::Refusal> field =
            parse_field_line(left.substr(0, ends));
        if (field && http::equal_ignoring_case(field->field_name, name))
            return field->field_value;
        left.remove_prefix(ends + 2);
    }
    return {};
}

} // namespace http1
