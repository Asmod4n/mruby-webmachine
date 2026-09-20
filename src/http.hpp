#pragma once

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
#include <variant>

#include <ada.h>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace http
{

struct Problem {
    std::string_view section;
    std::string_view rule;
    std::string_view title;
    std::string_view allowed;
    uint16_t status;
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
    {"RFC 3986 2.1", "pct-encoded", "The request target is not valid", "\"%\" HEXDIG HEXDIG",
     400},
    {"RFC 9110 8.8.3", "entity-tag", "The entity tag is not valid",
     "[ \"W/\" ] DQUOTE *etagc DQUOTE, etagc = %x21 / %x23-7E / obs-text", 400},
    {"RFC 9110 8.3.1", "media-type", "The media type is not valid",
     "type \"/\" subtype *( OWS \";\" OWS parameter ), type and subtype are tokens", 400},
    {"RFC 9110 8.6", "Content-Length", "The Content-Length field is not valid",
     "1*DIGIT, and the number fits in 64 bits", 400},
    {"RFC 9110 12.4.2", "qvalue", "The quality value is not valid",
     "( \"0\" [ \".\" 0*3DIGIT ] ) / ( \"1\" [ \".\" 0*3(\"0\") ] )", 400},
    {"RFC 9110 14.1.1", "range-spec", "The Range field is not valid",
     "range-unit \"=\" OWS 1#( first-pos \"-\" [ last-pos ] / \"-\" suffix-length )", 400},
    {"RFC 9110 8.3.1", "media-type", "A media type this resource provides is not valid",
     "type \"/\" subtype *( OWS \";\" OWS parameter ), type and subtype are tokens", 500},
    {"RFC 9651 4.1", "sf-item",
     "A media type this resource provides cannot be spelled as a Structured Field",
     "sf-token = ( ALPHA / \"*\" ) *( tchar / \":\" / \"/\" ); a string holds %x20-7E; "
     "a parameter key holds ( lcalpha / \"*\" ) *( lcalpha / DIGIT / \"_\" / \"-\" / \".\" / \"*\" )",
     500},
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
inline constexpr uint16_t kPctEncodedProblem = 18;
inline constexpr uint16_t kEntityTagProblem = 19;
inline constexpr uint16_t kMediaTypeProblem = 20;
inline constexpr uint16_t kContentLengthProblem = 21;
inline constexpr uint16_t kQvalueProblem = 22;
inline constexpr uint16_t kRangeProblem = 23;
inline constexpr uint16_t kProvidedMediaTypeProblem = 24;
inline constexpr uint16_t kStructuredItemProblem = 25;

struct Refusal {
    uint16_t problem;
    uint32_t offset;
};

constexpr Refusal moved_forward(const Refusal refusal, const size_t forward)
{
    return Refusal{refusal.problem, static_cast<uint32_t>(refusal.offset + forward)};
}

inline constexpr size_t kExcerptBefore = 16;

class ParseError : public std::runtime_error
{
public:
    ParseError(const Refusal refusal, const std::string_view text)
        : std::runtime_error(""), refusal_(refusal),
          found_byte_(refusal.offset < text.size()
                          ? static_cast<unsigned char>(text.at(refusal.offset))
                          : 0)
    {
        const size_t back =
            refusal.offset < kExcerptBefore ? 0 : refusal.offset - kExcerptBefore;
        const size_t from = std::min(back, text.size());
        for (const char letter : text.substr(from, excerpt_.size()))
            excerpt_.at(excerpt_length_++) = letter;
    }

    const char *what() const noexcept override
    {
        return kProblems.at(refusal_.problem).title.data();
    }
    uint16_t problem() const noexcept { return refusal_.problem; }
    std::string_view section() const noexcept { return kProblems.at(refusal_.problem).section; }
    std::string_view rule() const noexcept { return kProblems.at(refusal_.problem).rule; }
    std::string_view title() const noexcept { return kProblems.at(refusal_.problem).title; }
    std::string_view allowed() const noexcept { return kProblems.at(refusal_.problem).allowed; }
    uint16_t status() const noexcept { return kProblems.at(refusal_.problem).status; }
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

inline constexpr uint64_t kConnectNumber = method_number("CONNECT");
inline constexpr uint64_t kOptionsNumber = method_number("OPTIONS");
inline constexpr uint64_t kGetNumber = method_number("GET");
inline constexpr uint64_t kHeadNumber = method_number("HEAD");
inline constexpr uint64_t kPostNumber = method_number("POST");
inline constexpr uint64_t kPutNumber = method_number("PUT");
inline constexpr uint64_t kDeleteNumber = method_number("DELETE");
inline constexpr uint64_t kTraceNumber = method_number("TRACE");
inline constexpr uint64_t kQueryNumber = method_number("QUERY");

enum class Method : uint8_t {
    kUnknown,
    kGet,
    kHead,
    kPost,
    kPut,
    kDelete,
    kConnect,
    kOptions,
    kTrace,
    kQuery,
};

constexpr Method method_of(const std::string_view text)
{
    switch (method_number(text)) {
    case kGetNumber:
        return Method::kGet;
    case kHeadNumber:
        return Method::kHead;
    case kPostNumber:
        return Method::kPost;
    case kPutNumber:
        return Method::kPut;
    case kDeleteNumber:
        return Method::kDelete;
    case kConnectNumber:
        return Method::kConnect;
    case kOptionsNumber:
        return Method::kOptions;
    case kTraceNumber:
        return Method::kTrace;
    case kQueryNumber:
        return Method::kQuery;
    default:
        return Method::kUnknown;
    }
}

constexpr std::string_view method_name_of(const Method method)
{
    switch (method) {
    case Method::kGet:
        return "GET";
    case Method::kHead:
        return "HEAD";
    case Method::kPost:
        return "POST";
    case Method::kPut:
        return "PUT";
    case Method::kDelete:
        return "DELETE";
    case Method::kConnect:
        return "CONNECT";
    case Method::kOptions:
        return "OPTIONS";
    case Method::kTrace:
        return "TRACE";
    case Method::kQuery:
        return "QUERY";
    case Method::kUnknown:
        return {};
    }
    return {};
}

// webmachine-ruby's STANDARD_HTTP_METHODS holds the eight of RFC 9110
// 9.3, and its known_methods returns that list. This one is those eight
// and QUERY, which RFC 10008 made Standards Track long after webmachine
// was written. It says what this server understands, so a method outside
// it is the 501 of B12. What a resource permits is allowed_methods, and
// that default stays GET and HEAD, as webmachine has it - a resource
// that wants QUERY says so.
inline constexpr std::array kKnownMethods =
    std::to_array({Method::kGet, Method::kHead, Method::kPost, Method::kPut, Method::kDelete,
                   Method::kTrace, Method::kConnect, Method::kOptions, Method::kQuery});

// What a resource permits, which is the 405 of B10. webmachine-ruby
// defaults allowed_methods to GET and HEAD; this tree adds QUERY,
// because RFC 10008 2.4 gives the honest answer for a resource that does
// not understand one: "If a media type is specified but is not supported
// by the resource, a 415 (Unsupported Media Type) is appropriate. This
// specifically includes the case where the media type is known in
// principle, but it lacks semantics specific to a QUERY to the target
// resource." So a default QUERY is refused at B5 by known_content_type?,
// not answered with the whole representation at O18.
inline constexpr std::array kAllowedMethods =
    std::to_array({Method::kGet, Method::kHead, Method::kQuery});

constexpr bool is_known_method(const Method method)
{
    return std::ranges::find(kKnownMethods, method) != kKnownMethods.end();
}

// RFC 9110 9.2.1: "Of the request methods defined by this specification,
// the GET, HEAD, OPTIONS, and TRACE methods are defined to be safe."
// RFC 10008 2.1 adds QUERY: "QUERY requests are safe with regard to the
// target resource."
constexpr bool is_safe(const Method method)
{
    return method == Method::kGet || method == Method::kHead || method == Method::kOptions ||
           method == Method::kTrace || method == Method::kQuery;
}

// RFC 9110 9.2.2: "Of the request methods defined by this specification,
// PUT, DELETE, and safe request methods are idempotent."
constexpr bool is_idempotent(const Method method)
{
    return is_safe(method) || method == Method::kPut || method == Method::kDelete;
}

// RFC 9110 9.2.3: "This specification defines caching semantics for GET,
// HEAD, and POST, although the overwhelming majority of cache
// implementations only support GET and HEAD." RFC 10008 2.7: "The
// response to a QUERY method is cacheable; a cache MAY use it to satisfy
// subsequent QUERY requests."
constexpr bool is_cacheable(const Method method)
{
    return method == Method::kGet || method == Method::kHead || method == Method::kPost ||
           method == Method::kQuery;
}

constexpr char ascii_lowered(const char letter)
{
    const unsigned byte = static_cast<unsigned char>(letter);
    return static_cast<char>(letter + 0x20 * (byte - 'A' < 26u));
}

inline std::string ascii_lowered_copy(const std::string_view text)
{
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), ascii_lowered);
    return lowered;
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

constexpr bool is_alpha(const char letter)
{
    return (static_cast<unsigned char>(letter) | 0x20u) - 'a' < 26u;
}

constexpr bool is_digit(const char letter)
{
    return static_cast<unsigned char>(letter) - '0' < 10u;
}

constexpr bool is_alphanum(const char letter)
{
    return is_alpha(letter) || is_digit(letter);
}

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

constexpr std::array<unsigned char, 16>
ascii_low_nibble_bits_of(const std::array<bool, 256> &allowed)
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

inline constexpr std::array<bool, 256> kEtagc = [] {
    std::array<bool, 256> table{};
    table.at(0x21) = true;
    for (unsigned index = 0x23; index <= 0x7E; index++)
        table.at(index) = true;
    for (unsigned index = 0x80; index <= 0xFF; index++)
        table.at(index) = true;
    return table;
}();

inline constexpr auto kRegNameLowBits = ascii_low_nibble_bits_of(kRegName);

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

inline constexpr std::array<bool, 256> kQueryByte = [] {
    std::array<bool, 256> table = kPathByte;
    table.at('?') = true;
    return table;
}();

inline constexpr auto kPathByteLowBits = ascii_low_nibble_bits_of(kPathByte);
inline constexpr auto kQueryByteLowBits = ascii_low_nibble_bits_of(kQueryByte);
inline constexpr auto kTcharLowBits = ascii_low_nibble_bits_of(kTchar);
inline constexpr auto kLowercaseTcharLowBits = ascii_low_nibble_bits_of(kLowercaseTchar);

inline bool every_byte_is_allowed(const std::string_view text,
                                  const std::array<bool, 256> &allowed)
{
    for (const char letter : text)
        if (!allowed.at(static_cast<unsigned char>(letter))) [[unlikely]]
            return false;
    return true;
}

#if defined(__ARM_NEON)
inline uint64_t neon_block_refusals(const unsigned char *at,
                                    const std::array<unsigned char, 16> &low_bits)
{
    const uint8x16_t bytes = vld1q_u8(at);
    const uint8x16_t low =
        vqtbl1q_u8(vld1q_u8(low_bits.data()), vandq_u8(bytes, vdupq_n_u8(0x0F)));
    const uint8x16_t high = vqtbl1q_u8(vld1q_u8(kHighNibbleBit.data()), vshrq_n_u8(bytes, 4));
    const uint8x16_t refused = vceqq_u8(vandq_u8(low, high), vdupq_n_u8(0));
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(refused), 4)), 0);
}

