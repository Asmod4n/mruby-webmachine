#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <string_view>
#include <vector>

#include "http.hpp"

namespace
{

// The inputs change from call to call, so the compiler cannot compute
// the answer once and lift it out of the loop.
const std::string_view kTimestamps[16] = {
    "Sun, 06 Nov 1994 08:49:37 GMT", "Mon, 07 Dec 2020 23:59:59 GMT",
    "Tue, 01 Jan 1980 00:00:00 GMT", "Wed, 29 Feb 2020 12:30:01 GMT",
    "Thu, 15 Mar 2001 06:06:06 GMT", "Fri, 31 Aug 1999 17:45:00 GMT",
    "Sat, 22 Jun 2011 09:09:09 GMT", "Sun, 05 May 2030 21:00:59 GMT",
    "Mon, 11 Apr 1975 03:14:15 GMT", "Tue, 19 Jan 2038 03:14:07 GMT",
    "Wed, 02 Jul 2024 13:37:00 GMT", "Thu, 25 Oct 1990 08:00:00 GMT",
    "Fri, 14 Sep 2007 19:20:21 GMT", "Sat, 30 Nov 2013 01:02:03 GMT",
    "Sun, 08 Feb 1998 22:22:22 GMT", "Mon, 27 Dec 2049 16:05:44 GMT"};

const std::string_view kFieldNames[16] = {
    "host", "user-agent", "accept", "accept-encoding", "connection", "content-type",
    "content-length", "if-none-match", "if-modified-since", "authorization", "cookie",
    "referer", "x-forwarded-for", "cache-control", "sec-fetch-mode", "origin"};

const std::string_view kQuoted[4] = {"\"utf-8\"", "\"a b c\"", "\"x\\\"y\"", "\"\""};

const std::string_view kParameters[4] = {";charset=utf-8", ";q=0.8", ";charset=\"utf-8\"",
                                         "; level = 1"};

void is_tchar(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        const std::string_view name = kFieldNames[at++ & 15];
        bool all = true;
        for (const char letter : name)
            all = all && http::is_tchar(letter);
        benchmark::DoNotOptimize(all);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// The field names of a real request, as views into the buffer that
// carries it, so the wide load has the rest of the request behind it.
const std::string kChrome =
    std::string(
    "GET /index.html HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "Connection: keep-alive\r\n"
    "sec-ch-ua: \"Chromium\";v=\"131\", \"Not_A Brand\";v=\"24\"\r\n"
    "sec-ch-ua-mobile: ?0\r\n"
    "sec-ch-ua-platform: \"Linux\"\r\n"
    "Upgrade-Insecure-Requests: 1\r\n"
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36\r\n"
    "Accept: text/html,application/xhtml+xml;q=0.9\r\n"
    "Sec-Fetch-Site: none\r\n"
    "Sec-Fetch-Mode: navigate\r\n"
    "Accept-Encoding: gzip, deflate, br, zstd\r\n"
    "Accept-Language: en-US,en;q=0.9\r\n"
    "\r\n")
    // The ring gives a wide read kWidePadding bytes behind any byte of the
    // pool. A std::string gives none, and the last field name here is close
    // enough to the end for a 32 byte load to pass it.
    + std::string(http::kWidePadding, '\0');

std::vector<std::string_view> field_names_of(const std::string &request)
{
    std::vector<std::string_view> names;
    const std::string_view whole(request);
    size_t at = whole.find("\r\n") + 2;
    while (at + 1 < whole.size() && whole.substr(at, 2) != "\r\n") {
        names.push_back(whole.substr(at, whole.find(':', at) - at));
        at = whole.find("\r\n", at) + 2;
    }
    return names;
}

const std::vector<std::string_view> kChromeFieldNames = field_names_of(kChrome);

void is_token_over_field_names(benchmark::State &state)
{
    for (auto _ : state) {
        bool all = true;
        for (const std::string_view name : kChromeFieldNames)
            all = all && http::is_token(name);
        benchmark::DoNotOptimize(all);
    }
}

void every_byte_over_field_names(benchmark::State &state)
{
    for (auto _ : state) {
        bool all = true;
        for (const std::string_view name : kChromeFieldNames)
            all = all && http::every_byte_is_allowed(name, http::kTchar);
        benchmark::DoNotOptimize(all);
    }
}

const std::string_view kHostOfChrome =
    std::string_view(kChrome).substr(kChrome.find("Host: ") + 6,
                                     kChrome.find("\r\n", kChrome.find("Host: ")) -
                                         kChrome.find("Host: ") - 6);

void is_reg_name_over_a_host(benchmark::State &state)
{
    for (auto _ : state) {
        bool good = http::is_reg_name(kHostOfChrome);
        benchmark::DoNotOptimize(good);
    }
}

void every_byte_over_a_host(benchmark::State &state)
{
    for (auto _ : state) {
        bool good = http::every_byte_is_allowed(kHostOfChrome, http::kRegName);
        benchmark::DoNotOptimize(good);
    }
}

void parse_host_of_chrome(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = http::parse_host(kHostOfChrome);
        benchmark::DoNotOptimize(got);
    }
}

void parse_quoted_string(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_quoted_string(kQuoted[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_field_value_parameter(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_field_value_parameter(kParameters[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_imf_fixdate(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_imf_fixdate(kTimestamps[at++ & 15]);
        benchmark::DoNotOptimize(got);
    }
}

void parse_http_date(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_http_date(kTimestamps[at++ & 15], std::chrono::year{2026});
        benchmark::DoNotOptimize(got);
    }
}

// The archive's media type reader, from src/resource.cpp of
// webmachine-archive, copied so that the old way and the new way run in
// one binary. It checks no grammar: the base is everything before the
// first ';', trimmed, and a parameter is split at ';' and at '=' with no
// regard for a quoted-string.
std::string_view archive_trim_optional_space(std::string_view text)
{
    size_t index = 0;
    size_t text_end = text.size();
    while (index < text_end && (text[index] == ' ' || text[index] == '\t'))
        index++;
    while (text_end > index && (text[text_end - 1] == ' ' || text[text_end - 1] == '\t'))
        text_end--;
    return text.substr(index, text_end - index);
}

bool archive_is_same_ignoring_case(std::string_view answer, std::string_view bound)
{
    if (answer.size() != bound.size())
        return false;
    for (size_t i = 0; i < answer.size(); i++) {
        char one = answer[i];
        char other = bound[i];
        if (one >= 'A' && one <= 'Z')
            one = static_cast<char>(one + 32);
        if (other >= 'A' && other <= 'Z')
            other = static_cast<char>(other + 32);
        if (one != other)
            return false;
    }
    return true;
}

std::string_view archive_media_type_base(std::string_view value)
{
    return archive_trim_optional_space(value.substr(0, value.find(';')));
}

std::string_view archive_media_type_params(std::string_view value)
{
    const size_t semi = value.find(';');
    return semi == std::string_view::npos ? std::string_view{} : value.substr(semi + 1);
}

struct ArchiveParam {
    std::string_view name;
    std::string_view value;
    std::string_view rest;
};

ArchiveParam archive_param_take_next(std::string_view list)
{
    const size_t semi = list.find(';');
    const std::string_view entry = list.substr(0, semi);
    const std::string_view rest =
        semi == std::string_view::npos ? std::string_view{} : list.substr(semi + 1);
    const size_t is_same = entry.find('=');
    if (is_same == std::string_view::npos)
        return {archive_trim_optional_space(entry), {}, rest};
    return {archive_trim_optional_space(entry.substr(0, is_same)),
            archive_trim_optional_space(entry.substr(is_same + 1)), rest};
}

std::string_view archive_param_find_named(std::string_view value, std::string_view name)
{
    std::string_view list = archive_media_type_params(value);
    while (!list.empty()) {
        const ArchiveParam next = archive_param_take_next(list);
        list = next.rest;
        if (archive_is_same_ignoring_case(next.name, name))
            return next.value;
    }
    return {};
}

// A wide read goes past the run it is given, so the inputs are held with
// the padding the ring's guard buffer gives a real one.
std::string held_with_padding(const std::string_view bytes)
{
    std::string held(bytes);
    held.append(http::kWidePadding, '\0');
    return held;
}

const std::string kHeldTypes[4] = {
    held_with_padding("text/html"), held_with_padding("text/html;charset=utf-8"),
    held_with_padding("application/vnd.api+json;charset=utf-8"),
    held_with_padding("multipart/form-data; boundary=----WebKitFormBoundaryABC123")};

const std::string_view kContentTypes[4] = {
    std::string_view(kHeldTypes[0]).substr(0, kHeldTypes[0].size() - http::kWidePadding),
    std::string_view(kHeldTypes[1]).substr(0, kHeldTypes[1].size() - http::kWidePadding),
    std::string_view(kHeldTypes[2]).substr(0, kHeldTypes[2].size() - http::kWidePadding),
    std::string_view(kHeldTypes[3]).substr(0, kHeldTypes[3].size() - http::kWidePadding)};

const std::string kHeldQuoted = held_with_padding("text/html;charset=\"utf-8\"");
const std::string_view kQuotedCharset =
    std::string_view(kHeldQuoted).substr(0, kHeldQuoted.size() - http::kWidePadding);

// Two arms that answer differently measure nothing, so this runs before
// every row that claims to compare them.
void check_the_arms_agree()
{
    for (const std::string_view text : kContentTypes) {
        const std::string_view base = archive_media_type_base(text);
        const auto media = http::parse_media_type(text);
        if (!media || base.data() != media->type.data() ||
            base.size() != media->type.size() + 1 + media->subtype.size())
            std::abort();
        const std::string_view old_charset = archive_param_find_named(text, "charset");
        const auto found = http::value_of_parameter(media->parameters, "charset");
        if (!found)
            std::abort();
        const std::string_view new_charset =
            *found ? http::unquoted_token(**found) : std::string_view{};
        if (old_charset != new_charset)
            std::abort();
    }
}

void media_type_archive(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        auto got = archive_media_type_base(kContentTypes[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void media_type_new(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::parse_media_type(kContentTypes[at++ & 3]);
        benchmark::DoNotOptimize(got);
    }
}

void charset_archive(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        auto got = archive_param_find_named(kContentTypes[at++ & 3], "charset");
        benchmark::DoNotOptimize(got);
    }
}

void charset_new(benchmark::State &state)
{
    check_the_arms_agree();
    size_t at = 0;
    for (auto _ : state) {
        const std::string_view text = kContentTypes[at++ & 3];
        const auto media = http::parse_media_type(text);
        auto got = http::value_of_parameter(media->parameters, "charset");
        benchmark::DoNotOptimize(got);
    }
}


// The scale a real request has: this block is what Chrome sends, and the
// arms below read it the way the server would. One arm is the readers
// this tree has; the other is a single pass that writes down where the
// structural bytes and the tchars are, which is the budget a reader
// built on masks would have to fit into.
struct ChromeField {
    std::string_view name;
    std::string_view value;
};

std::vector<ChromeField> fields_of(const std::string &request)
{
    std::vector<ChromeField> fields;
    const std::string_view whole(request);
    size_t at = whole.find("\r\n") + 2;
    while (at + 1 < whole.size() && whole.substr(at, 2) != "\r\n") {
        const size_t colon = whole.find(':', at);
        const size_t line_end = whole.find("\r\n", at);
        size_t value_from = colon + 1;
        while (value_from < line_end && (whole[value_from] == ' ' || whole[value_from] == '\t'))
            value_from++;
        fields.push_back({whole.substr(at, colon - at), whole.substr(value_from, line_end - value_from)});
        at = line_end + 2;
    }
    return fields;
}

const std::vector<ChromeField> kChromeFields = fields_of(kChrome);

const std::string_view kChromeBlock = [] {
    const std::string_view whole(kChrome);
    const size_t from = whole.find("\r\n") + 2;
    return whole.substr(from, whole.find("\r\n\r\n") + 2 - from);
}();

size_t read_chrome_with_todays_readers()
{
    size_t answered = 0;
    for (const ChromeField field : kChromeFields) {
        answered += http::is_token(field.name);
        if (http::equal_ignoring_case(field.name, "host")) {
            answered += http::parse_host(field.value).has_value();
        } else if (http::equal_ignoring_case(field.name, "accept")) {
            std::string_view rest = field.value;
            while (const auto element = http::parse_list_element(rest)) {
                const auto media = http::parse_media_type(element->element);
                if (media)
                    answered += http::value_of_parameter(media->parameters, "q").has_value();
                rest = element->rest;
            }
        } else if (http::equal_ignoring_case(field.name, "accept-encoding")) {
            std::string_view rest = field.value;
            while (const auto element = http::parse_list_element(rest)) {
                answered += http::content_coding(element->element) != http::ContentCoding::kUnknown;
                rest = element->rest;
            }
        } else if (http::equal_ignoring_case(field.name, "accept-language")) {
            std::string_view rest = field.value;
            while (const auto element = http::parse_list_element(rest)) {
                const std::string_view tag = element->element.substr(0, element->element.find(';'));
                answered += http::is_language_tag(tag);
                rest = element->rest;
            }
        } else if (http::equal_ignoring_case(field.name, "connection")) {
            std::string_view rest = field.value;
            while (const auto element = http::parse_list_element(rest)) {
                answered += http::is_token(element->element);
                rest = element->rest;
            }
        }
    }
    return answered;
}

// The upper bound nobody pays: every field of the request read. The
// decision graph does not work that way - it walks on facts, and a fact
// is "is there an If-None-Match", not what stands in it. A value is read
// where a node needs it, and a Ruby object is made where a resource asks
// for it. The three arms under this one are what a request really costs.
void chrome_read_every_field(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = read_chrome_with_todays_readers();
        benchmark::DoNotOptimize(got);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kChromeBlock.size()));
}


// A plain GET of a static file: the graph reaches O18 with every
// conditional and conneg fact false, so the only field value anybody
// reads is the Host that routed it.
void chrome_plain_get(benchmark::State &state)
{
    const std::string_view host = kChromeFields[0].value;
    for (auto _ : state) {
        std::string_view text = host;
        benchmark::DoNotOptimize(text);
        auto got = http::parse_host(text);
        benchmark::DoNotOptimize(got);
    }
}

// What a browser sends on the second visit: the same GET with the two
// validators. Now three values are read, and not one more.
const std::string kRevalidate =
    std::string("If-None-Match: \"686897696a7c876b7e\", W/\"xyzzy\"\r\n"
                "If-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n") +
    std::string(http::kWidePadding, '\0');

const std::vector<ChromeField> kRevalidateFields = fields_of(
    std::string("GET / HTTP/1.1\r\n") + kRevalidate);

void chrome_conditional_get(benchmark::State &state)
{
    const std::string_view host = kChromeFields[0].value;
    for (auto _ : state) {
        std::string_view text = host;
        benchmark::DoNotOptimize(text);
        size_t answered = http::parse_host(text).has_value();
        std::string_view rest = kRevalidateFields[0].value;
        while (const auto element = http::parse_list_element(rest)) {
            answered += http::parse_entity_tag(element->element).has_value();
            rest = element->rest;
        }
        answered += http::parse_http_date(kRevalidateFields[1].value, std::chrono::year{2026})
                        .has_value();
        benchmark::DoNotOptimize(answered);
    }
}

// A resource that offers more than one media type: the graph asks C4,
// and only then is Accept read.
void chrome_negotiated_get(benchmark::State &state)
{
    const std::string_view host = kChromeFields[0].value;
    const std::string_view accept = kChromeFields[7].value;
    for (auto _ : state) {
        std::string_view text = host;
        benchmark::DoNotOptimize(text);
        size_t answered = http::parse_host(text).has_value();
        std::string_view rest = accept;
        benchmark::DoNotOptimize(rest);
        while (const auto element = http::parse_list_element(rest)) {
            const auto media = http::parse_media_type(element->element);
            if (media)
                answered += http::value_of_parameter(media->parameters, "q").has_value();
            rest = element->rest;
        }
        benchmark::DoNotOptimize(answered);
    }
}

#if defined(__AVX2__)
inline constexpr std::array<bool, 256> kStructural = [] {
    std::array<bool, 256> table{};
    for (const char letter : std::string_view("/;=,\" :"))
        table.at(static_cast<unsigned char>(letter)) = true;
    return table;
}();

inline constexpr auto kStructuralLowBits = http::ascii_low_nibble_bits_of(kStructural);

void chrome_one_pass(benchmark::State &state)
{
    const __m256i high_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(http::kHighNibbleBit.data())));
    const __m256i structural_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(kStructuralLowBits.data())));
    const __m256i tchar_table = _mm256_broadcastsi128_si256(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(http::kTcharLowBits.data())));
    uint32_t structural[24];
    uint32_t tchar[24];
    for (auto _ : state) {
        size_t block = 0;
        for (size_t at = 0; at < kChromeBlock.size(); at += 32, block++) {
            const char *const from = std::next(kChromeBlock.data(), at);
            const size_t left = kChromeBlock.size() - at;
            const uint32_t inside = left >= 32 ? ~0u : (1u << left) - 1;
            structural[block] = ~http::avx2_block_refusals(from, structural_table, high_table) & inside;
            tchar[block] = ~http::avx2_block_refusals(from, tchar_table, high_table) & inside;
        }
        benchmark::DoNotOptimize(structural);
        benchmark::DoNotOptimize(tchar);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * kChromeBlock.size()));
}
#endif

