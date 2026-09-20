#ifndef WEBMACHINE_HTTP_HPP
#define WEBMACHINE_HTTP_HPP

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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
    {"RFC 5234 B.1", "DIGIT", "The field value is not valid", "%x30-39", 400},
    {"RFC 9110 5.6.7", "month", "The timestamp is not valid",
     "Jan / Feb / Mar / Apr / May / Jun / Jul / Aug / Sep / Oct / Nov / Dec", 400},
    {"RFC 9110 5.6.7", "IMF-fixdate", "The timestamp is not valid",
     "day-name \",\" SP day SP month SP year SP hour \":\" minute \":\" second SP GMT", 400},
    {"RFC 9110 5.6.7", "time-of-day", "The timestamp is not valid",
     "hour \":\" minute \":\" second, hour 00-23, minute 00-59, second 00-60", 400},
    {"RFC 9110 5.6.7", "rfc850-date", "The timestamp is not valid",
     "day-name-l \",\" SP day \"-\" month \"-\" 2DIGIT SP time-of-day SP GMT", 400},
    {"RFC 9110 5.6.7", "asctime-date", "The timestamp is not valid",
     "day-name SP month SP ( 2DIGIT / ( SP 1DIGIT ) ) SP time-of-day SP 4DIGIT", 400},
    {"RFC 9110 7.2", "Host", "The Host field is not valid", "uri-host [ \":\" port ]", 400},
    {"RFC 3986 3.2.3", "port", "The Host field is not valid", "*DIGIT, at most 65535", 400},
    {"RFC 9112 3.2", "request-target", "The request target is not valid",
     "origin-form / absolute-form / authority-form / asterisk-form", 400},
    {"RFC 9110 4.1", "absolute-path", "The request target is not valid",
     "1*( \"/\" segment ), segment = *pchar", 400},
    {"RFC 3986 3.4", "query", "The request target is not valid",
     "*( pchar / \"/\" / \"?\" )", 400},
    {"RFC 9110 4.2.1", "scheme", "The request target is not valid", "\"http\" / \"https\"", 400},
    {"RFC 9110 4.2.4", "userinfo", "The request target is not valid",
     "no userinfo in an http or https URI", 400},
});

inline constexpr uint16_t kUnknownProblem = 0;
inline constexpr uint16_t kTcharProblem = 1;
inline constexpr uint16_t kQuotedStringProblem = 2;
inline constexpr uint16_t kQdtextProblem = 3;
inline constexpr uint16_t kParameterProblem = 4;
inline constexpr uint16_t kDigitProblem = 5;
inline constexpr uint16_t kMonthProblem = 6;
inline constexpr uint16_t kImfFixdateProblem = 7;
inline constexpr uint16_t kTimeOfDayProblem = 8;
inline constexpr uint16_t kRfc850DateProblem = 9;
inline constexpr uint16_t kAsctimeDateProblem = 10;
inline constexpr uint16_t kHostProblem = 11;
inline constexpr uint16_t kPortProblem = 12;
inline constexpr uint16_t kRequestTargetProblem = 13;
inline constexpr uint16_t kAbsolutePathProblem = 14;
inline constexpr uint16_t kQueryProblem = 15;
inline constexpr uint16_t kSchemeProblem = 16;
inline constexpr uint16_t kUserinfoProblem = 17;

struct Refusal {
    uint16_t problem;
    uint32_t offset;
};

constexpr Refusal moved_forward(const Refusal refusal, const size_t forward)
{
    return Refusal{refusal.problem, static_cast<uint32_t>(refusal.offset + forward)};
}

class ParseError : public std::runtime_error
{
public:
    ParseError(const Refusal refusal, const std::string_view text)
        : std::runtime_error(""), refusal_(refusal),
          found_byte_(refusal.offset < text.size()
                          ? static_cast<unsigned char>(text.at(refusal.offset))
                          : 0)
    {
        const size_t back = refusal.offset < 16 ? 0 : refusal.offset - 16;
        const size_t from = std::min(back, text.size());
        for (const char letter : text.substr(from, excerpt_.size()))
            excerpt_.at(excerpt_length_++) = letter;
    }

