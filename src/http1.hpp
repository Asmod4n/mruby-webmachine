#pragma once

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <expected>
#if defined(__GNUC__) && !defined(__clang__)
#include <experimental/simd>
#endif
#include <functional>
#include <numeric>
#include <optional>
#include <string_view>

#include "http.hpp"

namespace http1
{

inline constexpr uint16_t kFieldLineProblem = 27;
inline constexpr uint16_t kStartLineProblem = 28;
inline constexpr uint16_t kNotWholeYet = 29;
inline constexpr uint16_t kUriTooLong = 30;
inline constexpr uint16_t kRequestHeaderFieldsTooLarge = 31;

inline constexpr size_t kMostRequestLineBytes = 8000;
inline constexpr size_t kMostHeaderSectionBytes = 8192;

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
    std::string_view host;
    uint32_t host_field_lines;
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
    if (!http::is_token(field_name)) [[unlikely]]
        return std::unexpected(http::Refusal{kFieldLineProblem, 0});
    const std::string_view after = line.substr(colon + 1);
    const size_t first = after.find_first_not_of(" \t");
    if (first == std::string_view::npos) return FieldLine{field_name, after.substr(after.size())};
    return FieldLine{field_name, after.substr(first, after.find_last_not_of(" \t") + 1 - first)};
}

constexpr bool is_refused_in_field_lines(const char letter)
{
    const unsigned char byte = static_cast<unsigned char>(letter);
    return (byte < ' ' && byte != '\t' && byte != '\r' && byte != '\n') || byte == 0x7F;
}

inline bool floor_field_lines_are_valid(const std::string_view lines)
{
    const uint8_t refused =
        std::transform_reduce(lines.begin(), lines.end(), uint8_t{0}, std::bit_or<uint8_t>{},
                              [](const char letter) { return static_cast<uint8_t>(is_refused_in_field_lines(letter)); });
    return refused == 0 && std::ranges::count(lines, '\r') == std::ranges::count(lines, '\n');
}

#if defined(__GNUC__) && !defined(__clang__)
using FieldLineBlock = std::experimental::fixed_size_simd<unsigned char, http::kWideBlockBytes>;

inline bool simd_field_lines_are_valid(const std::string_view lines)
{
    const FieldLineBlock lane([](const auto index) { return static_cast<unsigned char>(index); });
    const FieldLineBlock cr(static_cast<unsigned char>('\r'));
    const FieldLineBlock lf(static_cast<unsigned char>('\n'));
    const FieldLineBlock htab(static_cast<unsigned char>('\t'));
    const FieldLineBlock sp(static_cast<unsigned char>(' '));
    const FieldLineBlock del(static_cast<unsigned char>('\x7f'));
    const unsigned char *const from = reinterpret_cast<const unsigned char *>(lines.data());
    size_t crs = 0;
    size_t lfs = 0;
    bool refused = false;
    for (size_t at = 0; at < lines.size(); at += FieldLineBlock::size()) {
        FieldLineBlock bytes;
        bytes.copy_from(std::next(from, static_cast<std::ptrdiff_t>(at)), std::experimental::element_aligned);
        const auto inside =
            lane < FieldLineBlock(static_cast<unsigned char>(std::min(lines.size() - at, FieldLineBlock::size())));
        const auto control = bytes < sp && bytes != htab && bytes != cr && bytes != lf;
        refused = refused || std::experimental::any_of((control || bytes == del) && inside);
        crs += static_cast<size_t>(std::experimental::popcount((bytes == cr) && inside));
        lfs += static_cast<size_t>(std::experimental::popcount((bytes == lf) && inside));
    }
    return !refused && crs == lfs;
}
#endif

inline bool field_lines_are_valid(const std::string_view lines)
{
#if defined(__GNUC__) && !defined(__clang__)
    return simd_field_lines_are_valid(lines);
#else
    return floor_field_lines_are_valid(lines);
#endif
}