// The same walk on both sides, over the parameters alone: this says how
// much of the difference above is the grammar check and how much is the
// walk itself.
const std::string_view kParameterLists[4] = {
    ";charset=utf-8", ";q=0.8;charset=utf-8", ";boundary=----WebKitFormBoundaryABC123",
    ";level=1;charset=utf-8;q=0.9"};

void parameter_walk_archive(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        std::string_view list = kParameterLists[at++ & 3].substr(1);
        std::string_view found;
        while (!list.empty()) {
            const ArchiveParam next = archive_param_take_next(list);
            list = next.rest;
            if (archive_is_same_ignoring_case(next.name, "charset")) {
                found = next.value;
                break;
            }
        }
        benchmark::DoNotOptimize(found);
    }
}

void parameter_walk_new(benchmark::State &state)
{
    size_t at = 0;
    for (auto _ : state) {
        auto got = http::value_of_parameter(kParameterLists[at++ & 3], "charset");
        benchmark::DoNotOptimize(got);
    }
}

// The quoted spelling is where the two stop answering the same thing:
// the archive hands back the quotes and RFC 9110 5.6.6 says the quoted
// and the unquoted value are the same. The rows are named for that.
void charset_quoted_archive_keeps_quotes(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = archive_param_find_named(kQuotedCharset, "charset");
        benchmark::DoNotOptimize(got);
    }
}