    const char *what() const noexcept override { return kProblems.at(refusal_.problem).title; }
    uint16_t problem() const noexcept { return refusal_.problem; }
    std::string_view section() const noexcept { return kProblems.at(refusal_.problem).section; }
    std::string_view rule() const noexcept { return kProblems.at(refusal_.problem).rule; }
    std::string_view title() const noexcept { return kProblems.at(refusal_.problem).title; }
    std::string_view allowed() const noexcept { return kProblems.at(refusal_.problem).allowed; }
    unsigned status() const noexcept { return kProblems.at(refusal_.problem).status; }
    size_t offset() const noexcept { return refusal_.offset; }
    unsigned char found_byte() const noexcept { return found_byte_; }
    std::string_view excerpt() const noexcept { return {excerpt_.data(), excerpt_length_}; }

private:
    Refusal refusal_;
    std::array<char, 32> excerpt_{};
    uint8_t excerpt_length_ = 0;
    unsigned char found_byte_;
};

constexpr uint64_t method_number(const std::string_view method)
{
    if (method.empty() || method.size() > sizeof(uint64_t))
        return 0;
    if consteval {
        uint64_t number = 0;
        for (size_t at = 0; at < method.size(); ++at)
            number |= static_cast<uint64_t>(static_cast<unsigned char>(method.at(at)))
                      << (at * 8);
        return number;
    }
    uint64_t number = 0;
    std::memcpy(&number, method.data(), sizeof number);
    return number & (~uint64_t{0} >> (8 * (sizeof number - method.size())));
}

inline constexpr uint64_t kConnect = method_number("CONNECT");
inline constexpr uint64_t kOptions = method_number("OPTIONS");

constexpr char ascii_lowered(const char letter)
{
    const unsigned byte = static_cast<unsigned char>(letter);
    return static_cast<char>(letter + 0x20 * (byte - 'A' < 26u));
}

constexpr bool equal_ignoring_case(const std::string_view left, const std::string_view right)
{
    return std::ranges::equal(left, right, [](const char a, const char b) {
        return ascii_lowered(a) == ascii_lowered(b);
    });
}

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

inline constexpr std::array<unsigned char, 16> kHighNibbleBit = [] {
    std::array<unsigned char, 16> table{};
    for (unsigned nibble = 0; nibble < 8; nibble++)
        table.at(nibble) = static_cast<unsigned char>(1 << nibble);
    return table;
}();

constexpr std::array<unsigned char, 16> low_nibble_bits_of(const std::array<bool, 256> &allowed)
{
    std::array<unsigned char, 16> table{};
    for (unsigned byte = 0; byte < 128; byte++)
        if (allowed.at(byte))
            table.at(byte & 0x0F) =
                static_cast<unsigned char>(table.at(byte & 0x0F) | (1 << (byte >> 4)));
    return table;
}

inline constexpr std::array<bool, 256> kRegName = [] {
    std::array<bool, 256> table{};
    for (const char letter : std::string_view("-._~!$&'()*+,;=%"))
        table.at(static_cast<unsigned char>(letter)) = true;
    for (unsigned index = '0'; index <= '9'; index++)
        table.at(index) = true;
    for (unsigned index = 'A'; index <= 'Z'; index++)
        table.at(index) = true;
    for (unsigned index = 'a'; index <= 'z'; index++)
        table.at(index) = true;
    return table;
}();

inline constexpr auto kRegNameLowBits = low_nibble_bits_of(kRegName);

inline constexpr std::array<bool, 256> kIpLiteral = [] {
    std::array<bool, 256> table{};
    for (const char letter : std::string_view(".:v"))
        table.at(static_cast<unsigned char>(letter)) = true;
    for (unsigned index = '0'; index <= '9'; index++)
        table.at(index) = true;
    for (unsigned index = 'A'; index <= 'F'; index++)
        table.at(index) = true;
    for (unsigned index = 'a'; index <= 'f'; index++)
        table.at(index) = true;
    return table;
}();

inline constexpr std::array<bool, 256> kLowercaseTchar = [] {
    std::array<bool, 256> table = kTchar;
    for (unsigned index = 'A'; index <= 'Z'; index++)
        table.at(index) = false;
    return table;
}();