inline constexpr size_t kNeonNibblesPerByte = 4;
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

inline size_t allowed_run_length(const std::string_view padded,
                                 [[maybe_unused]] const std::array<bool, 256> &allowed,
                                 [[maybe_unused]] const std::array<unsigned char, 16> &low_bits)
{
    static_assert(32 <= kWidePadding);
#if defined(__AVX2__)
    const __m256i low_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(low_bits.data())));
    const __m256i high_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(kHighNibbleBit.data())));
    for (size_t at = 0; at < padded.size(); at += 32) {
        const uint32_t refused =
            avx2_block_refusals(std::next(padded.data(), at), low_table, high_table);
        if (refused != 0)
            return std::min(at + static_cast<size_t>(std::countr_zero(refused)), padded.size());
    }
    return padded.size();
#elif defined(__ARM_NEON)
    const unsigned char *const from = reinterpret_cast<const unsigned char *>(padded.data());
    for (size_t at = 0; at < padded.size(); at += 16) {
        const uint64_t refused = neon_block_refusals(std::next(from, at), low_bits);
        if (refused != 0)
            return std::min(at + static_cast<size_t>(std::countr_zero(refused)) /
                                     kNeonNibblesPerByte,
                            padded.size());
    }
    return padded.size();
#else
    const auto found = std::ranges::find_if_not(padded, [&allowed](const char letter) {
        return allowed.at(static_cast<unsigned char>(letter));
    });
    return static_cast<size_t>(std::distance(padded.begin(), found));
#endif
}

#if defined(__AVX2__)
inline uint32_t avx2_field_value_refusals(const char *at)
{
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(at));
    const __m256i below_space = _mm256_cmpeq_epi8(
        _mm256_max_epu8(bytes, _mm256_set1_epi8(0x1f)), _mm256_set1_epi8(0x1f));
    const __m256i horizontal_tab = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8('\t'));
    const __m256i delete_byte = _mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(0x7f));
    return static_cast<uint32_t>(_mm256_movemask_epi8(
        _mm256_or_si256(_mm256_andnot_si256(horizontal_tab, below_space), delete_byte)));
}
#elif defined(__ARM_NEON)
inline uint64_t neon_field_value_refusals(const unsigned char *at)
{
    const uint8x16_t bytes = vld1q_u8(at);
    const uint8x16_t below_space = vcleq_u8(bytes, vdupq_n_u8(0x1f));
    const uint8x16_t horizontal_tab = vceqq_u8(bytes, vdupq_n_u8('\t'));
    const uint8x16_t delete_byte = vceqq_u8(bytes, vdupq_n_u8(0x7f));
    const uint8x16_t refused =
        vorrq_u8(vbicq_u8(below_space, horizontal_tab), delete_byte);
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(refused), 4)), 0);
}
#endif