void charset_quoted_new_unquotes(benchmark::State &state)
{
    for (auto _ : state) {
        const auto media = http::parse_media_type(kQuotedCharset);
        const auto found = http::value_of_parameter(media->parameters, "charset");
        auto got = http::unquoted_token(**found);
        benchmark::DoNotOptimize(got);
    }
}

// libreactor serves a request the way this tree plans to: picohttpparser
// fills a flat array of fields, and every field the server wants is found
// by name afterwards. src/reactor/http.c:
//
//   data http_field_lookup(http_field *fields, size_t fields_count, data name)
//   {
//     for (i = 0; i < fields_count; i++)
//       if (data_size(fields[i].name) == data_size(name) &&
//           strncasecmp(data_base(fields[i].name), data_base(name),
//                       data_size(name)) == 0)
//         return fields[i].value;
//     return data_null();
//   }
//
// The shape is ours as well, so the arms below differ in one call:
// strncasecmp against http::equal_ignoring_case.
std::string_view libreactor_field_lookup(const std::vector<ChromeField> &fields,
                                         const std::string_view name)
{
    for (size_t at = 0; at < fields.size(); at++)
        if (fields[at].name.size() == name.size() &&
            strncasecmp(fields[at].name.data(), name.data(), name.size()) == 0)
            return fields[at].value;
    return std::string_view{};
}