// RFC 3986 3.3: segment = *pchar, and pchar = unreserved / pct-encoded /
// sub-delims / ":" / "@". The "%" of a pct-encoded triplet is a byte of
// the set; the two HEXDIG behind it are percent_decode's to check.
inline constexpr std::array<bool, 256> kPathByte = [] {
    std::array<bool, 256> table{};
    for (const char letter : std::string_view("-._~%!$&'()*+,;=:@/"))
        table.at(static_cast<unsigned char>(letter)) = true;
    for (unsigned index = '0'; index <= '9'; index++)
        table.at(index) = true;
    for (unsigned index = 'A'; index <= 'Z'; index++)
        table.at(index) = true;
    for (unsigned index = 'a'; index <= 'z'; index++)
        table.at(index) = true;
    return table;
}();

// RFC 3986 3.4: query = *( pchar / "/" / "?" ).
inline constexpr std::array<bool, 256> kQueryByte = [] {
    std::array<bool, 256> table = kPathByte;
    table.at('?') = true;
    return table;
}();

inline constexpr auto kIpLiteralLowBits = low_nibble_bits_of(kIpLiteral);
inline constexpr auto kPathByteLowBits = low_nibble_bits_of(kPathByte);
inline constexpr auto kQueryByteLowBits = low_nibble_bits_of(kQueryByte);
inline constexpr auto kTcharLowBits = low_nibble_bits_of(kTchar);
inline constexpr auto kQdtextLowBits = low_nibble_bits_of(kQdtext);
inline constexpr auto kLowercaseTcharLowBits = low_nibble_bits_of(kLowercaseTchar);

inline bool every_byte_is_allowed(const std::string_view text,
                                  const std::array<bool, 256> &allowed)
{
    for (const char letter : text)
        if (!allowed.at(static_cast<unsigned char>(letter))) [[unlikely]]
            return false;
    return true;
}

#if defined(__ARM_NEON)
inline bool neon_block_is_allowed(const unsigned char *at, const size_t length,
                                  const std::array<unsigned char, 16> &low_bits)
{
    const uint8x16_t bytes = vld1q_u8(at);
    const uint8x16_t low =
        vqtbl1q_u8(vld1q_u8(low_bits.data()), vandq_u8(bytes, vdupq_n_u8(0x0F)));
    const uint8x16_t high = vqtbl1q_u8(vld1q_u8(kHighNibbleBit.data()), vshrq_n_u8(bytes, 4));
    const uint8x16_t lanes = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8x16_t beyond =
        vcgeq_u8(lanes, vdupq_n_u8(static_cast<unsigned char>(std::min(length, size_t{16}))));
    return vminvq_u8(vorrq_u8(vandq_u8(low, high), beyond)) != 0;
}
#endif

inline constexpr size_t kWidePadding = 64;

#if defined(__AVX2__)
inline uint32_t avx2_block_refusals(const char *at, const __m256i low_table,
                                    const __m256i high_table)
{
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(at));
    const __m256i low =
        _mm256_shuffle_epi8(low_table, _mm256_and_si256(bytes, _mm256_set1_epi8(0x0F)));
    const __m256i high = _mm256_shuffle_epi8(
        high_table, _mm256_and_si256(_mm256_srli_epi16(bytes, 4), _mm256_set1_epi8(0x0F)));
    return static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_cmpeq_epi8(_mm256_and_si256(low, high), _mm256_setzero_si256())));
}
#endif

inline bool every_byte_is_allowed(const std::string_view text,
                                  const std::array<bool, 256> &allowed,
                                  [[maybe_unused]] const std::array<unsigned char, 16> &low_bits)
{
    static_assert(32 <= kWidePadding);
    if (text.empty()) [[unlikely]]
        return false;
#if defined(__AVX2__)
    const __m256i low_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(low_bits.data())));
    const __m256i high_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(kHighNibbleBit.data())));
    if (text.size() <= 32) {
        const uint32_t inside = text.size() == 32 ? ~0u : (1u << text.size()) - 1;
        return (avx2_block_refusals(text.data(), low_table, high_table) & inside) == 0;
    }
    size_t at = 0;
    for (; at + 32 <= text.size(); at += 32)
        if (avx2_block_refusals(std::next(text.data(), at), low_table, high_table) != 0)
            [[unlikely]]
            return false;
    if (at == text.size())
        return true;
    const uint32_t inside = (1u << (text.size() - at)) - 1;
    return (avx2_block_refusals(std::next(text.data(), at), low_table, high_table) & inside) == 0;