inline size_t field_value_run_length(const std::string_view padded)
{
    static_assert(32 <= kWidePadding);
#if defined(__AVX2__)
    for (size_t at = 0; at < padded.size(); at += 32) {
        const uint32_t refused = avx2_field_value_refusals(std::next(padded.data(), at));
        if (refused != 0)
            return std::min(at + static_cast<size_t>(std::countr_zero(refused)), padded.size());
    }
    return padded.size();
#elif defined(__ARM_NEON)
    const unsigned char *const from = reinterpret_cast<const unsigned char *>(padded.data());
    for (size_t at = 0; at < padded.size(); at += 16) {
        const uint64_t refused = neon_field_value_refusals(std::next(from, at));
        if (refused != 0)
            return std::min(at + static_cast<size_t>(std::countr_zero(refused)) /
                                     kNeonNibblesPerByte,
                            padded.size());
    }
    return padded.size();
#else
    const auto found = std::ranges::find_if_not(padded, [](const char letter) {
        const unsigned char byte = static_cast<unsigned char>(letter);
        return (byte >= 0x20 || byte == '\t') && byte != 0x7f;
    });
    return static_cast<size_t>(std::distance(padded.begin(), found));
#endif
}

inline bool is_field_value(const std::string_view text)
{
    if (text.empty())
        return true;
    if (text.front() == ' ' || text.front() == '\t' || text.back() == ' ' ||
        text.back() == '\t')
        return false;
    return field_value_run_length(text) == text.size();
}

inline bool is_token(const std::string_view text)
{
    return !text.empty() && allowed_run_length(text, kTchar, kTcharLowBits) == text.size();
}

inline bool is_lowercase_token(const std::string_view text)
{
    return !text.empty() &&
           allowed_run_length(text, kLowercaseTchar, kLowercaseTcharLowBits) == text.size();
}

inline bool is_reg_name(const std::string_view host)
{
    return !host.empty() && allowed_run_length(host, kRegName, kRegNameLowBits) == host.size();
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

struct Request {
    std::string_view method;
    std::string_view target;
    std::span<const Field> header_section;
    std::span<const std::byte> content;
    std::span<const Field> trailer_section;
};

struct Response {
    uint16_t status;
    std::span<const Field> header_section;
    std::span<const std::byte> content;
    std::span<const Field> trailer_section;
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

enum class FieldCombining : uint8_t { kList, kRefuse, kMustAgree, kNeverCombined };

constexpr FieldCombining field_combining(const std::string_view name)
{
    switch (name.size()) {
    case 4:
        return equal_ignoring_case(name, "host") ? FieldCombining::kRefuse
                                                 : FieldCombining::kList;
    case 10:
        return equal_ignoring_case(name, "set-cookie") ? FieldCombining::kNeverCombined
                                                       : FieldCombining::kList;
    case 12:
        return equal_ignoring_case(name, "content-type") ? FieldCombining::kRefuse
                                                         : FieldCombining::kList;
    case 14:
        return equal_ignoring_case(name, "content-length") ? FieldCombining::kMustAgree
                                                           : FieldCombining::kList;
    default:
        return FieldCombining::kList;
    }
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

inline constexpr size_t kTimeOfDayLength = 8;
inline constexpr size_t kHourAt = 0;
inline constexpr size_t kMinuteAt = 3;
inline constexpr size_t kSecondAt = 6;

inline std::optional<std::chrono::seconds> parse_time_of_day(const std::string_view text)
{
    if (text.size() != kTimeOfDayLength || text.at(kMinuteAt - 1) != ':' ||
        text.at(kSecondAt - 1) != ':') [[unlikely]]
        return std::nullopt;
    const auto hour = parse_digits(text.substr(kHourAt, 2), 2);
    const auto minute = parse_digits(text.substr(kMinuteAt, 2), 2);
    const auto second = parse_digits(text.substr(kSecondAt, 2), 2);
    if (!hour || !minute || !second) [[unlikely]]
        return std::nullopt;
    if (*hour > 23 || *minute > 59 || *second > 60) [[unlikely]]
        return std::nullopt;
    return std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

inline constexpr size_t kFixdateLength = 29;
inline constexpr size_t kFixdateDayAt = 5;
inline constexpr size_t kFixdateMonthAt = 8;
inline constexpr size_t kFixdateYearAt = 12;
inline constexpr size_t kFixdateTimeAt = 17;
inline constexpr size_t kFixdateZoneAt = 25;

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_imf_fixdate(const std::string_view text)
{
    if (text.size() != kFixdateLength || text.substr(kFixdateDayAt - 2, 2) != ", " ||
        text.at(kFixdateMonthAt - 1) != ' ' || text.at(kFixdateYearAt - 1) != ' ' ||
        text.at(kFixdateTimeAt - 1) != ' ' || text.substr(kFixdateZoneAt) != " GMT") [[unlikely]]
        return std::unexpected(Refusal{kImfFixdateProblem, 0});
    const auto day = parse_digits(text.substr(kFixdateDayAt, 2), 2);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, kFixdateDayAt});
    const auto month = parse_month(text.substr(kFixdateMonthAt, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, kFixdateMonthAt});
    const auto year = parse_digits(text.substr(kFixdateYearAt, 4), 4);
    if (!year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, kFixdateYearAt});
    const auto time = parse_time_of_day(text.substr(kFixdateTimeAt, kTimeOfDayLength));
    if (!time) [[unlikely]]
        return std::unexpected(Refusal{kTimeOfDayProblem, kFixdateTimeAt});
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(*year)},
                                           std::chrono::month{*month}, std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kImfFixdateProblem, kFixdateDayAt});
    return std::chrono::sys_days{date} + *time;
}