std::string_view field_lookup(const std::vector<ChromeField> &fields,
                              const std::string_view name)
{
    for (size_t at = 0; at < fields.size(); at++)
        if (http::equal_ignoring_case(fields[at].name, name))
            return fields[at].value;
    return std::string_view{};
}

// First, last and absent, because a linear scan costs what the position
// of the answer costs. Host is field 1 of 13 and Accept-Language is 13.
void libreactor_field_lookup_first(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = libreactor_field_lookup(kChromeFields, "host");
        benchmark::DoNotOptimize(got);
    }
}

void field_lookup_first(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = field_lookup(kChromeFields, "host");
        benchmark::DoNotOptimize(got);
    }
}

void libreactor_field_lookup_last(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = libreactor_field_lookup(kChromeFields, "accept-language");
        benchmark::DoNotOptimize(got);
    }
}

void field_lookup_last(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = field_lookup(kChromeFields, "accept-language");
        benchmark::DoNotOptimize(got);
    }
}

void libreactor_field_lookup_absent(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = libreactor_field_lookup(kChromeFields, "if-none-match");
        benchmark::DoNotOptimize(got);
    }
}

void field_lookup_absent(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = field_lookup(kChromeFields, "if-none-match");
        benchmark::DoNotOptimize(got);
    }
}