#elif defined(__ARM_NEON)
    const unsigned char *const from = reinterpret_cast<const unsigned char *>(text.data());
    for (size_t at = 0; at < text.size(); at += 16)
        if (!neon_block_is_allowed(std::next(from, at), text.size() - at, low_bits)) [[unlikely]]
            return false;
    return true;
#else
    return every_byte_is_allowed(text, allowed);
#endif
}

inline bool is_token(const std::string_view text)
{
    return every_byte_is_allowed(text, kTchar, kTcharLowBits);
}

inline bool is_lowercase_token(const std::string_view text)
{
    return every_byte_is_allowed(text, kLowercaseTchar, kLowercaseTcharLowBits);
}

inline bool is_reg_name(const std::string_view host)
{
    return every_byte_is_allowed(host, kRegName, kRegNameLowBits);
}

inline bool is_ip_literal(const std::string_view inside)
{
    return every_byte_is_allowed(inside, kIpLiteral);
}

inline std::expected<std::string_view, Refusal> parse_quoted_string(const std::string_view text)
{
    if (!text.starts_with('"')) [[unlikely]]
        return std::unexpected(Refusal{kQuotedStringProblem, static_cast<uint32_t>(0)});
    std::string_view rest = text.substr(1);
    while (!rest.empty()) {
        const size_t at = text.size() - rest.size();
        const size_t stop = rest.find_first_of("\"\\");
        if (stop == std::string_view::npos)
            break;
        const std::string_view plain = rest.substr(0, stop);
        const auto bad = std::ranges::find_if_not(plain, is_qdtext);
        if (bad != plain.end()) [[unlikely]]
            return std::unexpected(Refusal{
                kQdtextProblem, static_cast<uint32_t>(at + std::distance(plain.begin(), bad))});
        if (rest.at(stop) == '"')
            return text.substr(0, at + stop + 1);
        const std::string_view escaped = rest.substr(stop + 1);
        if (escaped.empty())
            break;
        if (!is_qdtext(escaped.front()) && escaped.front() != '"' && escaped.front() != '\\') [[unlikely]]
            return std::unexpected(Refusal{kQdtextProblem, static_cast<uint32_t>(at + stop + 1)});
        rest = escaped.substr(1);
    }
    return std::unexpected(Refusal{kQuotedStringProblem, static_cast<uint32_t>(text.size())});
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


inline std::optional<unsigned> parse_digits(const std::string_view text)
{
    if (text.empty()) [[unlikely]]
        return std::nullopt;
    unsigned value = 0;
    const auto done = std::from_chars(text.data(), text.data() + text.size(), value);
    if (done.ec != std::errc{} || done.ptr != text.data() + text.size()) [[unlikely]]
        return std::nullopt;
    return value;
}

inline std::optional<unsigned> parse_digits(const std::string_view text, const size_t count)
{
    if (text.size() != count) [[unlikely]]
        return std::nullopt;
    return parse_digits(text);
}

inline constexpr std::array kMonthNames = std::to_array<std::string_view>(
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"});

inline std::optional<unsigned> parse_month(const std::string_view text)
{
    const auto found = std::ranges::find(kMonthNames, text);
    if (found == kMonthNames.end()) [[unlikely]]
        return std::nullopt;
    return static_cast<unsigned>(std::distance(kMonthNames.begin(), found)) + 1;
}

constexpr std::string_view skip_optional_whitespace(const std::string_view text)
{
    const size_t start = text.find_first_not_of(" \t");
    return start == std::string_view::npos ? std::string_view{} : text.substr(start);
}

inline size_t find_comma_or_quote(const std::string_view text, const size_t at)
{
    for (size_t walked = at; walked < text.size(); ++walked)
        if (text.at(walked) == ',' || text.at(walked) == '"')
            return walked;
    return std::string_view::npos;
}

struct ListElement {
    std::string_view element;
    std::string_view rest;
};

inline std::optional<ListElement> parse_list_element(const std::string_view text)
{
    std::string_view rest = skip_optional_whitespace(text);
    while (rest.starts_with(','))
        rest = skip_optional_whitespace(rest.substr(1));
    if (rest.empty())
        return std::nullopt;
    size_t at = 0;
    while (at < rest.size()) {
        const size_t stop = find_comma_or_quote(rest, at);
        if (stop == std::string_view::npos) {
            at = rest.size();
            break;
        }
        if (rest.at(stop) == ',') {
            at = stop;
            break;
        }
        const auto quoted = parse_quoted_string(rest.substr(stop));
        if (!quoted) [[unlikely]]
            return ListElement{rest, std::string_view{}};
        at = stop + quoted->size();
    }
    const std::string_view whole = rest.substr(0, at);
    return ListElement{whole.substr(0, whole.find_last_not_of(" \t") + 1), rest.substr(at)};
}

struct FieldValueParameter {
    std::string_view name;
    std::string_view value;
    std::string_view rest;
};

inline std::expected<std::optional<FieldValueParameter>, Refusal>
parse_field_value_parameter(const std::string_view text)
{
    std::string_view rest = skip_optional_whitespace(text);
    while (rest.starts_with(';'))
        rest = skip_optional_whitespace(rest.substr(1));
    if (rest.empty())
        return std::optional<FieldValueParameter>{};
    const std::string_view name(rest.begin(), std::ranges::find_if_not(rest, is_tchar));
    if (name.empty()) [[unlikely]]
        return std::unexpected(Refusal{kTcharProblem, static_cast<uint32_t>(text.size() - rest.size())});
    const std::string_view after = rest.substr(name.size());
    if (!after.starts_with('=')) [[unlikely]]
        return std::unexpected(Refusal{kParameterProblem, static_cast<uint32_t>(text.size() - after.size())});
    const std::string_view raw = after.substr(1);
    const size_t begins = text.size() - raw.size();
    if (raw.starts_with('"')) {
        const auto quoted = parse_quoted_string(raw);
        if (!quoted) [[unlikely]]
            return std::unexpected(Refusal{quoted.error().problem, static_cast<uint32_t>(begins + quoted.error().offset)});
        return FieldValueParameter{name, *quoted, raw.substr(quoted->size())};
    }
    const std::string_view value(raw.begin(), std::ranges::find_if_not(raw, is_tchar));
    if (value.empty()) [[unlikely]]
        return std::unexpected(Refusal{kTcharProblem, static_cast<uint32_t>(begins)});
    return FieldValueParameter{name, value, raw.substr(value.size())};
}

inline std::optional<std::chrono::seconds> parse_time_of_day(const std::string_view text)
{
    if (text.size() != 8 || text.at(2) != ':' || text.at(5) != ':') [[unlikely]]
        return std::nullopt;
    const auto hour = parse_digits(text.substr(0, 2), 2);
    const auto minute = parse_digits(text.substr(3, 2), 2);
    const auto second = parse_digits(text.substr(6, 2), 2);
    if (!hour || !minute || !second) [[unlikely]]
        return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 60) [[unlikely]]
        return std::nullopt;
    return std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_imf_fixdate(const std::string_view text)
{
    if (text.size() != 29 || text.substr(3, 2) != ", " || text.at(7) != ' ' ||
        text.at(11) != ' ' || text.at(16) != ' ' || text.substr(25) != " GMT") [[unlikely]]
        return std::unexpected(Refusal{kImfFixdateProblem, static_cast<uint32_t>(0)});
    const auto day = parse_digits(text.substr(5, 2), 2);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(5)});
    const auto month = parse_month(text.substr(8, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, static_cast<uint32_t>(8)});
    const auto year = parse_digits(text.substr(12, 4), 4);
    if (!year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(12)});
    const auto time = parse_time_of_day(text.substr(17, 8));
    if (!time) [[unlikely]]
        return std::unexpected(Refusal{kTimeOfDayProblem, static_cast<uint32_t>(17)});
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(*year)},
                                           std::chrono::month{*month}, std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kImfFixdateProblem, static_cast<uint32_t>(5)});
    return std::chrono::sys_days{date} + *time;
}

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_rfc850_date(const std::string_view text, const std::chrono::year current_year)
{
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos || text.size() - comma != 24 ||
        text.substr(comma, 2) != ", ") [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, static_cast<uint32_t>(0)});
    const size_t at = comma + 2;
    const std::string_view tail = text.substr(at);
    if (tail.at(2) != '-' || tail.at(6) != '-' || tail.at(9) != ' ' || tail.substr(18) != " GMT") [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, static_cast<uint32_t>(at)});
    const auto day = parse_digits(tail.substr(0, 2), 2);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(at)});
    const auto month = parse_month(tail.substr(3, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, static_cast<uint32_t>(at + 3)});
    const auto short_year = parse_digits(tail.substr(7, 2), 2);
    if (!short_year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(at + 7)});
    const auto time = parse_time_of_day(tail.substr(10, 8));
    if (!time) [[unlikely]]
        return std::unexpected(Refusal{kTimeOfDayProblem, static_cast<uint32_t>(at + 10)});
    int full = (static_cast<int>(current_year) / 100) * 100 + static_cast<int>(*short_year);
    if (full - static_cast<int>(current_year) > 50)
        full -= 100;
    const std::chrono::year_month_day date{std::chrono::year{full}, std::chrono::month{*month},
                                           std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, static_cast<uint32_t>(at)});
    return std::chrono::sys_days{date} + *time;
}

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_asctime_date(const std::string_view text)
{
    if (text.size() != 24 || text.at(3) != ' ' || text.at(7) != ' ' || text.at(10) != ' ' ||
        text.at(19) != ' ') [[unlikely]]
        return std::unexpected(Refusal{kAsctimeDateProblem, static_cast<uint32_t>(0)});
    const auto month = parse_month(text.substr(4, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, static_cast<uint32_t>(4)});
    const std::string_view day_text = text.substr(8, 2);
    const auto day = parse_digits(day_text.starts_with(' ') ? day_text.substr(1) : day_text);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(8)});
    const auto time = parse_time_of_day(text.substr(11, 8));
    if (!time) [[unlikely]]
        return std::unexpected(Refusal{kTimeOfDayProblem, static_cast<uint32_t>(11)});
    const auto year = parse_digits(text.substr(20, 4), 4);
    if (!year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(20)});
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(*year)},
                                           std::chrono::month{*month}, std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kAsctimeDateProblem, static_cast<uint32_t>(8)});
    return std::chrono::sys_days{date} + *time;
}

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_http_date(const std::string_view text, const std::chrono::year current_year)
{
    const auto fixdate = parse_imf_fixdate(text);
    if (fixdate)
        return fixdate;
    if (const auto obsolete = parse_rfc850_date(text, current_year))
        return obsolete;
    if (const auto obsolete = parse_asctime_date(text))
        return obsolete;
    return fixdate;
}