// RFC 9110 5.6.7: day-name = %s"Mon" / %s"Tue" / %s"Wed" / %s"Thu" /
// %s"Fri" / %s"Sat" / %s"Sun", and %s means the case is part of the rule.
inline constexpr std::array kDayNames = std::to_array<std::string_view>(
    {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"});

constexpr std::array<char, 2> two_digits(const unsigned value)
{
    return {static_cast<char>('0' + value / 10 % 10), static_cast<char>('0' + value % 10)};
}

inline std::array<char, kFixdateLength> spell_imf_fixdate(const std::chrono::sys_seconds moment)
{
    const std::chrono::sys_days midnight = std::chrono::floor<std::chrono::days>(moment);
    const std::chrono::year_month_day date{midnight};
    const std::chrono::hh_mm_ss<std::chrono::seconds> time{moment - midnight};
    const unsigned year = static_cast<unsigned>(static_cast<int>(date.year()));
    std::array<char, kFixdateLength> spelled{};
    const auto put = [&spelled](const size_t at, const std::string_view text) {
        std::ranges::copy(text, std::next(spelled.begin(), static_cast<ptrdiff_t>(at)));
    };
    const auto put_two = [&spelled](const size_t at, const unsigned value) {
        const std::array<char, 2> digits = two_digits(value);
        std::ranges::copy(digits, std::next(spelled.begin(), static_cast<ptrdiff_t>(at)));
    };
    put(0, kDayNames.at(std::chrono::weekday{midnight}.iso_encoding() - 1));
    put(kDayNames.front().size(), ", ");
    put_two(kFixdateDayAt, static_cast<unsigned>(date.day()));
    put(kFixdateMonthAt - 1, " ");
    put(kFixdateMonthAt, kMonthNames.at(static_cast<unsigned>(date.month()) - 1));
    put(kFixdateYearAt - 1, " ");
    put_two(kFixdateYearAt, year / 100);
    put_two(kFixdateYearAt + 2, year % 100);
    put(kFixdateTimeAt - 1, " ");
    put_two(kFixdateTimeAt, static_cast<unsigned>(time.hours().count()));
    put(kFixdateTimeAt + 2, ":");
    put_two(kFixdateTimeAt + 3, static_cast<unsigned>(time.minutes().count()));
    put(kFixdateTimeAt + 5, ":");
    put_two(kFixdateTimeAt + 6, static_cast<unsigned>(time.seconds().count()));
    put(kFixdateZoneAt, " GMT");
    return spelled;
}

// RFC 9110 6.6.1: "An origin server with a clock ... MUST generate a Date
// header field in all 2xx, 3xx, and 4xx responses, and MAY generate a
// Date header field in 1xx and 5xx responses."
constexpr bool date_is_required(const unsigned status)
{
    return status >= 200 && status < 500;
}

inline constexpr size_t kRfc850TailLength = 22;
inline constexpr size_t kRfc850DayAt = 0;
inline constexpr size_t kRfc850MonthAt = 3;
inline constexpr size_t kRfc850YearAt = 7;
inline constexpr size_t kRfc850TimeAt = 10;
inline constexpr size_t kRfc850ZoneAt = 18;

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_rfc850_date(const std::string_view text, const std::chrono::year current_year)
{
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos || text.size() - comma != kRfc850TailLength + 2 ||
        text.substr(comma, 2) != ", ") [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, 0});
    const size_t at = comma + 2;
    const std::string_view tail = text.substr(at);
    if (tail.at(kRfc850MonthAt - 1) != '-' || tail.at(kRfc850YearAt - 1) != '-' ||
        tail.at(kRfc850TimeAt - 1) != ' ' || tail.substr(kRfc850ZoneAt) != " GMT") [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, static_cast<uint32_t>(at)});
    const auto day = parse_digits(tail.substr(kRfc850DayAt, 2), 2);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(at + kRfc850DayAt)});
    const auto month = parse_month(tail.substr(kRfc850MonthAt, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, static_cast<uint32_t>(at + kRfc850MonthAt)});
    const auto short_year = parse_digits(tail.substr(kRfc850YearAt, 2), 2);
    if (!short_year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, static_cast<uint32_t>(at + kRfc850YearAt)});
    const auto time = parse_time_of_day(tail.substr(kRfc850TimeAt, kTimeOfDayLength));
    if (!time) [[unlikely]]
        return std::unexpected(
            Refusal{kTimeOfDayProblem, static_cast<uint32_t>(at + kRfc850TimeAt)});
    int full = (static_cast<int>(current_year) / 100) * 100 + static_cast<int>(*short_year);
    if (full - static_cast<int>(current_year) > 50)
        full -= 100;
    const std::chrono::year_month_day date{std::chrono::year{full}, std::chrono::month{*month},
                                           std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kRfc850DateProblem, static_cast<uint32_t>(at)});
    return std::chrono::sys_days{date} + *time;
}

inline constexpr size_t kAsctimeLength = 24;
inline constexpr size_t kAsctimeMonthAt = 4;
inline constexpr size_t kAsctimeDayAt = 8;
inline constexpr size_t kAsctimeTimeAt = 11;
inline constexpr size_t kAsctimeYearAt = 20;

inline std::expected<std::chrono::sys_seconds, Refusal>
parse_asctime_date(const std::string_view text)
{
    if (text.size() != kAsctimeLength || text.at(kAsctimeMonthAt - 1) != ' ' ||
        text.at(kAsctimeDayAt - 1) != ' ' || text.at(kAsctimeTimeAt - 1) != ' ' ||
        text.at(kAsctimeYearAt - 1) != ' ') [[unlikely]]
        return std::unexpected(Refusal{kAsctimeDateProblem, 0});
    const auto month = parse_month(text.substr(kAsctimeMonthAt, 3));
    if (!month) [[unlikely]]
        return std::unexpected(Refusal{kMonthProblem, kAsctimeMonthAt});
    const std::string_view day_text = text.substr(kAsctimeDayAt, 2);
    const auto day = parse_digits(day_text.starts_with(' ') ? day_text.substr(1) : day_text);
    if (!day) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, kAsctimeDayAt});
    const auto time = parse_time_of_day(text.substr(kAsctimeTimeAt, kTimeOfDayLength));
    if (!time) [[unlikely]]
        return std::unexpected(Refusal{kTimeOfDayProblem, kAsctimeTimeAt});
    const auto year = parse_digits(text.substr(kAsctimeYearAt, 4), 4);
    if (!year) [[unlikely]]
        return std::unexpected(Refusal{kDigitProblem, kAsctimeYearAt});
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(*year)},
                                           std::chrono::month{*month}, std::chrono::day{*day}};
    if (!date.ok()) [[unlikely]]
        return std::unexpected(Refusal{kAsctimeDateProblem, kAsctimeDayAt});
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

inline constexpr size_t kIpLiteralAt = 1;

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
        if (!is_ip_literal(uri_host.substr(kIpLiteralAt, uri_host.size() - 2))) [[unlikely]]
            return std::unexpected(Refusal{kHostProblem, kIpLiteralAt});
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
    if (method == kConnectNumber)
        return TargetForm::kAuthority;
    if (text == "*")
        return method == kOptionsNumber
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
    if (allowed_run_length(text, kQueryByte, kQueryByteLowBits) != text.size()) [[unlikely]]
        return std::unexpected(Refusal{kQueryProblem, 0});
    return text;
}

struct OriginForm {
    std::string_view path;
    std::string_view query;
};