// What one request really asks for: the four fields a negotiated GET
// needs. Four scans of the same array, which is what the flat array
// costs when more than one field is wanted.
void libreactor_four_lookups(benchmark::State &state)
{
    for (auto _ : state) {
        size_t seen = libreactor_field_lookup(kChromeFields, "host").size() +
                      libreactor_field_lookup(kChromeFields, "accept").size() +
                      libreactor_field_lookup(kChromeFields, "accept-encoding").size() +
                      libreactor_field_lookup(kChromeFields, "accept-language").size();
        benchmark::DoNotOptimize(seen);
    }
}

void four_lookups(benchmark::State &state)
{
    for (auto _ : state) {
        size_t seen = field_lookup(kChromeFields, "host").size() +
                      field_lookup(kChromeFields, "accept").size() +
                      field_lookup(kChromeFields, "accept-encoding").size() +
                      field_lookup(kChromeFields, "accept-language").size();
        benchmark::DoNotOptimize(seen);
    }
}

// Whether the gap is glibc's vector code or our byte loop. Eight bytes
// at a time, and only 'A' to 'Z' fold: 0x41 + 0x3f sets bit 7 and 0x5a +
// 0x25 does not, so the two carries name the range without a branch. It
// holds for tchar alone - a byte above 0x7f would carry into its
// neighbour - and a field name is tchar.
uint64_t ascii_lowered_word(const uint64_t word)
{
    const uint64_t high = 0x8080808080808080ull;
    const uint64_t at_least_a = word + 0x3f3f3f3f3f3f3f3full;
    const uint64_t at_most_z = word + 0x2525252525252525ull;
    return word | ((at_least_a & ~at_most_z & high) >> 2);
}

