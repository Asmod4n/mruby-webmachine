#pragma once

#include <cstddef>
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

constexpr std::expected<RequestLine, http::Refusal>
parse_request_line(const std::string_view line)
{
    const size_t after_method = line.find(' ');
    if (after_method == std::string_view::npos) [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, 0});
    const size_t after_target = line.find(' ', after_method + 1);
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



constexpr std::expected<FieldLine, http::Refusal> parse_field_line(const std::string_view line)
{
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) [[unlikely]]
        return std::unexpected(http::Refusal{kFieldLineProblem, 0});
    const std::string_view field_name = line.substr(0, colon);
    const std::string_view after = line.substr(colon + 1);
    const size_t first = after.find_first_not_of(" \t");
    if (first == std::string_view::npos) return FieldLine{field_name, after.substr(after.size())};
    return FieldLine{field_name, after.substr(first, after.find_last_not_of(" \t") + 1 - first)};
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
constexpr std::expected<Request, http::Refusal> parse_request(const std::string_view asked, Seen &&seen)
{
    const size_t first = asked.find('\n');
    if (first == std::string_view::npos) [[unlikely]]
        return std::unexpected(not_whole_yet(asked));
    if (first == 0 || asked[first - 1] != '\r') [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, static_cast<uint32_t>(first)});
    const std::expected<RequestLine, http::Refusal> line = parse_request_line(asked.substr(0, first - 1));
    if (!line) [[unlikely]]
        return std::unexpected(line.error());
    const size_t fields_from = first + 1;
    size_t at = fields_from;
    for (;;) {
        if (at > kMostRequestBytes) [[unlikely]]
            return std::unexpected(http::Refusal{kStartLineProblem, 0});
        const size_t ends = asked.find('\n', at);
        if (ends == std::string_view::npos) [[unlikely]]
            return std::unexpected(not_whole_yet(asked));
        if (ends == at || asked[ends - 1] != '\r') [[unlikely]]
            return std::unexpected(http::Refusal{kFieldLineProblem, static_cast<uint32_t>(ends)});
        if (ends == at + 1)
            return Request{*line, asked.substr(fields_from, at - fields_from), ends + 1};
        const std::expected<FieldLine, http::Refusal> field = parse_field_line(asked.substr(at, ends - 1 - at));
        if (field) seen(*field);
        at = ends + 1;
    }
}

constexpr std::expected<Request, http::Refusal> parse_request(const std::string_view asked)
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