inline std::expected<OriginForm, Refusal> parse_origin_form(const std::string_view text)
{
    if (!text.starts_with('/')) [[unlikely]]
        return std::unexpected(Refusal{kAbsolutePathProblem, 0});
    const size_t question = text.find('?');
    const std::string_view path = text.substr(0, question);
    if (allowed_run_length(path, kPathByte, kPathByteLowBits) != path.size()) [[unlikely]]
        return std::unexpected(Refusal{kAbsolutePathProblem, 0});
    if (question == std::string_view::npos)
        return OriginForm{path, {}};
    const auto query = parse_query(text.substr(question + 1));
    if (!query) [[unlikely]]
        return std::unexpected(moved_forward(query.error(), question + 1));
    return OriginForm{path, *query};
}

inline std::expected<Host, Refusal> parse_authority_form(const std::string_view text)
{
    const auto host = parse_host(text);
    if (!host) [[unlikely]]
        return host;
    if (!host->port) [[unlikely]]
        return std::unexpected(Refusal{kPortProblem, static_cast<uint32_t>(text.size())});
    return *host;
}

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

inline std::expected<std::string, Refusal> percent_decode(const std::string_view text)
{
    const size_t first = text.find('%');
    for (size_t at = first; at != std::string_view::npos; at = text.find('%', at + 3)) {
        if (text.size() - at < 3) [[unlikely]]
            return std::unexpected(Refusal{kPctEncodedProblem, static_cast<uint32_t>(at)});
        const char *const digits = std::next(text.data(), at + 1);
        unsigned char octet = 0;
        const auto done = std::from_chars(digits, std::next(digits, 2), octet, 16);
        if (done.ec != std::errc{} || done.ptr != std::next(digits, 2)) [[unlikely]]
            return std::unexpected(Refusal{kPctEncodedProblem, static_cast<uint32_t>(at)});
    }
    return ada::unicode::percent_decode(text, first);
}

struct MediaType {
    std::string_view type;
    std::string_view subtype;
    std::string_view parameters;
};

inline std::expected<MediaType, Refusal> parse_media_type(const std::string_view text)
{
    const std::string_view type(text.begin(), std::ranges::find_if_not(text, is_tchar));
    if (type.empty()) [[unlikely]]
        return std::unexpected(Refusal{kMediaTypeProblem, 0});
    const std::string_view after = text.substr(type.size());
    if (!after.starts_with('/')) [[unlikely]]
        return std::unexpected(Refusal{kMediaTypeProblem, static_cast<uint32_t>(type.size())});
    const std::string_view rest = after.substr(1);
    const std::string_view subtype(rest.begin(), std::ranges::find_if_not(rest, is_tchar));
    if (subtype.empty()) [[unlikely]]
        return std::unexpected(Refusal{kMediaTypeProblem, static_cast<uint32_t>(type.size() + 1)});
    return MediaType{type, subtype, rest.substr(subtype.size())};
}

inline std::expected<std::optional<std::string_view>, Refusal>
value_of_parameter(const std::string_view parameters, const std::string_view name)
{
    std::string_view rest = parameters;
    while (true) {
        const auto parameter = parse_field_value_parameter(rest);
        if (!parameter) [[unlikely]]
            return std::unexpected(
                moved_forward(parameter.error(), parameters.size() - rest.size()));
        if (!*parameter)
            return std::optional<std::string_view>{};
        if (equal_ignoring_case((*parameter)->name, name))
            return (*parameter)->value;
        rest = (*parameter)->rest;
    }
}

constexpr std::string_view unquoted_token(const std::string_view value)
{
    if (value.size() < 2 || !value.starts_with('"') || !value.ends_with('"'))
        return value;
    const std::string_view inside = value.substr(1, value.size() - 2);
    if (inside.empty() || !std::ranges::all_of(inside, is_tchar))
        return value;
    return inside;
}

enum class ContentCoding : uint8_t { kIdentity, kGzip, kCompress, kDeflate, kUnknown };

constexpr ContentCoding content_coding(const std::string_view token)
{
    if (equal_ignoring_case(token, "gzip") || equal_ignoring_case(token, "x-gzip"))
        return ContentCoding::kGzip;
    if (equal_ignoring_case(token, "compress") || equal_ignoring_case(token, "x-compress"))
        return ContentCoding::kCompress;
    if (equal_ignoring_case(token, "deflate"))
        return ContentCoding::kDeflate;
    if (equal_ignoring_case(token, "identity"))
        return ContentCoding::kIdentity;
    return ContentCoding::kUnknown;
}

inline bool is_language_tag(const std::string_view text)
{
    std::string_view rest = text;
    bool primary = true;
    while (!rest.empty()) {
        const size_t hyphen = rest.find('-');
        const std::string_view subtag = rest.substr(0, hyphen);
        if (subtag.empty() || subtag.size() > 8) [[unlikely]]
            return false;
        const bool spelled = primary ? std::ranges::all_of(subtag, is_alpha)
                                     : std::ranges::all_of(subtag, is_alphanum);
        if (!spelled) [[unlikely]]
            return false;
        if (hyphen == std::string_view::npos)
            return true;
        primary = false;
        rest = rest.substr(hyphen + 1);
    }
    return false;
}

inline std::expected<uint64_t, Refusal> parse_content_length(const std::string_view text)
{
    uint64_t length = 0;
    const char *const end = std::next(text.data(), text.size());
    const auto done = std::from_chars(text.data(), end, length);
    if (done.ec != std::errc{} || done.ptr != end) [[unlikely]]
        return std::unexpected(Refusal{kContentLengthProblem, 0});
    return length;
}

inline constexpr uint16_t kMostPreferred = 1000;

inline constexpr size_t kQvaluePointAt = 1;
inline constexpr size_t kQvalueDigitsAt = 2;
inline constexpr size_t kQvalueLength = 5;

inline std::expected<uint64_t, Refusal>
parse_content_length_list(const std::string_view combined)
{
    std::optional<uint64_t> agreed;
    std::string_view rest = combined;
    while (const auto element = parse_list_element(rest)) {
        const size_t at =
            static_cast<size_t>(std::distance(combined.data(), element->element.data()));
        const auto one = parse_content_length(element->element);
        if (!one) [[unlikely]]
            return std::unexpected(moved_forward(one.error(), at));
        if (agreed && *agreed != *one) [[unlikely]]
            return std::unexpected(Refusal{kContentLengthProblem, static_cast<uint32_t>(at)});
        agreed = *one;
        rest = element->rest;
    }
    if (!agreed) [[unlikely]]
        return std::unexpected(Refusal{kContentLengthProblem, 0});
    return *agreed;
}

