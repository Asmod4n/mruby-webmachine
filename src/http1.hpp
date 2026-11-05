#pragma once

#include <cstddef>
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <bit>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
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
inline constexpr uint16_t kTransferEncodingProblem = 32;
inline constexpr uint16_t kLengthRequired = 33;
inline constexpr uint16_t kContentTooLarge = 34;

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
    uint64_t content_length;
    uint32_t content_length_field_lines;
    std::string_view transfer_encoding;
    uint32_t transfer_encoding_field_lines;
    bool close;
    bool keep_alive;
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



constexpr std::string_view without_ows(const std::string_view text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return text.substr(text.size());
    return text.substr(first, text.find_last_not_of(" \t") + 1 - first);
}

constexpr std::expected<FieldLine, http::Refusal> parse_field_line(const std::string_view line)
{
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) [[unlikely]]
        return std::unexpected(http::Refusal{kFieldLineProblem, 0});
    const std::string_view field_name = line.substr(0, colon);
    if (!http::is_token(field_name)) [[unlikely]]
        return std::unexpected(http::Refusal{kFieldLineProblem, 0});
    return FieldLine{field_name, without_ows(line.substr(colon + 1))};
}

inline constexpr unsigned char kLastControl = 0x1F;
inline constexpr unsigned char kDelete = 0x7F;

constexpr bool is_refused_in_field_lines(const char letter)
{
    const unsigned char byte = static_cast<unsigned char>(letter);
    return (byte <= kLastControl && byte != '\t' && byte != '\r' && byte != '\n') || byte == kDelete;
}

inline bool floor_field_lines_are_valid(const std::string_view lines)
{
    const uint8_t refused =
        std::transform_reduce(lines.begin(), lines.end(), uint8_t{0}, std::bit_or<uint8_t>{},
                              [](const char letter) { return static_cast<uint8_t>(is_refused_in_field_lines(letter)); });
    return refused == 0 && std::ranges::count(lines, '\r') == std::ranges::count(lines, '\n');
}

#if defined(__AVX2__)
inline bool avx2_field_lines_are_valid(const std::string_view lines)
{
    constexpr size_t kBlock = 32;
    const __m256i last_control = _mm256_set1_epi8(static_cast<char>(kLastControl));
    const __m256i htab = _mm256_set1_epi8('\t');
    const __m256i cr = _mm256_set1_epi8('\r');
    const __m256i lf = _mm256_set1_epi8('\n');
    const __m256i del = _mm256_set1_epi8(static_cast<char>(kDelete));
    uint32_t refused = 0;
    int crs = 0;
    int lfs = 0;
    for (size_t at = 0; at < lines.size(); at += kBlock) {
        const size_t left = lines.size() - at;
        const uint32_t inside = left >= kBlock ? ~uint32_t{0} : (uint32_t{1} << left) - 1;
        const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(std::next(lines.data(), static_cast<std::ptrdiff_t>(at))));
        const __m256i control = _mm256_cmpeq_epi8(_mm256_min_epu8(bytes, last_control), bytes);
        const __m256i is_cr = _mm256_cmpeq_epi8(bytes, cr);
        const __m256i is_lf = _mm256_cmpeq_epi8(bytes, lf);
        const __m256i allowed = _mm256_or_si256(_mm256_cmpeq_epi8(bytes, htab), _mm256_or_si256(is_cr, is_lf));
        const __m256i no = _mm256_or_si256(_mm256_andnot_si256(allowed, control), _mm256_cmpeq_epi8(bytes, del));
        refused |= static_cast<uint32_t>(_mm256_movemask_epi8(no)) & inside;
        crs += std::popcount(static_cast<uint32_t>(_mm256_movemask_epi8(is_cr)) & inside);
        lfs += std::popcount(static_cast<uint32_t>(_mm256_movemask_epi8(is_lf)) & inside);
    }
    return refused == 0 && crs == lfs;
}
#endif

