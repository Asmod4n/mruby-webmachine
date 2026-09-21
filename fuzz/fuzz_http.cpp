#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>

#include "http.hpp"

namespace
{

void demand(const bool held, const char *const what)
{
    if (held)
        return;
    std::fputs(what, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

constexpr size_t kArenaBytes = 64 * 1024;

constexpr char kAllowedEverywhere = 'a';

class Walled
{
  public:
    Walled()
    {
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        char *const whole =
            static_cast<char *>(mmap(nullptr, kArenaBytes + page, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (whole == MAP_FAILED)
            throw std::system_error(errno, std::system_category(), "mmap");
        if (mprotect(std::next(whole, kArenaBytes), page, PROT_NONE) != 0)
            throw std::system_error(errno, std::system_category(), "mprotect");
        wall_ = std::next(whole, kArenaBytes);
    }

    static constexpr size_t kLongest = kArenaBytes - http::kWidePadding;

    std::string_view hold(const std::span<const uint8_t> bytes) const
    {
        char *const at = std::prev(wall_, http::kWidePadding + bytes.size());
        std::fill_n(at, http::kWidePadding + bytes.size(), kAllowedEverywhere);
        std::copy(bytes.begin(), bytes.end(), at);
        return {at, bytes.size()};
    }

  private:
    char *wall_ = nullptr;
};

const Walled kWalled;

void problem_holds(const http::Refusal refusal)
{
    demand(refusal.problem < http::kProblems.size(), "the problem is not in the table");
}

void refusal_holds(const http::Refusal refusal, const std::string_view text)
{
    problem_holds(refusal);
    if (http::kProblems.at(refusal.problem).status >= 500)
        return;
    demand(refusal.offset <= text.size(), "the offset is past the end of the text");
    const http::ParseError error(refusal, text);
    demand(!error.title().empty(), "the problem has no title");
    demand(error.excerpt().size() <= text.size(), "the excerpt is longer than the text");
}

void inside(const std::string_view part, const std::string_view whole)
{
    if (part.empty())
        return;
    demand(part.data() >= whole.data(), "the result starts before the text");
    demand(std::next(part.data(), static_cast<ptrdiff_t>(part.size())) <=
               std::next(whole.data(), static_cast<ptrdiff_t>(whole.size())),
           "the result ends after the text");
}

void scans_agree(const std::string_view text, const std::array<bool, 256> &allowed,
                 const std::array<unsigned char, 16> &low_bits)
{
    const size_t wide = http::allowed_run_length(text, allowed, low_bits);
    const auto found = std::ranges::find_if_not(text, [&allowed](const char letter) {
        return allowed.at(static_cast<unsigned char>(letter));
    });
    demand(wide == static_cast<size_t>(std::distance(text.begin(), found)),
           "the wide scan and the byte loop disagree");
}

enum class Entry : uint8_t {
    kRuns,
    kQuotedString,
    kListElements,
    kFieldValueParameter,
    kHttpDate,
    kFixdateRoundTrip,
    kHost,
    kRequestTarget,
    kOriginForm,
    kAbsoluteForm,
    kPath,
    kPercentDecode,
    kMediaType,
    kContentLength,
    kQvalue,
    kMediaTypeWeight,
    kCodingWeight,
    kLanguageWeight,
    kStructuredItem,
    kAcceptQuery,
    kRange,
    kEntityTag,
    kConditionals,
    kMethod,
    kStatus,
    kExpectation,
    kCredentials,
    kQuotedStringRoundTrip,
    kCount,
};

constexpr char kCut = '\n';

std::string_view left_of(const std::string_view both)
{
    return both.substr(0, both.find(kCut));
}

std::string_view right_of(const std::string_view both)
{
    const size_t cut = both.find(kCut);
    return cut == std::string_view::npos ? std::string_view{} : both.substr(cut + 1);
}

constexpr std::chrono::year kCurrentYear{2026};

void run_runs(const std::string_view text)
{
    scans_agree(text, http::kTchar, http::kTcharLowBits);
    scans_agree(text, http::kLowercaseTchar, http::kLowercaseTcharLowBits);
    scans_agree(text, http::kRegName, http::kRegNameLowBits);
    scans_agree(text, http::kPathByte, http::kPathByteLowBits);
    scans_agree(text, http::kQueryByte, http::kQueryByteLowBits);
    demand(http::is_token(text) ==
               (!text.empty() && http::every_byte_is_allowed(text, http::kTchar)),
           "is_token and the byte loop disagree");
    demand(http::is_reg_name(text) ==
               (!text.empty() && http::every_byte_is_allowed(text, http::kRegName)),
           "is_reg_name and the byte loop disagree");
}

void run_quoted_string(const std::string_view text)
{
    const auto quoted = http::parse_quoted_string(text);
    if (!quoted)
        return refusal_holds(quoted.error(), text);
    inside(*quoted, text);
    demand(quoted->size() >= 2, "a quoted-string is shorter than two bytes");
    demand(quoted->starts_with('"') && quoted->ends_with('"'), "a quoted-string is not in quotes");
}

void run_list_elements(const std::string_view text)
{
    std::string_view rest = text;
    while (const auto element = http::parse_list_element(rest)) {
        inside(element->element, text);
        inside(element->rest, text);
        demand(element->rest.size() < rest.size(), "a list element consumed nothing");
        rest = element->rest;
    }
}

void run_field_value_parameter(const std::string_view text)
{
    std::string_view rest = text;
    while (true) {
        const auto parameter = http::parse_field_value_parameter(rest);
        if (!parameter)
            return refusal_holds(parameter.error(), rest);
        if (!*parameter)
            return;
        inside((*parameter)->name, text);
        inside((*parameter)->value, text);
        demand((*parameter)->rest.size() < rest.size(), "a parameter consumed nothing");
        rest = (*parameter)->rest;
    }
}

void run_http_date(const std::string_view text)
{
    const auto moment = http::parse_http_date(text, kCurrentYear);
    if (!moment)
        refusal_holds(moment.error(), text);
    const auto fixdate = http::parse_imf_fixdate(text);
    if (!fixdate)
        refusal_holds(fixdate.error(), text);
    const auto rfc850 = http::parse_rfc850_date(text, kCurrentYear);
    if (!rfc850)
        refusal_holds(rfc850.error(), text);
    const auto asctime = http::parse_asctime_date(text);
    if (!asctime)
        refusal_holds(asctime.error(), text);
}

void run_fixdate_round_trip(const std::string_view text)
{
    if (text.size() < sizeof(int64_t))
        return;
    int64_t seconds = 0;
    std::copy_n(text.data(), sizeof(seconds), reinterpret_cast<char *>(&seconds));

    constexpr int64_t kFirst = -62167219200;
    constexpr int64_t kLast = 253402300799;
    if (seconds < kFirst || seconds > kLast)
        return;
    const std::chrono::sys_seconds moment{std::chrono::seconds{seconds}};
    const auto spelled = http::spell_imf_fixdate(moment);
    const std::string_view view(spelled.data(), spelled.size());
    const auto read_back = http::parse_imf_fixdate(view);
    demand(read_back.has_value(), "this tree cannot read the date it spelled");
    demand(*read_back == moment, "the date changed on the way out and back");
}

void run_host(const std::string_view text)
{
    const auto host = http::parse_host(text);
    if (!host)
        return refusal_holds(host.error(), text);
    inside(host->uri_host, text);
    demand(!host->port || *host->port <= 65535, "the port is above 65535");
    demand(host->uri_host.starts_with('[') || http::percent_decode(host->uri_host).has_value(),
           "the host was taken but cannot be decoded");
}

void target_path_holds(const std::string_view path, const std::string_view text)
{
    if (path == "/")
        return;
    inside(path, text);
}

void run_request_target(const std::string_view text, const http::Method method)
{
    const auto target = http::parse_request_target(text, method);
    if (!target)
        return refusal_holds(target.error(), text);
    target_path_holds(target->path, text);
    inside(target->query, text);
}

void run_origin_form(const std::string_view text)
{
    const auto origin = http::parse_origin_form(text);
    if (!origin)
        return refusal_holds(origin.error(), text);
    inside(origin->path, text);
    inside(origin->query, text);
    const auto query = http::parse_query(origin->query);
    demand(query.has_value(), "a query the origin-form accepted is refused on its own");

    demand(http::percent_decode(origin->path).has_value(),
           "the path was taken but cannot be decoded");
    demand(http::percent_decode(origin->query).has_value(),
           "the query was taken but cannot be decoded");
}

void run_absolute_form(const std::string_view text)
{
    const auto target = http::parse_absolute_form(text);
    if (!target)
        return refusal_holds(target.error(), text);
    inside(target->scheme, text);
}

void run_path(const std::string_view text)
{
    const std::string removed = http::remove_dot_segments(text);
    demand(removed.size() <= text.size(), "remove_dot_segments made the path longer");
    demand(!http::path_has_dot_segment(removed), "a dot segment survived remove_dot_segments");
    const std::string twice = http::remove_dot_segments(removed);
    demand(twice == removed, "remove_dot_segments is not settled after one round");
}

void run_percent_decode(const std::string_view text)
{
    const auto decoded = http::percent_decode(text);
    if (!decoded)
        return refusal_holds(decoded.error(), text);
    demand(decoded->size() <= text.size(), "percent_decode made the text longer");
}

void run_media_type(const std::string_view both)
{
    const std::string_view text = left_of(both);
    const auto media = http::parse_media_type(text);
    if (!media)
        return refusal_holds(media.error(), text);
    inside(media->type, text);
    inside(media->subtype, text);
    inside(media->parameters, text);
    demand(http::is_token(media->type), "a media type has a type that is not a token");
    demand(http::is_token(media->subtype), "a media type has a subtype that is not a token");
    const auto found = http::value_of_parameter(media->parameters, right_of(both));
    if (!found)
        return refusal_holds(found.error(), media->parameters);
    if (*found)
        inside(**found, text);
}

void run_content_length(const std::string_view text)
{
    const auto one = http::parse_content_length(text);
    if (!one)
        refusal_holds(one.error(), text);
    const auto list = http::parse_content_length_list(text);
    if (!list)
        return refusal_holds(list.error(), text);

    if (one)
        demand(*one == *list, "the list and the number disagree");
}

void run_qvalue(const std::string_view both)
{
    const auto weight = http::parse_qvalue(left_of(both));
    if (!weight)
        refusal_holds(weight.error(), left_of(both));
    else
        demand(*weight <= http::kMostPreferred, "a qvalue is above 1.000");
    const auto found = http::weight_of(right_of(both));
    if (!found)
        refusal_holds(found.error(), right_of(both));
    else
        demand(*found <= http::kMostPreferred, "a weight is above 1.000");
}

void run_media_type_weight(const std::string_view both)
{
    const auto provided = http::parse_media_type(right_of(both));
    if (!provided)
        return;
    const auto weight = http::media_type_weight(left_of(both), *provided);
    if (!weight)
        return refusal_holds(weight.error(), left_of(both));
    demand(*weight <= http::kMostPreferred, "a media type weight is above 1.000");
    const std::array<http::MediaType, 1> one = {*provided};
    const auto chosen = http::choose_media_type(one, left_of(both));
    demand(chosen.has_value(), "choose_media_type refuses what media_type_weight accepted");
    demand(!*chosen || (*chosen)->at == 0, "choose_media_type named a media type nobody gave");
}

void run_coding_weight(const std::string_view both)
{
    const auto weight = http::coding_weight(left_of(both), right_of(both));
    if (!weight)
        return refusal_holds(weight.error(), left_of(both));
    demand(*weight <= http::kMostPreferred, "a coding weight is above 1.000");
    const std::array<std::string_view, 1> one = {right_of(both)};
    const auto chosen = http::choose_coding(one, left_of(both));
    demand(chosen.has_value(), "choose_coding refuses what coding_weight accepted");
}

void run_language_weight(const std::string_view both)
{
    const auto weight = http::language_weight(left_of(both), right_of(both));
    if (!weight)
        return refusal_holds(weight.error(), left_of(both));
    demand(*weight <= http::kMostPreferred, "a language weight is above 1.000");
    const std::array<std::string_view, 1> one = {right_of(both)};
    const auto chosen = http::choose_language(one, left_of(both));
    demand(chosen.has_value(), "choose_language refuses what language_weight accepted");
}

void run_structured_item(const std::string_view text)
{
    const auto item = http::spell_structured_item(text);
    if (!item)
        return refusal_holds(item.error(), text);
    if (http::is_structured_token(text)) {
        demand(*item == text, "a token was spelled as something else");
        return;
    }
    demand(item->size() >= 2 && item->front() == '"' && item->back() == '"',
           "a string was not spelled in quotes");
    for (size_t at = 1; at + 1 < item->size(); at++) {
        const unsigned char byte = static_cast<unsigned char>(item->at(at));
        demand(byte >= 0x20 && byte <= 0x7e, "a byte outside %x20-7E reached a string");
        if (byte != '\\')
            continue;
        demand(at + 2 < item->size(), "a string ends in a backslash");
        const char escaped = item->at(at + 1);
        demand(escaped == '"' || escaped == '\\', "a string escapes a byte the RFC does not");
        at++;
    }
}

void run_accept_query(const std::string_view both)
{
    const auto first = http::parse_media_type(left_of(both));
    const auto second = http::parse_media_type(right_of(both));
    if (!first || !second)
        return;
    const std::array<http::MediaType, 2> provided = {*first, *second};
    const auto spelled = http::spell_accept_query(provided);
    if (!spelled)
        return refusal_holds(spelled.error(), left_of(both));
    for (const char letter : *spelled)
        demand(static_cast<unsigned char>(letter) >= 0x20 &&
                   static_cast<unsigned char>(letter) <= 0x7e,
               "Accept-Query carries a byte no field value may carry");
}

void run_range(const std::string_view both)
{
    const std::string_view text = left_of(both);
    const auto specifier = http::parse_ranges_specifier(text);
    if (!specifier)
        return refusal_holds(specifier.error(), text);
    inside(specifier->range_unit, text);
    inside(specifier->range_set, text);
    uint64_t complete_length = 0;
    const std::string_view length = right_of(both);
    std::copy_n(length.data(), std::min(sizeof(complete_length), length.size()),
                reinterpret_cast<char *>(&complete_length));
    std::string_view rest = specifier->range_set;
    while (const auto element = http::parse_list_element(rest)) {
        const auto spec = http::parse_byte_range_spec(element->element);
        if (!spec) {
            refusal_holds(spec.error(), element->element);
            return;
        }

        if (const auto resolved = http::resolved_range(*spec, complete_length)) {
            demand(resolved->first_pos <= resolved->last_pos, "a range ends before it starts");
            demand(resolved->last_pos < complete_length, "a range names a byte that is not there");
        }
        rest = element->rest;
    }
}

void run_entity_tag(const std::string_view text)
{
    const auto tag = http::parse_entity_tag(text);
    if (!tag)
        return refusal_holds(tag.error(), text);
    inside(tag->opaque_tag, text);
    demand(http::weak_comparison(*tag, *tag), "an entity tag does not match itself weakly");
    demand(http::strong_comparison(*tag, *tag) != tag->weak,
           "a strong comparison does not follow the weak flag");
}

void run_conditionals(const std::string_view both)
{
    const std::string_view field = left_of(both);
    const auto selected = http::parse_entity_tag(right_of(both));
    const std::optional<http::EntityTag> tag =
        selected ? std::optional<http::EntityTag>(*selected) : std::nullopt;
    const auto matched = http::if_match_passes(field, true, tag);
    if (!matched)
        refusal_holds(matched.error(), field);
    const auto none = http::if_none_match_passes(field, true, tag);
    if (!none)
        refusal_holds(none.error(), field);

    if (matched && none && field != "*" && tag && *matched)
        demand(!*none, "If-Match and If-None-Match both passed one entity tag");
    const auto ranged = http::if_range_passes(field, tag, std::nullopt, kCurrentYear);
    if (!ranged)
        refusal_holds(ranged.error(), field);
}

void run_method(const std::string_view text)
{
    const http::Method method = http::method_of(text);
    if (method == http::Method::kUnknown) {
        demand(!http::is_known_method(method), "an unknown method is known");
        return;
    }
    demand(http::method_name_of(method) == text, "a method does not spell its own name");
    demand(!http::is_safe(method) || http::is_idempotent(method),
           "a safe method is not idempotent");
    demand(!http::is_cacheable(method) || http::is_safe(method) || method == http::Method::kPost,
           "a method nobody may cache is cacheable");
}

void run_status(const std::string_view text)
{
    if (text.size() < 2)
        return;
    const auto status = static_cast<uint16_t>((static_cast<unsigned char>(text[0]) << 8) |
                                              static_cast<unsigned char>(text[1]));
    if (!http::is_status(status))
        return;
    const http::StatusClass held = http::status_class(status);
    demand(static_cast<unsigned>(held) == status / 100u - 1u,
           "the class does not follow the first digit");
    demand(!http::is_heuristically_cacheable(status) || !http::reason_phrase(status).empty(),
           "a status a cache may keep has no reason phrase");
    demand(http::content_is_forbidden(status) ==
               (held == http::StatusClass::kInformational || status == 204 || status == 304),
           "content_is_forbidden names something other than 1xx, 204 and 304");
}

void run_expectation(const std::string_view text)
{
    const bool understood = http::every_expectation_is_understood(text);

    if (understood && !text.empty())
        demand(text.find(',') == std::string_view::npos ||
                   http::every_expectation_is_understood(text),
               "a list of understood expectations stopped being understood");
}

void run_credentials(const std::string_view text)
{
    const auto got = http::parse_credentials(text);
    if (!got)
        return refusal_holds(got.error(), text);
    inside(got->auth_scheme, text);
    inside(got->rest, text);
    demand(http::is_token(got->auth_scheme), "an auth-scheme that is not a token");

    const auto spelled = http::spell_challenge(got->auth_scheme, {});
    demand(spelled.has_value(), "a scheme that parses cannot be spelled");
    demand(*spelled == got->auth_scheme, "spelling a bare scheme changed it");
}

void run_quoted_string_round_trip(const std::string_view text)
{
    const auto spelled = http::spell_quoted_string(text);
    if (!spelled)
        return refusal_holds(spelled.error(), text);
    const auto read_back = http::parse_quoted_string(*spelled);
    demand(read_back.has_value(), "this tree cannot read the quoted-string it wrote");
    demand(read_back->size() == spelled->size(),
           "the quoted-string it wrote ends before its own end");
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size - 1 > Walled::kLongest)
        return 0;
    const Entry entry = static_cast<Entry>(data[0] % static_cast<uint8_t>(Entry::kCount));
    const std::string_view text = kWalled.hold({std::next(data), size - 1});

    switch (entry) {
        case Entry::kRuns:
            return run_runs(text), 0;
        case Entry::kQuotedString:
            return run_quoted_string(text), 0;
        case Entry::kListElements:
            return run_list_elements(text), 0;
        case Entry::kFieldValueParameter:
            return run_field_value_parameter(text), 0;
        case Entry::kHttpDate:
            return run_http_date(text), 0;
        case Entry::kFixdateRoundTrip:
            return run_fixdate_round_trip(text), 0;
        case Entry::kHost:
            return run_host(text), 0;
        case Entry::kRequestTarget:
            return run_request_target(left_of(text), http::method_of(right_of(text))), 0;
        case Entry::kOriginForm:
            return run_origin_form(text), 0;
        case Entry::kAbsoluteForm:
            return run_absolute_form(text), 0;
        case Entry::kPath:
            return run_path(text), 0;
        case Entry::kPercentDecode:
            return run_percent_decode(text), 0;
        case Entry::kMediaType:
            return run_media_type(text), 0;
        case Entry::kContentLength:
            return run_content_length(text), 0;
        case Entry::kQvalue:
            return run_qvalue(text), 0;
        case Entry::kMediaTypeWeight:
            return run_media_type_weight(text), 0;
        case Entry::kCodingWeight:
            return run_coding_weight(text), 0;
        case Entry::kLanguageWeight:
            return run_language_weight(text), 0;
        case Entry::kStructuredItem:
            return run_structured_item(text), 0;
        case Entry::kAcceptQuery:
            return run_accept_query(text), 0;
        case Entry::kRange:
            return run_range(text), 0;
        case Entry::kEntityTag:
            return run_entity_tag(text), 0;
        case Entry::kConditionals:
            return run_conditionals(text), 0;
        case Entry::kMethod:
            return run_method(text), 0;
        case Entry::kStatus:
            return run_status(text), 0;
        case Entry::kExpectation:
            return run_expectation(text), 0;
        case Entry::kCredentials:
            return run_credentials(text), 0;
        case Entry::kQuotedStringRoundTrip:
            return run_quoted_string_round_trip(text), 0;
        case Entry::kCount:
            break;
    }
    return 0;
}