inline std::expected<uint16_t, Refusal> parse_qvalue(const std::string_view text)
{
    if (text.empty() || (text.front() != '0' && text.front() != '1')) [[unlikely]]
        return std::unexpected(Refusal{kQvalueProblem, 0});
    const uint16_t whole = text.front() == '1' ? kMostPreferred : 0;
    if (text.size() == 1)
        return whole;
    if (text.at(kQvaluePointAt) != '.' || text.size() > kQvalueLength) [[unlikely]]
        return std::unexpected(Refusal{kQvalueProblem, kQvaluePointAt});
    uint16_t thousandths = 0;
    uint16_t place = 100;
    for (size_t at = kQvalueDigitsAt; at < text.size(); ++at) {
        if (!is_digit(text.at(at))) [[unlikely]]
            return std::unexpected(Refusal{kQvalueProblem, static_cast<uint32_t>(at)});
        thousandths = static_cast<uint16_t>(thousandths +
                                            static_cast<unsigned>(text.at(at) - '0') * place);
        place = static_cast<uint16_t>(place / 10);
    }
    if (whole == kMostPreferred && thousandths != 0) [[unlikely]]
        return std::unexpected(Refusal{kQvalueProblem, kQvalueDigitsAt});
    return static_cast<uint16_t>(whole + thousandths);
}

inline std::expected<uint16_t, Refusal> weight_of(const std::string_view parameters)
{
    const auto found = value_of_parameter(parameters, "q");
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (!*found)
        return kMostPreferred;
    return parse_qvalue(unquoted_token(**found));
}

inline constexpr unsigned kAnyMediaTypePrecedence = 1;
inline constexpr unsigned kAnySubtypePrecedence = 2;
inline constexpr unsigned kNamedMediaTypePrecedence = 3;

inline std::expected<std::optional<unsigned>, Refusal>
media_range_precedence(const MediaType range, const MediaType media_type)
{
    if (range.type == "*" && range.subtype != "*") [[unlikely]]
        return std::unexpected(Refusal{kMediaTypeProblem, 0});
    if (range.type != "*" && !equal_ignoring_case(range.type, media_type.type))
        return std::optional<unsigned>{};
    if (range.subtype != "*" && !equal_ignoring_case(range.subtype, media_type.subtype))
        return std::optional<unsigned>{};
    unsigned precedence = range.type == "*"      ? kAnyMediaTypePrecedence
                          : range.subtype == "*" ? kAnySubtypePrecedence
                                                 : kNamedMediaTypePrecedence;
    std::string_view rest = range.parameters;
    while (true) {
        const auto parameter = parse_field_value_parameter(rest);
        if (!parameter) [[unlikely]]
            return std::unexpected(
                moved_forward(parameter.error(), range.parameters.size() - rest.size()));
        if (!*parameter)
            return precedence;
        if (!equal_ignoring_case((*parameter)->name, "q")) {
            const auto provided = value_of_parameter(media_type.parameters, (*parameter)->name);
            if (!provided) [[unlikely]]
                return std::unexpected(
                    Refusal{kProvidedMediaTypeProblem, provided.error().offset});
            if (!*provided || !equal_ignoring_case(unquoted_token(**provided),
                                                   unquoted_token((*parameter)->value)))
                return std::optional<unsigned>{};
            ++precedence;
        }
        rest = (*parameter)->rest;
    }
}

inline std::expected<uint16_t, Refusal> media_type_weight(const std::string_view accept,
                                                          const MediaType media_type)
{
    unsigned most_specific = 0;
    uint16_t weight = 0;
    std::string_view rest = accept;
    while (const auto element = parse_list_element(rest)) {
        const size_t at =
            static_cast<size_t>(std::distance(accept.data(), element->element.data()));
        const auto range = parse_media_type(element->element);
        if (!range) [[unlikely]]
            return std::unexpected(moved_forward(range.error(), at));
        const auto precedence = media_range_precedence(*range, media_type);
        if (!precedence) [[unlikely]]
            return std::unexpected(precedence.error().problem == kProvidedMediaTypeProblem
                                       ? precedence.error()
                                       : moved_forward(precedence.error(), at));
        if (*precedence && **precedence > most_specific) {
            const auto found = weight_of(range->parameters);
            if (!found) [[unlikely]]
                return std::unexpected(moved_forward(found.error(), at));
            most_specific = **precedence;
            weight = *found;
        }
        rest = element->rest;
    }
    return weight;
}

inline constexpr unsigned kAnyCodingPrecedence = 1;
inline constexpr unsigned kNamedCodingPrecedence = 2;

inline std::string_view before_parameters(const std::string_view element)
{
    const std::string_view named = element.substr(0, element.find(';'));
    return named.substr(0, named.find_last_not_of(" \t") + 1);
}

inline std::string_view parameters_of(const std::string_view element)
{
    const size_t semicolon = element.find(';');
    return semicolon == std::string_view::npos ? std::string_view{} : element.substr(semicolon);
}

inline std::expected<uint16_t, Refusal> coding_weight(const std::string_view accept_encoding,
                                                      const std::string_view coding)
{
    unsigned most_specific = 0;
    uint16_t weight = 0;
    std::string_view rest = accept_encoding;
    while (const auto element = parse_list_element(rest)) {
        const size_t at =
            static_cast<size_t>(std::distance(accept_encoding.data(), element->element.data()));
        const std::string_view codings = before_parameters(element->element);
        if (codings != "*" && !is_token(codings)) [[unlikely]]
            return std::unexpected(Refusal{kTcharProblem, static_cast<uint32_t>(at)});
        const unsigned precedence = codings == "*" ? kAnyCodingPrecedence
                                    : equal_ignoring_case(codings, coding)
                                        ? kNamedCodingPrecedence
                                        : 0;
        if (precedence > most_specific) {
            const auto found = weight_of(parameters_of(element->element));
            if (!found) [[unlikely]]
                return std::unexpected(moved_forward(found.error(), at));
            most_specific = precedence;
            weight = *found;
        }
        rest = element->rest;
    }
    if (most_specific == 0 && equal_ignoring_case(coding, "identity"))
        return kMostPreferred;
    return weight;
}

inline bool is_language_range(const std::string_view text)
{
    return text == "*" || is_language_tag(text);
}

inline bool language_range_matches(const std::string_view range, const std::string_view tag)
{
    if (range == "*")
        return true;
    if (range.size() > tag.size())
        return false;
    if (range.size() < tag.size() && tag.at(range.size()) != '-')
        return false;
    return equal_ignoring_case(range, tag.substr(0, range.size()));
}

inline constexpr unsigned kAnyLanguagePrecedence = 1;

inline unsigned language_range_precedence(const std::string_view range)
{
    return range == "*"
               ? kAnyLanguagePrecedence
               : kAnyLanguagePrecedence + 1 +
                     static_cast<unsigned>(std::ranges::count(range, '-'));
}