bool equal_ignoring_case_wide(const std::string_view left, const std::string_view right)
{
    if (left.size() != right.size())
        return false;
    size_t at = 0;
    for (; at + 8 <= left.size(); at += 8) {
        uint64_t a = 0;
        uint64_t b = 0;
        std::memcpy(&a, std::next(left.data(), static_cast<ptrdiff_t>(at)), 8);
        std::memcpy(&b, std::next(right.data(), static_cast<ptrdiff_t>(at)), 8);
        if (ascii_lowered_word(a) != ascii_lowered_word(b))
            return false;
    }
    uint64_t a = 0;
    uint64_t b = 0;
    std::memcpy(&a, std::next(left.data(), static_cast<ptrdiff_t>(at)), left.size() - at);
    std::memcpy(&b, std::next(right.data(), static_cast<ptrdiff_t>(at)), left.size() - at);
    return ascii_lowered_word(a) == ascii_lowered_word(b);
}

std::string_view wide_field_lookup(const std::vector<ChromeField> &fields,
                                   const std::string_view name)
{
    for (size_t at = 0; at < fields.size(); at++)
        if (equal_ignoring_case_wide(fields[at].name, name))
            return fields[at].value;
    return std::string_view{};
}

void wide_field_lookup_last(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = wide_field_lookup(kChromeFields, "accept-language");
        benchmark::DoNotOptimize(got);
    }
}

void wide_field_lookup_absent(benchmark::State &state)
{
    for (auto _ : state) {
        auto got = wide_field_lookup(kChromeFields, "if-none-match");
        benchmark::DoNotOptimize(got);
    }
}

void wide_four_lookups(benchmark::State &state)
{
    for (auto _ : state) {
        size_t seen = wide_field_lookup(kChromeFields, "host").size() +
                      wide_field_lookup(kChromeFields, "accept").size() +
                      wide_field_lookup(kChromeFields, "accept-encoding").size() +
                      wide_field_lookup(kChromeFields, "accept-language").size();
        benchmark::DoNotOptimize(seen);
    }
}

// The third shape, and the archive's: the fields are walked once and the
// names this server knows are recognised on the way past. A length that
// no known name has is one test - webmachine.hpp's length_is_one_of - so
// most fields are rejected before a byte is compared. After the pass the
// four values are in hand and no lookup happens at all.
constexpr uint32_t kKnownFieldLengths = (1u << 4) | (1u << 6) | (1u << 15);

struct KnownFields {
    std::string_view host;
    std::string_view accept;
    std::string_view accept_encoding;
    std::string_view accept_language;
};

KnownFields classify_once(const std::vector<ChromeField> &fields)
{
    KnownFields known{};
    for (const ChromeField field : fields) {
        const size_t length = field.name.size();
        if (length >= 32 || ((kKnownFieldLengths >> length) & 1u) == 0)
            continue;
        switch (length) {
        case 4:
            if (http::equal_ignoring_case(field.name, "host"))
                known.host = field.value;
            break;
        case 6:
            if (http::equal_ignoring_case(field.name, "accept"))
                known.accept = field.value;
            break;
        case 15:
            if (http::equal_ignoring_case(field.name, "accept-encoding"))
                known.accept_encoding = field.value;
            else if (http::equal_ignoring_case(field.name, "accept-language"))
                known.accept_language = field.value;
            break;
        default:
            break;
        }
    }
    return known;
}

KnownFields classify_once_wide(const std::vector<ChromeField> &fields)
{
    KnownFields known{};
    for (const ChromeField field : fields) {
        const size_t length = field.name.size();
        if (length >= 32 || ((kKnownFieldLengths >> length) & 1u) == 0)
            continue;
        switch (length) {
        case 4:
            if (equal_ignoring_case_wide(field.name, "host"))
                known.host = field.value;
            break;
        case 6:
            if (equal_ignoring_case_wide(field.name, "accept"))
                known.accept = field.value;
            break;
        case 15:
            if (equal_ignoring_case_wide(field.name, "accept-encoding"))
                known.accept_encoding = field.value;
            else if (equal_ignoring_case_wide(field.name, "accept-language"))
                known.accept_language = field.value;
            break;
        default:
            break;
        }
    }
    return known;
}

