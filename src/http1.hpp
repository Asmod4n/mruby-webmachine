#pragma once

#include <cstddef>
#include <expected>
#include <string_view>

#include "http.hpp"

namespace http1
{

inline constexpr uint16_t kFieldLineProblem = 27;
inline constexpr uint16_t kStartLineProblem = 28;

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
    std::string_view field_value = line.substr(colon + 1);
    while (!field_value.empty() && (field_value.front() == ' ' || field_value.front() == '\t'))
        field_value.remove_prefix(1);
    while (!field_value.empty() && (field_value.back() == ' ' || field_value.back() == '\t'))
        field_value.remove_suffix(1);
    return FieldLine{field_name, field_value};
}

constexpr size_t bytes_before_the_body(const std::string_view asked)
{
    const size_t ends = asked.find("\r\n\r\n");
    return ends == std::string_view::npos ? 0 : ends + 4;
}

constexpr std::expected<Request, http::Refusal> parse_request(const std::string_view asked)
{
    const size_t bytes = bytes_before_the_body(asked);
    if (bytes == 0 || bytes > kMostRequestBytes) [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, 0});
    const size_t after_line = asked.find("\r\n");
    const std::expected<RequestLine, http::Refusal> line =
        parse_request_line(asked.substr(0, after_line));
    if (!line) [[unlikely]]
        return std::unexpected(line.error());
    return Request{*line, asked.substr(after_line + 2, bytes - 2 - (after_line + 2)), bytes};
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