struct Host {
    std::string_view uri_host;
    std::optional<unsigned> port;
};

inline std::expected<Host, Refusal> parse_host(const std::string_view text)
{
    if (text.empty()) [[unlikely]]
        return std::unexpected(Refusal{kHostProblem, 0});
    const size_t colon = text.starts_with('[') ? text.find(':', text.find(']')) : text.find(':');
    const std::string_view uri_host = text.substr(0, colon);
    if (uri_host.starts_with('[')) {
        if (!uri_host.ends_with(']')) [[unlikely]]
            return std::unexpected(Refusal{kHostProblem, 0});
        if (!is_ip_literal(uri_host.substr(1, uri_host.size() - 2))) [[unlikely]]
            return std::unexpected(Refusal{kHostProblem, 1});
    } else if (!is_reg_name(uri_host)) [[unlikely]] {
        return std::unexpected(Refusal{kHostProblem, 0});
    }
    if (colon == std::string_view::npos)
        return Host{uri_host, std::nullopt};
    const std::string_view digits = text.substr(colon + 1);
    if (digits.empty())
        return Host{uri_host, std::nullopt};
    const auto port = parse_digits(digits);
    if (!port || *port > 65535) [[unlikely]]
        return std::unexpected(Refusal{kPortProblem, static_cast<uint32_t>(colon + 1)});
    return Host{uri_host, *port};
}