inline std::expected<uint16_t, Refusal> language_weight(const std::string_view accept_language,
                                                        const std::string_view tag)
{
    unsigned most_specific = 0;
    uint16_t weight = 0;
    std::string_view rest = accept_language;
    while (const auto element = parse_list_element(rest)) {
        const size_t at =
            static_cast<size_t>(std::distance(accept_language.data(), element->element.data()));
        const std::string_view range = before_parameters(element->element);
        if (!is_language_range(range)) [[unlikely]]
            return std::unexpected(Refusal{kTcharProblem, static_cast<uint32_t>(at)});
        if (!language_range_matches(range, tag)) {
            rest = element->rest;
            continue;
        }
        const unsigned precedence = language_range_precedence(range);
        if (precedence > most_specific) {
            const auto found = weight_of(parameters_of(element->element));
            if (!found) [[unlikely]]
                return std::unexpected(moved_forward(found.error(), at));
            most_specific = precedence;
            weight = *found;
        }
        rest = element->rest;
    }
    return weight;
}

// RFC 9651 4.1.3.1: sf-token = ( ALPHA / "*" ) *( tchar / ":" / "/" ).
constexpr bool is_structured_token(const std::string_view text)
{
    if (text.empty() || !(is_alpha(text.front()) || text.front() == '*'))
        return false;
    return std::ranges::all_of(text, [](const char letter) {
        return is_tchar(letter) || letter == ':' || letter == '/';
    });
}

// RFC 9651: key = ( lcalpha / "*" ) *( lcalpha / DIGIT / "_" / "-" / "."
// / "*" ). A media type parameter name is a tchar run and case
// insensitive, so most of them lower into a key and a few - the ones
// holding ! # $ % & ' + ^ ` | ~ - cannot.
constexpr bool is_structured_key(const std::string_view text)
{
    const auto is_lowercase_alpha = [](const char letter) {
        return letter >= 'a' && letter <= 'z';
    };
    if (text.empty() || !(is_lowercase_alpha(text.front()) || text.front() == '*'))
        return false;
    return std::ranges::all_of(text, [&is_lowercase_alpha](const char letter) {
        return is_lowercase_alpha(letter) || is_digit(letter) || letter == '_' ||
               letter == '-' || letter == '.' || letter == '*';
    });
}

// RFC 9651: sf-string = DQUOTE *( unescaped / "%" / bs-escaped ) DQUOTE,
// unescaped = %x20-21 / %x23-24 / %x26-5B / %x5D-7E, and
// bs-escaped = "\" ( DQUOTE / "\" ). So only those two bytes are
// escaped, and nothing outside %x20-7E can be carried at all.
inline std::expected<std::string, Refusal> spell_structured_string(const std::string_view text)
{
    std::string spelled(1, '"');
    for (size_t at = 0; at < text.size(); at++) {
        const unsigned char byte = static_cast<unsigned char>(text.at(at));
        if (byte < 0x20 || byte > 0x7e) [[unlikely]]
            return std::unexpected(
                Refusal{kStructuredItemProblem, static_cast<uint32_t>(at)});
        if (byte == '"' || byte == '\\')
            spelled.push_back('\\');
        spelled.push_back(text.at(at));
    }
    spelled.push_back('"');
    return spelled;
}

// A token where the bytes allow one, a string otherwise. RFC 10008 3 says
// the choice carries no meaning: recipients "MAY convert Tokens to
// Strings, but MUST NOT process them differently based on the received
// type", so one rule, applied everywhere, is enough.
inline std::expected<std::string, Refusal> spell_structured_item(const std::string_view text)
{
    if (is_structured_token(text))
        return std::string(text);
    return spell_structured_string(text);
}

// RFC 10008 3: the Accept-Query response field "contains a list of media
// ranges ... using Structured Fields syntax", each "without parameters",
// and "media type parameters, if any, are mapped to Structured Field
// Parameters". It is built once, from what the resource declared, and
// sent as bytes from then on.
inline std::expected<std::string, Refusal>
spell_accept_query(const std::span<const MediaType> provided)
{
    std::string spelled;
    for (const MediaType &media : provided) {
        if (!spelled.empty())
            spelled.append(", ");
        std::string range(media.type);
        range.push_back('/');
        range.append(media.subtype);
        const auto item = spell_structured_item(range);
        if (!item) [[unlikely]]
            return std::unexpected(item.error());
        spelled.append(*item);
        std::string_view rest = media.parameters;
        while (true) {
            const auto parameter = parse_field_value_parameter(rest);
            if (!parameter) [[unlikely]]
                return std::unexpected(Refusal{kProvidedMediaTypeProblem,
                                               parameter.error().offset});
            if (!*parameter)
                break;
            const std::string key = ascii_lowered_copy((*parameter)->name);
            if (!is_structured_key(key)) [[unlikely]]
                return std::unexpected(Refusal{kStructuredItemProblem, 0});
            const auto value = spell_structured_item(unquoted_token((*parameter)->value));
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            spelled.push_back(';');
            spelled.append(key);
            spelled.push_back('=');
            spelled.append(*value);
            rest = (*parameter)->rest;
        }
    }
    return spelled;
}

struct Chosen {
    size_t at;
    uint16_t weight;
};

inline std::expected<std::optional<Chosen>, Refusal>
choose_media_type(const std::span<const MediaType> provided, const std::string_view accept)
{
    std::optional<Chosen> best;
    for (size_t at = 0; at < provided.size(); at++) {
        const auto weight = media_type_weight(accept, provided[at]);
        if (!weight) [[unlikely]]
            return std::unexpected(weight.error());
        if (*weight != 0 && (!best || *weight > best->weight))
            best = Chosen{at, *weight};
    }
    return best;
}

inline std::expected<std::optional<Chosen>, Refusal>
choose_coding(const std::span<const std::string_view> provided,
              const std::string_view accept_encoding)
{
    std::optional<Chosen> best;
    for (size_t at = 0; at < provided.size(); at++) {
        const auto weight = coding_weight(accept_encoding, provided[at]);
        if (!weight) [[unlikely]]
            return std::unexpected(weight.error());
        if (*weight != 0 && (!best || *weight > best->weight))
            best = Chosen{at, *weight};
    }
    return best;
}

inline std::expected<std::optional<Chosen>, Refusal>
choose_language(const std::span<const std::string_view> provided,
                const std::string_view accept_language)
{
    std::optional<Chosen> best;
    for (size_t at = 0; at < provided.size(); at++) {
        const auto weight = language_weight(accept_language, provided[at]);
        if (!weight) [[unlikely]]
            return std::unexpected(weight.error());
        if (*weight != 0 && (!best || *weight > best->weight))
            best = Chosen{at, *weight};
    }
    return best;
}

enum class SelectingField : uint8_t { kAccept, kAcceptEncoding, kAcceptLanguage };

constexpr std::string_view field_name_of(const SelectingField field)
{
    switch (field) {
    case SelectingField::kAccept:
        return "accept";
    case SelectingField::kAcceptEncoding:
        return "accept-encoding";
    case SelectingField::kAcceptLanguage:
        return "accept-language";
    }
    return {};
}

struct RangesSpecifier {
    std::string_view range_unit;
    std::string_view range_set;
};