#if defined(__AVX512BW__)
inline bool avx512_field_lines_are_valid(const std::string_view lines)
{
    constexpr size_t kBlock = 64;
    const __m512i last_control = _mm512_set1_epi8(static_cast<char>(kLastControl));
    const __m512i htab = _mm512_set1_epi8('\t');
    const __m512i cr = _mm512_set1_epi8('\r');
    const __m512i lf = _mm512_set1_epi8('\n');
    const __m512i del = _mm512_set1_epi8(static_cast<char>(kDelete));
    uint64_t refused = 0;
    int crs = 0;
    int lfs = 0;
    for (size_t at = 0; at < lines.size(); at += kBlock) {
        const size_t left = lines.size() - at;
        const __mmask64 inside = left >= kBlock ? ~__mmask64{0} : (__mmask64{1} << left) - 1;
        const __m512i bytes = _mm512_loadu_si512(std::next(lines.data(), static_cast<std::ptrdiff_t>(at)));
        const __mmask64 control = _mm512_mask_cmple_epu8_mask(inside, bytes, last_control);
        const __mmask64 is_cr = _mm512_mask_cmpeq_epi8_mask(inside, bytes, cr);
        const __mmask64 is_lf = _mm512_mask_cmpeq_epi8_mask(inside, bytes, lf);
        const __mmask64 is_htab = _mm512_mask_cmpeq_epi8_mask(inside, bytes, htab);
        const __mmask64 is_del = _mm512_mask_cmpeq_epi8_mask(inside, bytes, del);
        refused |= (control & ~(is_htab | is_cr | is_lf)) | is_del;
        crs += std::popcount(is_cr);
        lfs += std::popcount(is_lf);
    }
    return refused == 0 && crs == lfs;
}
#endif

#if defined(__ARM_NEON) && defined(__aarch64__)
inline bool neon_field_lines_are_valid(const std::string_view lines)
{
    constexpr size_t kBlock = 16;
    constexpr std::array<uint8_t, kBlock> kLanes = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    constexpr int kLowestBit = 7;
    const uint8x16_t lane = vld1q_u8(kLanes.data());
    const uint8x16_t last_control = vdupq_n_u8(kLastControl);
    const uint8x16_t htab = vdupq_n_u8('\t');
    const uint8x16_t cr = vdupq_n_u8('\r');
    const uint8x16_t lf = vdupq_n_u8('\n');
    const uint8x16_t del = vdupq_n_u8(kDelete);
    uint8x16_t refused = vdupq_n_u8(0);
    uint32_t crs = 0;
    uint32_t lfs = 0;
    for (size_t at = 0; at < lines.size(); at += kBlock) {
        const uint8x16_t inside = vcltq_u8(lane, vdupq_n_u8(static_cast<uint8_t>(std::min(lines.size() - at, kBlock))));
        const uint8x16_t bytes =
            vld1q_u8(reinterpret_cast<const uint8_t *>(std::next(lines.data(), static_cast<std::ptrdiff_t>(at))));
        const uint8x16_t is_cr = vceqq_u8(bytes, cr);
        const uint8x16_t is_lf = vceqq_u8(bytes, lf);
        const uint8x16_t allowed = vorrq_u8(vceqq_u8(bytes, htab), vorrq_u8(is_cr, is_lf));
        const uint8x16_t no = vorrq_u8(vbicq_u8(vcleq_u8(bytes, last_control), allowed), vceqq_u8(bytes, del));
        refused = vorrq_u8(refused, vandq_u8(no, inside));
        crs += vaddvq_u8(vshrq_n_u8(vandq_u8(is_cr, inside), kLowestBit));
        lfs += vaddvq_u8(vshrq_n_u8(vandq_u8(is_lf, inside), kLowestBit));
    }
    return vmaxvq_u8(refused) == 0 && crs == lfs;
}
#endif