void classify_once_wide_four(benchmark::State &state)
{
    for (auto _ : state) {
        const KnownFields known = classify_once_wide(kChromeFields);
        size_t seen = known.host.size() + known.accept.size() +
                      known.accept_encoding.size() + known.accept_language.size();
        benchmark::DoNotOptimize(seen);
    }
}

bool equal_ignoring_case_libc(const std::string_view left, const std::string_view right)
{
    return left.size() == right.size() &&
           strncasecmp(left.data(), right.data(), right.size()) == 0;
}

KnownFields classify_once_libc(const std::vector<ChromeField> &fields)
{
    KnownFields known{};
    for (const ChromeField field : fields) {
        const size_t length = field.name.size();
        if (length >= 32 || ((kKnownFieldLengths >> length) & 1u) == 0)
            continue;
        switch (length) {
        case 4:
            if (equal_ignoring_case_libc(field.name, "host"))
                known.host = field.value;
            break;
        case 6:
            if (equal_ignoring_case_libc(field.name, "accept"))
                known.accept = field.value;
            break;
        case 15:
            if (equal_ignoring_case_libc(field.name, "accept-encoding"))
                known.accept_encoding = field.value;
            else if (equal_ignoring_case_libc(field.name, "accept-language"))
                known.accept_language = field.value;
            break;
        default:
            break;
        }
    }
    return known;
}

void classify_once_libc_four(benchmark::State &state)
{
    for (auto _ : state) {
        const KnownFields known = classify_once_libc(kChromeFields);
        size_t seen = known.host.size() + known.accept.size() +
                      known.accept_encoding.size() + known.accept_language.size();
        benchmark::DoNotOptimize(seen);
    }
}

// Can we hold to HTTP and still beat it. The one thing glibc cannot do
// and this tree can: read past the end of a name. The ring leaves
// kWidePadding bytes behind every byte of the pool, so a name of four
// bytes can be loaded as eight and the bytes that are not the name are
// masked away. The needle is a literal, so its word is a constant and the
// whole comparison is a load, an and, the fold and one compare.
constexpr uint64_t ascii_word_at(const std::string_view text, const size_t at)
{
    uint64_t word = 0;
    for (size_t index = 0; index < 8 && at + index < text.size(); index++)
        word |= static_cast<uint64_t>(static_cast<unsigned char>(text.at(at + index)))
                << (index * 8);
    return word;
}

inline uint64_t padded_word_at(const std::string_view text, const size_t at)
{
    uint64_t word = 0;
    std::memcpy(&word, std::next(text.data(), static_cast<ptrdiff_t>(at)), 8);
    const size_t left = text.size() - at;
    return left >= 8 ? word : word & ((uint64_t{1} << (left * 8)) - 1);
}

// The fold above is right for tchar and wrong for a byte above 0x7f: the
// carry of 0xc1 + 0x3f lands in the next byte. A field name is tchar, but
// the comparison runs before anything says so, so bit 7 is taken out of
// the range test and put back as the last and.
uint64_t ascii_lowered_word_safe(const uint64_t word)
{
    const uint64_t high = 0x8080808080808080ull;
    const uint64_t seven = word & ~high;
    const uint64_t at_least_a = seven + 0x3f3f3f3f3f3f3f3full;
    const uint64_t at_most_z = seven + 0x2525252525252525ull;
    return word | ((at_least_a & ~at_most_z & ~word & high) >> 2);
}

inline bool name_is_safe(const std::string_view name, const std::string_view lowercase)
{
    if (name.size() != lowercase.size())
        return false;
    if (ascii_lowered_word_safe(padded_word_at(name, 0)) != ascii_word_at(lowercase, 0))
        return false;
    if (name.size() <= 8)
        return true;
    if (ascii_lowered_word_safe(padded_word_at(name, 8)) != ascii_word_at(lowercase, 8))
        return false;
    return name.size() <= 16 || equal_ignoring_case_wide(name.substr(16), lowercase.substr(16));
}

std::string_view safe_field_lookup(const std::vector<ChromeField> &fields,
                                   const std::string_view name)
{
    for (size_t at = 0; at < fields.size(); at++)
        if (name_is_safe(fields[at].name, name))
            return fields[at].value;
    return std::string_view{};
}

void safe_four_lookups(benchmark::State &state)
{
    for (auto _ : state) {
        size_t seen = safe_field_lookup(kChromeFields, "host").size() +
                      safe_field_lookup(kChromeFields, "accept").size() +
                      safe_field_lookup(kChromeFields, "accept-encoding").size() +
                      safe_field_lookup(kChromeFields, "accept-language").size();
        benchmark::DoNotOptimize(seen);
    }
}