inline std::expected<RangesSpecifier, Refusal> parse_ranges_specifier(const std::string_view text)
{
    const size_t is_same = text.find('=');
    if (is_same == std::string_view::npos) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, static_cast<uint32_t>(text.size())});
    const std::string_view range_unit = text.substr(0, is_same);
    if (!is_token(range_unit)) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, 0});
    const std::string_view range_set = text.substr(is_same + 1);
    if (range_set.empty()) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, static_cast<uint32_t>(is_same + 1)});
    return RangesSpecifier{range_unit, range_set};
}

struct IntRange {
    uint64_t first_pos;
    std::optional<uint64_t> last_pos;
};

struct SuffixRange {
    uint64_t suffix_length;
};

using ByteRangeSpec = std::variant<IntRange, SuffixRange>;

inline std::expected<uint64_t, Refusal> parse_range_number(const std::string_view text)
{
    uint64_t number = 0;
    const char *const end = std::next(text.data(), text.size());
    const auto done = std::from_chars(text.data(), end, number);
    if (done.ec != std::errc{} || done.ptr != end) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, 0});
    return number;
}

inline std::expected<ByteRangeSpec, Refusal> parse_byte_range_spec(const std::string_view text)
{
    if (text.starts_with('-')) {
        const auto suffix_length = parse_range_number(text.substr(1));
        if (!suffix_length) [[unlikely]]
            return std::unexpected(moved_forward(suffix_length.error(), 1));
        return SuffixRange{*suffix_length};
    }
    const size_t hyphen = text.find('-');
    if (hyphen == std::string_view::npos) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, static_cast<uint32_t>(text.size())});
    const auto first_pos = parse_range_number(text.substr(0, hyphen));
    if (!first_pos) [[unlikely]]
        return std::unexpected(first_pos.error());
    const std::string_view behind = text.substr(hyphen + 1);
    if (behind.empty())
        return IntRange{*first_pos, std::nullopt};
    const auto last_pos = parse_range_number(behind);
    if (!last_pos) [[unlikely]]
        return std::unexpected(moved_forward(last_pos.error(), hyphen + 1));
    if (*last_pos < *first_pos) [[unlikely]]
        return std::unexpected(Refusal{kRangeProblem, static_cast<uint32_t>(hyphen + 1)});
    return IntRange{*first_pos, *last_pos};
}

struct ResolvedRange {
    uint64_t first_pos;
    uint64_t last_pos;
};

inline std::optional<ResolvedRange> resolved_range(const ByteRangeSpec spec,
                                                   const uint64_t complete_length)
{
    if (complete_length == 0)
        return std::nullopt;
    if (const SuffixRange *const suffix = std::get_if<SuffixRange>(&spec)) {
        if (suffix->suffix_length == 0)
            return std::nullopt;
        return ResolvedRange{complete_length - std::min(suffix->suffix_length, complete_length),
                             complete_length - 1};
    }
    const IntRange &range = std::get<IntRange>(spec);
    if (range.first_pos >= complete_length)
        return std::nullopt;
    return ResolvedRange{range.first_pos,
                         std::min(range.last_pos.value_or(complete_length - 1),
                                  complete_length - 1)};
}

struct EntityTag {
    std::string_view opaque_tag;
    bool weak;
};

constexpr bool is_etagc(const char letter)
{
    return kEtagc.at(static_cast<unsigned char>(letter));
}

inline std::expected<EntityTag, Refusal> parse_entity_tag(const std::string_view text)
{
    const size_t begins = text.starts_with("W/") ? 2 : 0;
    const std::string_view quoted = text.substr(begins);
    if (quoted.size() < 2 || !quoted.starts_with('"') || !quoted.ends_with('"')) [[unlikely]]
        return std::unexpected(Refusal{kEntityTagProblem, static_cast<uint32_t>(begins)});
    const std::string_view opaque_tag = quoted.substr(1, quoted.size() - 2);
    const auto refused = std::ranges::find_if_not(opaque_tag, is_etagc);
    if (refused != opaque_tag.end()) [[unlikely]]
        return std::unexpected(Refusal{
            kEntityTagProblem,
            static_cast<uint32_t>(begins + 1 + std::distance(opaque_tag.begin(), refused))});
    return EntityTag{opaque_tag, begins == 2};
}

constexpr bool strong_comparison(const EntityTag left, const EntityTag right)
{
    return !left.weak && !right.weak && left.opaque_tag == right.opaque_tag;
}

constexpr bool weak_comparison(const EntityTag left, const EntityTag right)
{
    return left.opaque_tag == right.opaque_tag;
}

inline std::expected<bool, Refusal> if_match_passes(const std::string_view field,
                                                    const bool representation_exists,
                                                    const std::optional<EntityTag> selected)
{
    if (field == "*")
        return representation_exists;
    std::string_view rest = field;
    while (const auto element = parse_list_element(rest)) {
        const auto tag = parse_entity_tag(element->element);
        if (!tag) [[unlikely]]
            return std::unexpected(moved_forward(
                tag.error(),
                static_cast<size_t>(std::distance(field.data(), element->element.data()))));
        if (selected && strong_comparison(*tag, *selected))
            return true;
        rest = element->rest;
    }
    return false;
}

inline std::expected<bool, Refusal> if_none_match_passes(const std::string_view field,
                                                         const bool representation_exists,
                                                         const std::optional<EntityTag> selected)
{
    if (field == "*")
        return !representation_exists;
    std::string_view rest = field;
    while (const auto element = parse_list_element(rest)) {
        const auto tag = parse_entity_tag(element->element);
        if (!tag) [[unlikely]]
            return std::unexpected(moved_forward(
                tag.error(),
                static_cast<size_t>(std::distance(field.data(), element->element.data()))));
        if (selected && weak_comparison(*tag, *selected))
            return false;
        rest = element->rest;
    }
    return true;
}

constexpr bool
if_modified_since_passes(const std::chrono::sys_seconds since,
                         const std::optional<std::chrono::sys_seconds> last_modified)
{
    return !last_modified || *last_modified > since;
}

constexpr bool
if_unmodified_since_passes(const std::chrono::sys_seconds since,
                           const std::optional<std::chrono::sys_seconds> last_modified)
{
    return last_modified.has_value() && *last_modified <= since;
}

inline constexpr size_t kIfRangeQuoteWithin = 3;

inline std::expected<bool, Refusal>
if_range_passes(const std::string_view field, const std::optional<EntityTag> selected,
                const std::optional<std::chrono::sys_seconds> last_modified,
                const std::chrono::year current_year)
{
    if (field.substr(0, kIfRangeQuoteWithin).find('"') != std::string_view::npos) {
        const auto tag = parse_entity_tag(field);
        if (!tag) [[unlikely]]
            return std::unexpected(tag.error());
        return selected.has_value() && strong_comparison(*tag, *selected);
    }
    const auto date = parse_http_date(field, current_year);
    if (!date) [[unlikely]]
        return std::unexpected(date.error());
    return last_modified.has_value() && *last_modified == *date;
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