inline bool field_lines_are_valid(const std::string_view lines)
{
#if defined(__AVX512BW__)
    return avx512_field_lines_are_valid(lines);
#elif defined(__AVX2__)
    return avx2_field_lines_are_valid(lines);
#elif defined(__ARM_NEON) && defined(__aarch64__)
    return neon_field_lines_are_valid(lines);
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
    if (first == 0 || asked[first - 1] != '\r') [[unlikely]]
        return std::unexpected(http::Refusal{kStartLineProblem, static_cast<uint32_t>(first)});
    if (first - 1 > kMostRequestLineBytes) [[unlikely]]
        return std::unexpected(http::Refusal{kUriTooLong, 0});
    const std::expected<RequestLine, http::Refusal> line = parse_request_line(asked.substr(0, first - 1));
    if (!line) [[unlikely]]
        return std::unexpected(line.error());
    const size_t fields_from = first + 1;
    size_t at = fields_from;
    std::string_view host;
    uint32_t host_field_lines = 0;
    uint64_t content_length = 0;
    uint32_t content_length_field_lines = 0;
    std::string_view transfer_encoding;
    uint32_t transfer_encoding_field_lines = 0;
    bool close = false;
    bool keep_alive = false;
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
            return Request{*line,          field_lines,
                           ends + 1,       host,
                           host_field_lines, content_length,
                           content_length_field_lines, transfer_encoding,
                           transfer_encoding_field_lines, close,
                           keep_alive};
        }
        if (ends + 1 - fields_from > kMostHeaderSectionBytes) [[unlikely]]
            return std::unexpected(http::Refusal{kRequestHeaderFieldsTooLarge, 0});
        if (asked[at] == ' ' || asked[at] == '\t') [[unlikely]]
            return std::unexpected(http::Refusal{kFieldLineProblem, static_cast<uint32_t>(at)});
        const std::expected<FieldLine, http::Refusal> field = parse_field_line(asked.substr(at, ends - 1 - at));
        if (!field) [[unlikely]]
            return std::unexpected(http::moved_forward(field.error(), at));
        if (http::equal_ignoring_case(field->field_name, "Host")) {
            host = field->field_value;
            host_field_lines++;
        } else if (http::equal_ignoring_case(field->field_name, "Content-Length")) {
            const std::expected<uint64_t, http::Refusal> length = http::parse_content_length_list(field->field_value);
            if (!length) [[unlikely]]
                return std::unexpected(http::moved_forward(length.error(), at));
            if (content_length_field_lines > 0 && *length != content_length) [[unlikely]]
                return std::unexpected(http::Refusal{http::kContentLengthProblem, static_cast<uint32_t>(at)});
            content_length = *length;
            content_length_field_lines++;
        } else if (http::equal_ignoring_case(field->field_name, "Connection")) {
            std::string_view options = field->field_value;
            while (const std::optional<http::ListElement> option = http::parse_list_element(options)) {
                close = close || http::equal_ignoring_case(option->element, "close");
                keep_alive = keep_alive || http::equal_ignoring_case(option->element, "keep-alive");
                options = option->rest;
            }
        } else if (http::equal_ignoring_case(field->field_name, "Transfer-Encoding")) {
            transfer_encoding = field->field_value;
            transfer_encoding_field_lines++;
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

constexpr bool is_persistent(const Request &request)
{
    if (request.close)
        return false;
    if (request.request_line.http_version == "HTTP/1.0")
        return request.keep_alive;
    return true;
}

constexpr std::optional<uint64_t> content_length_of(const Request &request)
{
    if (request.content_length_field_lines == 0)
        return std::nullopt;
    return request.content_length;
}

constexpr std::string_view final_transfer_coding_of(const std::string_view transfer_encoding)
{
    const size_t comma = transfer_encoding.rfind(',');
    const std::string_view coding =
        comma == std::string_view::npos ? transfer_encoding : transfer_encoding.substr(comma + 1);
    return without_ows(coding.substr(0, coding.find(';')));
}

constexpr std::expected<uint64_t, http::Refusal> parse_message_body_length(const Request &request,
                                                                          const uint64_t most_content_bytes)
{
    if (request.transfer_encoding_field_lines > 0) [[unlikely]] {
        if (request.request_line.http_version == "HTTP/1.0")
            return std::unexpected(http::Refusal{kTransferEncodingProblem, 0});
        if (http::equal_ignoring_case(final_transfer_coding_of(request.transfer_encoding), "chunked"))
            return std::unexpected(http::Refusal{kLengthRequired, 0});
        return std::unexpected(http::Refusal{kTransferEncodingProblem, 0});
    }
    if (request.content_length > most_content_bytes) [[unlikely]]
        return std::unexpected(http::Refusal{kContentTooLarge, 0});
    return request.content_length;
}

} // namespace http1