[[gnu::cold]] inline uint32_t first_refused_in_field_lines(const std::string_view lines)
{
    for (size_t at = 0; at < lines.size(); at++) {
        const char letter = lines[at];
        if (is_refused_in_field_lines(letter) || (letter == '\r' && (at + 1 == lines.size() || lines[at + 1] != '\n')))
            return static_cast<uint32_t>(at);
    }
    return 0;
}

constexpr size_t bytes_before_the_body(const std::string_view asked)
{
    const size_t ends = asked.find("\r\n\r\n");
    return ends == std::string_view::npos ? 0 : ends + 4;
}


constexpr http::Refusal not_whole_yet(const size_t taken, const size_t most, const uint16_t too_large)
{
    return taken > most ? http::Refusal{too_large, 0} : http::Refusal{kNotWholeYet, 0};
}

template <class Seen>
constexpr std::expected<Request, http::Refusal> parse_request(const std::string_view asked, Seen &&seen)
{
    const size_t first = asked.find('\n');
    if (first == std::string_view::npos) [[unlikely]]
        return std::unexpected(not_whole_yet(asked.size(), kMostRequestLineBytes + 1, kUriTooLong));
    if (first - 1 > kMostRequestLineBytes) [[unlikely]]
        return std::unexpected(http::Refusal{kUriTooLong, 0});
    if (first == 0 || asked[first - 1] != '\r') [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, static_cast<uint32_t>(first)});
    const std::expected<RequestLine, http::Refusal> line = parse_request_line(asked.substr(0, first - 1));
    if (!line) [[unlikely]]
        return std::unexpected(line.error());
    const size_t fields_from = first + 1;
    size_t at = fields_from;
    std::string_view host;
    uint32_t host_field_lines = 0;
    for (;;) {
        const size_t ends = asked.find('\n', at);
        if (ends == std::string_view::npos) [[unlikely]]
            return std::unexpected(
                not_whole_yet(asked.size() - fields_from, kMostHeaderSectionBytes + 1, kRequestHeaderFieldsTooLarge));
        if (ends == at || asked[ends - 1] != '\r') [[unlikely]]
            return std::unexpected(http::Refusal{kFieldLineProblem, static_cast<uint32_t>(ends)});
        if (ends == at + 1) {
            const std::string_view field_lines = asked.substr(fields_from, at - fields_from);
            if (!field_lines_are_valid(field_lines)) [[unlikely]]
                return std::unexpected(http::Refusal{
                    kFieldLineProblem, static_cast<uint32_t>(fields_from) + first_refused_in_field_lines(field_lines)});
            return Request{*line, field_lines, ends + 1, host, host_field_lines};
        }
        if (ends + 1 - fields_from > kMostHeaderSectionBytes) [[unlikely]]
            return std::unexpected(http::Refusal{kRequestHeaderFieldsTooLarge, 0});
        if (asked[at] == ' ' || asked[at] == '\t') [[unlikely]]
            return std::unexpected(http::Refusal{kFieldLineProblem, static_cast<uint32_t>(at)});
        const std::expected<FieldLine, http::Refusal> field = parse_field_line(asked.substr(at, ends - 1 - at));
        if (!field) [[unlikely]]
            return std::unexpected(http::moved_forward(field.error(), at));
        if (field->field_name.size() == 4 && http::equal_ignoring_case(field->field_name, "Host")) {
            host = field->field_value;
            host_field_lines++;
        }
        seen(*field);
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

constexpr std::expected<std::optional<http::Host>, http::Refusal> parse_host_field(const Request &request)
{
    if (request.host_field_lines > 1) [[unlikely]]
        return std::unexpected(http::Refusal{http::kHostProblem, 0});
    if (request.host_field_lines == 0) {
        if (request.request_line.http_version != "HTTP/1.0") [[unlikely]]
            return std::unexpected(http::Refusal{http::kHostProblem, 0});
        return std::nullopt;
    }
    const std::expected<http::Host, http::Refusal> host = http::parse_host(request.host);
    if (!host) [[unlikely]]
        return std::unexpected(host.error());
    return *host;
}

} // namespace http1