enum class TargetForm : uint8_t { kOrigin, kAbsolute, kAuthority, kAsterisk };

inline std::expected<TargetForm, Refusal> request_target_form(const std::string_view text,
                                                              const uint64_t method)
{
    if (text.empty()) [[unlikely]]
        return std::unexpected(Refusal{kRequestTargetProblem, 0});
    if (method == kConnect)
        return TargetForm::kAuthority;
    if (text == "*")
        return method == kOptions
                   ? std::expected<TargetForm, Refusal>(TargetForm::kAsterisk)
                   : std::unexpected(Refusal{kRequestTargetProblem, 0});
    if (text.starts_with('/'))
        return TargetForm::kOrigin;
    return TargetForm::kAbsolute;
}

struct RequestTarget {
    TargetForm form;
    std::string_view scheme;
    Host authority;
    std::string_view path;
    std::string_view query;
};

inline std::expected<std::string_view, Refusal> parse_query(const std::string_view text)
{
    if (!text.empty() && !every_byte_is_allowed(text, kQueryByte, kQueryByteLowBits)) [[unlikely]]
        return std::unexpected(Refusal{kQueryProblem, 0});
    return text;
}

struct OriginForm {
    std::string_view path;
    std::string_view query;
};

// RFC 9112 3.2.1: origin-form = absolute-path [ "?" query ].
inline std::expected<OriginForm, Refusal> parse_origin_form(const std::string_view text)
{
    if (!text.starts_with('/')) [[unlikely]]
        return std::unexpected(Refusal{kAbsolutePathProblem, 0});
    const size_t question = text.find('?');
    const std::string_view path = text.substr(0, question);
    if (!every_byte_is_allowed(path, kPathByte, kPathByteLowBits)) [[unlikely]]
        return std::unexpected(Refusal{kAbsolutePathProblem, 0});
    if (question == std::string_view::npos)
        return OriginForm{path, {}};
    const auto query = parse_query(text.substr(question + 1));
    if (!query) [[unlikely]]
        return std::unexpected(moved_forward(query.error(), question + 1));
    return OriginForm{path, *query};
}