inline bool name_is(const std::string_view name, const std::string_view lowercase)
{
    if (name.size() != lowercase.size())
        return false;
    if (ascii_lowered_word(padded_word_at(name, 0)) != ascii_word_at(lowercase, 0))
        return false;
    if (name.size() <= 8)
        return true;
    if (ascii_lowered_word(padded_word_at(name, 8)) != ascii_word_at(lowercase, 8))
        return false;
    return name.size() <= 16 || equal_ignoring_case_wide(name.substr(16), lowercase.substr(16));
}

std::string_view padded_field_lookup(const std::vector<ChromeField> &fields,
                                     const std::string_view name)
{
    for (size_t at = 0; at < fields.size(); at++)
        if (name_is(fields[at].name, name))
            return fields[at].value;
    return std::string_view{};
}

void padded_four_lookups(benchmark::State &state)
{
    for (auto _ : state) {
        size_t seen = padded_field_lookup(kChromeFields, "host").size() +
                      padded_field_lookup(kChromeFields, "accept").size() +
                      padded_field_lookup(kChromeFields, "accept-encoding").size() +
                      padded_field_lookup(kChromeFields, "accept-language").size();
        benchmark::DoNotOptimize(seen);
    }
}

KnownFields classify_once_padded(const std::vector<ChromeField> &fields)
{
    KnownFields known{};
    for (const ChromeField field : fields) {
        const size_t length = field.name.size();
        if (length >= 32 || ((kKnownFieldLengths >> length) & 1u) == 0)
            continue;
        switch (length) {
        case 4:
            if (name_is(field.name, "host"))
                known.host = field.value;
            break;
        case 6:
            if (name_is(field.name, "accept"))
                known.accept = field.value;
            break;
        case 15:
            if (name_is(field.name, "accept-encoding"))
                known.accept_encoding = field.value;
            else if (name_is(field.name, "accept-language"))
                known.accept_language = field.value;
            break;
        default:
            break;
        }
    }
    return known;
}

void classify_once_padded_four(benchmark::State &state)
{
    for (auto _ : state) {
        const KnownFields known = classify_once_padded(kChromeFields);
        size_t seen = known.host.size() + known.accept.size() +
                      known.accept_encoding.size() + known.accept_language.size();
        benchmark::DoNotOptimize(seen);
    }
}

void classify_once_four(benchmark::State &state)
{
    for (auto _ : state) {
        const KnownFields known = classify_once(kChromeFields);
        size_t seen = known.host.size() + known.accept.size() +
                      known.accept_encoding.size() + known.accept_language.size();
        benchmark::DoNotOptimize(seen);
    }
}

BENCHMARK(is_tchar);
BENCHMARK(every_byte_over_field_names);
BENCHMARK(is_token_over_field_names);
BENCHMARK(every_byte_over_a_host);
BENCHMARK(is_reg_name_over_a_host);
BENCHMARK(parse_host_of_chrome);
BENCHMARK(parse_quoted_string);
BENCHMARK(parse_field_value_parameter);
BENCHMARK(parse_imf_fixdate);
BENCHMARK(parse_http_date);
BENCHMARK(media_type_archive);
BENCHMARK(media_type_new);
BENCHMARK(charset_archive);
BENCHMARK(charset_new);
BENCHMARK(chrome_read_every_field);
BENCHMARK(chrome_plain_get);
BENCHMARK(chrome_conditional_get);
BENCHMARK(chrome_negotiated_get);
#if defined(__AVX2__)
BENCHMARK(chrome_one_pass);
#endif
BENCHMARK(libreactor_field_lookup_first);
BENCHMARK(field_lookup_first);
BENCHMARK(libreactor_field_lookup_last);
BENCHMARK(field_lookup_last);
BENCHMARK(wide_field_lookup_last);
BENCHMARK(libreactor_field_lookup_absent);
BENCHMARK(field_lookup_absent);
BENCHMARK(wide_field_lookup_absent);
BENCHMARK(libreactor_four_lookups);
BENCHMARK(four_lookups);
BENCHMARK(wide_four_lookups);
BENCHMARK(classify_once_four);
BENCHMARK(classify_once_wide_four);
BENCHMARK(classify_once_libc_four);
BENCHMARK(padded_four_lookups);
BENCHMARK(classify_once_padded_four);
BENCHMARK(safe_four_lookups);
BENCHMARK(parameter_walk_archive);
BENCHMARK(parameter_walk_new);
BENCHMARK(charset_quoted_archive_keeps_quotes);
BENCHMARK(charset_quoted_new_unquotes);

} // namespace

BENCHMARK_MAIN();
