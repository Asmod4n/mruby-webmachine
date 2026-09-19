#ifndef WEBMACHINE_HTTP_HPP
#define WEBMACHINE_HTTP_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace http
{

struct Problem {
    const char *section;
    const char *rule;
    const char *title;
    const char *allowed;
    unsigned status;
};

inline constexpr std::array kProblems = std::to_array<Problem>({
    {"", "", "", "", 0},
    {"RFC 9110 5.6.2", "tchar", "The field name is not valid",
     "!#$%&'*+-.^_`|~ / DIGIT / ALPHA", 400},
    {"RFC 9110 5.6.4", "quoted-string", "The field value is not valid",
     "DQUOTE *( qdtext / quoted-pair ) DQUOTE", 400},
    {"RFC 9110 5.6.4", "qdtext", "The field value is not valid",
     "HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text", 400},
    {"RFC 9110 5.6.6", "parameter", "The field value is not valid",
     "parameter-name \"=\" parameter-value", 400},
});

inline constexpr uint16_t kUnknownProblem = 0;
inline constexpr uint16_t kTcharProblem = 1;
inline constexpr uint16_t kQuotedStringProblem = 2;
inline constexpr uint16_t kQdtextProblem = 3;
inline constexpr uint16_t kParameterProblem = 4;

class ParseError : public std::runtime_error
{
public:
    ParseError(const uint16_t problem, const std::string_view text, const size_t offset)
        : std::runtime_error(kProblems.at(problem).title), problem_(problem),
          offset_(static_cast<uint32_t>(offset)),
          found_byte_(offset < text.size() ? static_cast<unsigned char>(text[offset]) : 0)
    {
        const size_t from = offset < 16 ? 0 : offset - 16;
        for (const char letter : text.substr(from, excerpt_.size()))
            excerpt_[excerpt_length_++] = letter;
    }

    uint16_t problem() const noexcept { return problem_; }
    std::string_view section() const noexcept { return kProblems.at(problem_).section; }
    std::string_view rule() const noexcept { return kProblems.at(problem_).rule; }
    std::string_view title() const noexcept { return kProblems.at(problem_).title; }
    std::string_view allowed() const noexcept { return kProblems.at(problem_).allowed; }
    unsigned status() const noexcept { return kProblems.at(problem_).status; }
    size_t offset() const noexcept { return offset_; }
    unsigned char found_byte() const noexcept { return found_byte_; }
    std::string_view excerpt() const noexcept { return {excerpt_.data(), excerpt_length_}; }

private:
    uint16_t problem_;
    uint32_t offset_;
    std::array<char, 32> excerpt_{};
    uint8_t excerpt_length_ = 0;
    unsigned char found_byte_;
};

inline constexpr std::array<bool, 256> kTchar = [] {
    std::array<bool, 256> table{};
    for (const char letter : std::string_view("!#$%&'*+-.^_`|~"))
        table.at(static_cast<unsigned char>(letter)) = true;
    for (unsigned index = '0'; index <= '9'; index++)
        table.at(index) = true;
    for (unsigned index = 'A'; index <= 'Z'; index++)
        table.at(index) = true;
    for (unsigned index = 'a'; index <= 'z'; index++)
        table.at(index) = true;
    return table;
}();

constexpr bool is_tchar(const char letter)
{
    return kTchar.at(static_cast<unsigned char>(letter));
}

inline constexpr std::array<bool, 256> kQdtext = [] {
    std::array<bool, 256> table{};
    table.at('\t') = true;
    table.at(' ') = true;
    for (unsigned index = '!'; index <= '~'; index++)
        table.at(index) = true;
    table.at('"') = false;
    table.at('\\') = false;
    for (unsigned index = 0x80; index <= 0xFF; index++)
        table.at(index) = true;
    return table;
}();

constexpr bool is_qdtext(const char letter)
{
    return kQdtext.at(static_cast<unsigned char>(letter));
}

inline std::expected<std::string_view, ParseError> parse_quoted_string(const std::string_view text)
{
    if (!text.starts_with('"'))
        return std::unexpected(ParseError(kQuotedStringProblem, text, 0));
    std::string_view rest = text.substr(1);
    while (!rest.empty()) {
        const size_t at = text.size() - rest.size();
        const size_t stop = rest.find_first_of("\"\\");
        if (stop == std::string_view::npos)
            break;
        const std::string_view plain = rest.substr(0, stop);
        const auto bad = std::ranges::find_if_not(plain, is_qdtext);
        if (bad != plain.end())
            return std::unexpected(
                ParseError(kQdtextProblem, text, at + std::distance(plain.begin(), bad)));
        if (rest.at(stop) == '"')
            return text.substr(0, at + stop + 1);
        const std::string_view escaped = rest.substr(stop + 1);
        if (escaped.empty())
            break;
        if (!is_qdtext(escaped.front()) && escaped.front() != '"' && escaped.front() != '\\')
            return std::unexpected(ParseError(kQdtextProblem, text, at + stop + 1));
        rest = escaped.substr(1);
    }
    return std::unexpected(ParseError(kQuotedStringProblem, text, text.size()));
}

struct Field {
    std::string_view name;
    std::string_view value;
};

struct Fields {
    std::span<const Field> entries;
};

struct Request {
    std::string_view method;
    std::string_view target;
    Fields header_section;
    std::span<const std::byte> content;
    Fields trailer_section;
};

struct Uri {
    std::string_view scheme;
    std::string_view host;
    unsigned port;
    std::string_view path;
    std::string_view query;
};

struct Response {
    unsigned status;
    Fields header_section;
    std::span<const std::byte> content;
    Fields trailer_section;
};

struct Representation {
    std::string_view media_type;
    std::string_view content_coding;
    std::string_view language;
    std::string_view entity_tag;
    std::span<const std::byte> data;
};

struct Resource {
    std::string_view target;
    Representation (*select_representation)(const Request);
};


constexpr std::string_view skip_optional_whitespace(const std::string_view text)
{
    const size_t start = text.find_first_not_of(" \t");
    return start == std::string_view::npos ? std::string_view{} : text.substr(start);
}

struct FieldValueParameter {
    std::string_view name;
    std::string_view value;
    std::string_view rest;
};

inline std::expected<std::optional<FieldValueParameter>, ParseError>
parse_field_value_parameter(const std::string_view text)
{
    std::string_view rest = skip_optional_whitespace(text);
    while (rest.starts_with(';'))
        rest = skip_optional_whitespace(rest.substr(1));
    if (rest.empty())
        return std::optional<FieldValueParameter>{};
    const std::string_view name(rest.begin(), std::ranges::find_if_not(rest, is_tchar));
    if (name.empty())
        return std::unexpected(ParseError(kTcharProblem, text, text.size() - rest.size()));
    const std::string_view after = rest.substr(name.size());
    if (!after.starts_with('='))
        return std::unexpected(ParseError(kParameterProblem, text, text.size() - after.size()));
    const std::string_view raw = after.substr(1);
    const size_t begins = text.size() - raw.size();
    if (raw.starts_with('"')) {
        const auto quoted = parse_quoted_string(raw);
        if (!quoted)
            return std::unexpected(
                ParseError(quoted.error().problem(), text, begins + quoted.error().offset()));
        return FieldValueParameter{name, *quoted, raw.substr(quoted->size())};
    }
    const std::string_view value(raw.begin(), std::ranges::find_if_not(raw, is_tchar));
    if (value.empty())
        return std::unexpected(ParseError(kTcharProblem, text, begins));
    return FieldValueParameter{name, value, raw.substr(value.size())};
}

}

#endif