// RFC 9112 3.2.3: authority-form = uri-host ":" port. Both are there.
inline std::expected<Host, Refusal> parse_authority_form(const std::string_view text)
{
    const auto host = parse_host(text);
    if (!host) [[unlikely]]
        return host;
    if (!host->port) [[unlikely]]
        return std::unexpected(Refusal{kPortProblem, static_cast<uint32_t>(text.size())});
    return *host;
}

// RFC 9112 3.2.2: absolute-form = absolute-URI, which for this server is
// the http-URI and the https-URI of RFC 9110 4.2.1 and 4.2.2:
// "http://" authority path-abempty [ "?" query ]. RFC 9110 4.2.4 says a
// recipient treats a userinfo as an error, because it hides the
// authority from a reader.
inline std::expected<RequestTarget, Refusal> parse_absolute_form(const std::string_view text)
{
    const size_t mark = text.find("://");
    if (mark == std::string_view::npos) [[unlikely]]
        return std::unexpected(Refusal{kSchemeProblem, 0});
    const std::string_view scheme = text.substr(0, mark);
    if (!equal_ignoring_case(scheme, "http") && !equal_ignoring_case(scheme, "https")) [[unlikely]]
        return std::unexpected(Refusal{kSchemeProblem, 0});
    const size_t from = mark + 3;
    const size_t end = text.find_first_of("/?", from);
    const std::string_view authority = text.substr(from, end - from);
    const size_t user = authority.find('@');
    if (user != std::string_view::npos) [[unlikely]]
        return std::unexpected(Refusal{kUserinfoProblem, static_cast<uint32_t>(from + user)});
    const auto host = parse_host(authority);
    if (!host) [[unlikely]]
        return std::unexpected(moved_forward(host.error(), from));
    if (end == std::string_view::npos)
        return RequestTarget{TargetForm::kAbsolute, scheme, *host, "/", {}};
    if (text.at(end) == '?') {
        const auto query = parse_query(text.substr(end + 1));
        if (!query) [[unlikely]]
            return std::unexpected(moved_forward(query.error(), end + 1));
        return RequestTarget{TargetForm::kAbsolute, scheme, *host, "/", *query};
    }
    const auto origin = parse_origin_form(text.substr(end));
    if (!origin) [[unlikely]]
        return std::unexpected(moved_forward(origin.error(), end));
    return RequestTarget{TargetForm::kAbsolute, scheme, *host, origin->path, origin->query};
}

