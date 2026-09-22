#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "http.hpp"

namespace wm
{

inline constexpr size_t kMostHeadBytes = 8192;
inline constexpr size_t kMostFields = 64;

struct Field {
    std::string_view name;
    std::string_view value;
};

struct Head {
    http::Method method = http::Method::kUnknown;
    std::string_view target;
    std::string_view version;
    Field field[kMostFields];
    size_t fields = 0;
    size_t bytes = 0;
};

enum class Reading : uint8_t {
    kWantsMore,
    kRead,
    kRefused,
};

constexpr size_t end_of_head(const std::string_view seen, const size_t from)
{
    const size_t at = seen.find("\r\n\r\n", from);
    return at == std::string_view::npos ? 0 : at + 4;
}

constexpr std::string_view line_at(const std::string_view head, size_t &at)
{
    const size_t ends = head.find("\r\n", at);
    if (ends == std::string_view::npos)
        return {};
    const std::string_view line = head.substr(at, ends - at);
    at = ends + 2;
    return line;
}

constexpr bool split_at_colon(const std::string_view line, Field &into)
{
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return false;
    into.name = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    into.value = value;
    return true;
}

inline Reading head_of(const std::string_view seen, Head &into)
{
    if (seen.size() > kMostHeadBytes)
        return Reading::kRefused;
    const size_t ends = end_of_head(seen, 0);
    if (ends == 0)
        return Reading::kWantsMore;
    into.bytes = ends;
    const std::string_view head = seen.substr(0, ends - 2);

    size_t at = 0;
    const std::string_view first = line_at(head, at);
    const size_t one = first.find(' ');
    if (one == std::string_view::npos)
        return Reading::kRefused;
    const size_t two = first.find(' ', one + 1);
    if (two == std::string_view::npos)
        return Reading::kRefused;
    into.method = http::method_of(first.substr(0, one));
    into.target = first.substr(one + 1, two - one - 1);
    into.version = first.substr(two + 1);
    if (into.target.empty() || into.version.size() != 8)
        return Reading::kRefused;

    into.fields = 0;
    for (;;) {
        const std::string_view line = line_at(head, at);
        if (line.empty())
            break;
        if (into.fields == kMostFields)
            return Reading::kRefused;
        if (!split_at_colon(line, into.field[into.fields]))
            return Reading::kRefused;
        into.fields++;
    }
    return Reading::kRead;
}

constexpr std::string_view value_of(const Head &head, const std::string_view name)
{
    for (size_t at = 0; at < head.fields; at++)
        if (http::equal_ignoring_case(head.field[at].name, name))
            return head.field[at].value;
    return {};
}

} // namespace wm