struct PathWalk {
    std::string_view segment;
    std::string_view rest;
};

// RFC 3986 3.3: a path is segments behind slashes, and the walk gives
// them one at a time. "/a/" holds two segments, "a" and the empty one
// the trailing slash makes, so the caller stops on an empty rest and
// not on an empty segment.
inline PathWalk next_segment(const std::string_view path)
{
    const std::string_view after = path.substr(path.starts_with('/') ? 1 : 0);
    const size_t slash = after.find('/');
    if (slash == std::string_view::npos)
        return PathWalk{after, {}};
    return PathWalk{after.substr(0, slash), after.substr(slash)};
}

constexpr bool is_dot_segment(const std::string_view segment)
{
    return segment == "." || segment == "..";
}

inline bool path_has_dot_segment(const std::string_view path)
{
    std::string_view rest = path;
    while (!rest.empty()) {
        const PathWalk walk = next_segment(rest);
        if (is_dot_segment(walk.segment)) [[unlikely]]
            return true;
        rest = walk.rest;
    }
    return false;
}

// RFC 3986 5.2.4, the five cases of its loop in its order. RFC 3986
// 6.2.2.3 asks a recipient to run it over a path that is already
// absolute, because a "." or a ".." names the same resource as the path
// without it. A path that carries none comes back unchanged, which
// path_has_dot_segment answers without building anything.
inline std::string remove_dot_segments(const std::string_view path)
{
    std::string output;
    output.reserve(path.size());
    std::string_view input = path;
    while (!input.empty()) {
        if (input.starts_with("../")) {
            input.remove_prefix(3);
        } else if (input.starts_with("./")) {
            input.remove_prefix(2);
        } else if (input.starts_with("/./") || input == "/.") {
            input.remove_prefix(2);
            if (input.empty())
                input = "/";
        } else if (input.starts_with("/../") || input == "/..") {
            input.remove_prefix(3);
            if (input.empty())
                input = "/";
            const size_t slash = output.rfind('/');
            output.resize(slash == std::string::npos ? 0 : slash);
        } else if (is_dot_segment(input)) {
            input = {};
        } else {
            const size_t slash = input.find('/', 1);
            const size_t end = slash == std::string_view::npos ? input.size() : slash;
            output.append(input.substr(0, end));
            input.remove_prefix(end);
        }
    }
    return output;
}

inline std::expected<RequestTarget, Refusal> parse_request_target(const std::string_view text,
                                                                  const uint64_t method)
{
    const auto form = request_target_form(text, method);
    if (!form) [[unlikely]]
        return std::unexpected(form.error());
    switch (*form) {
    case TargetForm::kOrigin: {
        const auto origin = parse_origin_form(text);
        if (!origin) [[unlikely]]
            return std::unexpected(origin.error());
        return RequestTarget{*form, {}, {}, origin->path, origin->query};
    }
    case TargetForm::kAbsolute:
        return parse_absolute_form(text);
    case TargetForm::kAuthority: {
        const auto authority = parse_authority_form(text);
        if (!authority) [[unlikely]]
            return std::unexpected(authority.error());
        return RequestTarget{*form, {}, *authority, {}, {}};
    }
    case TargetForm::kAsterisk:
        return RequestTarget{*form, {}, {}, {}, {}};
    }
    std::unreachable();
}

}

#endif
