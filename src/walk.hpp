#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "flow.hpp"
#include "http.hpp"
#include "http1.hpp"
#include "webmachine.hpp"

namespace flow
{

struct Facts {
    http::Method     method;
    std::string_view request_target;
    std::string_view accept;
    std::string_view accept_language;
    std::string_view accept_encoding;
    std::string_view if_match;
    std::string_view if_unmodified_since;
    std::string_view if_none_match;
    std::string_view if_modified_since;
    std::string_view content_type;
    std::string_view content_length;
    std::string_view authorization;
    std::chrono::year current_year;
};

#if defined(MRB_DEBUG)
inline constexpr size_t kPathMost = kNodeCount;

struct Step {
    Node node;
    bool answer;
};
#endif

struct Outcome {
    uint16_t                        status;
    Node                            halted_at;
#if defined(MRB_DEBUG)
    std::array<Step, kPathMost>     path;
    size_t                          path_length;
#endif
    std::optional<size_t>           media_type;
    std::optional<std::string_view> content_language;
    std::optional<std::string_view> content_coding;
    std::string_view                location;
};

constexpr bool
is_present(const std::string_view field_value)
{
    return field_value.data() != nullptr;
}

constexpr Facts
facts_of(const http1::Request &request, const std::chrono::year current_year)
{
    return {request.request_line.method,
            request.request_line.request_target,
            http1::field_value_of(request, "Accept"),
            http1::field_value_of(request, "Accept-Language"),
            http1::field_value_of(request, "Accept-Encoding"),
            http1::field_value_of(request, "If-Match"),
            http1::field_value_of(request, "If-Unmodified-Since"),
            http1::field_value_of(request, "If-None-Match"),
            http1::field_value_of(request, "If-Modified-Since"),
            http1::field_value_of(request, "Content-Type"),
            http1::field_value_of(request, "Content-Length"),
            http1::field_value_of(request, "Authorization"),
            current_year};
}

constexpr bool
holds_method(const std::span<const http::Method> methods, const http::Method method)
{
    return std::ranges::find(methods, method) != methods.end();
}

inline std::optional<http::EntityTag>
entity_tag_of(const webmachine::Resource &resource, const http1::Request &request)
{
    const std::string_view spelled = resource.generate_etag(request);
    if (spelled.empty()) return std::nullopt;
    const auto tag = http::parse_entity_tag(spelled);
    if (!tag) [[unlikely]] return std::nullopt;
    return *tag;
}

inline std::optional<uint64_t>
content_length_of(const std::string_view field_value)
{
    if (!is_present(field_value)) return std::nullopt;
    uint64_t length = 0;
    const auto [end, error] = std::from_chars(field_value.data(), field_value.data() + field_value.size(), length);
    if (error != std::errc{} || end != field_value.data() + field_value.size()) [[unlikely]] return std::nullopt;
    return length;
}

class Walk
{
  public:
    Walk(const webmachine::Resource &resource, const http1::Request &request, const Facts &facts)
        : resource_(resource), request_(request), facts_(facts)
    {
    }

    std::expected<Outcome, http::Refusal> run()
    {
        Node at = Node::kB13;
        for (;;) {
            const std::expected<bool, http::Refusal> answer = answer_at(at);
            if (!answer) [[unlikely]] return std::unexpected(answer.error());
#if defined(MRB_DEBUG)
            if (path_length_ < path_.size()) path_.at(path_length_++) = Step{at, *answer};
#endif
            if (halt_) [[unlikely]] return outcome_at(at, *halt_);
            const Target next = flow::next(at, *answer);
            if (next.node == Node::kCount) return outcome_at(at, next.status);
            at = next.node;
        }
    }

  private:
    Outcome outcome_at(const Node at, const uint16_t status) const
    {
#if defined(MRB_DEBUG)
        return {status, at, path_, path_length_, media_type_, content_language_, content_coding_, location_};
#else
        return {status, at, media_type_, content_language_, content_coding_, location_};
#endif
    }

    bool is_get_or_head() const
    {
        return facts_.method == http::Method::kGet || facts_.method == http::Method::kHead;
    }

    std::expected<bool, http::Refusal> answer_at(const Node at)
    {
        const webmachine::Resource &r = resource_;
        const http1::Request &q = request_;
        const Facts &f = facts_;
        switch (at) {
            case Node::kB13: return r.service_available(q);
            case Node::kB12: return holds_method(r.known_methods(), f.method);
            case Node::kB11: return r.uri_too_long(f.request_target);
            case Node::kB10: return holds_method(r.allowed_methods(), f.method);
            case Node::kB9b: return r.malformed_request(q);
            case Node::kB8: return r.is_authorized(f.authorization);
            case Node::kB7: return r.forbidden(q);
            case Node::kB6: return r.valid_content_headers(q);
            case Node::kB5: return !is_present(f.content_type) || r.known_content_type(f.content_type);
            case Node::kB4: return r.valid_entity_length(content_length_of(f.content_length));
            case Node::kB3: return f.method == http::Method::kOptions;
            case Node::kC3: {
                if (is_present(f.accept)) return true;
                if (!r.content_types_provided().empty()) media_type_ = 0;
                return false;
            }
            case Node::kC4: return chose_media_type();
            case Node::kD4: {
                if (is_present(f.accept_language)) return true;
                const auto languages = r.languages_provided();
                if (!languages.empty()) content_language_ = languages.front();
                return false;
            }
            case Node::kD5: return chose_language();
            case Node::kF6: {
                if (is_present(f.accept_encoding)) return true;
                const auto encodings = r.encodings_provided();
                if (!encodings.empty()) content_coding_ = encodings.front().coding;
                return false;
            }
            case Node::kF7: return chose_coding();
            case Node::kG7: return r.resource_exists(q);
            case Node::kG8: return is_present(f.if_match);
            case Node::kG9: return f.if_match == "*";
            case Node::kG11: return http::if_match_passes(f.if_match, true, entity_tag_of(r, q));
            case Node::kH7: return f.if_match == "*";
            case Node::kH10: return is_present(f.if_unmodified_since);
            case Node::kH11: return http::parse_http_date(f.if_unmodified_since, f.current_year).has_value();
            case Node::kH12: {
                const auto since = http::parse_http_date(f.if_unmodified_since, f.current_year);
                return !http::if_unmodified_since_passes(*since, r.last_modified(q));
            }
            case Node::kI4: return r.moved_permanently(q);
            case Node::kI7: return f.method == http::Method::kPut;
            case Node::kI12: return is_present(f.if_none_match);
            case Node::kI13: return f.if_none_match == "*";
            case Node::kJ18: return is_get_or_head();
            case Node::kK5: return r.moved_permanently(q);
            case Node::kK7: return r.previously_existed(q);
            case Node::kK13: {
                const auto passes = http::if_none_match_passes(f.if_none_match, true, entity_tag_of(r, q));
                if (!passes) [[unlikely]] return std::unexpected(passes.error());
                return !*passes;
            }
            case Node::kL5: return r.moved_temporarily(q);
            case Node::kL7: return f.method == http::Method::kPost;
            case Node::kL13: return is_present(f.if_modified_since) && is_get_or_head();
            case Node::kL14: return http::parse_http_date(f.if_modified_since, f.current_year).has_value();
            case Node::kL17: {
                const auto since = http::parse_http_date(f.if_modified_since, f.current_year);
                return http::if_modified_since_passes(*since, r.last_modified(q));
            }
            case Node::kM5: return f.method == http::Method::kPost;
            case Node::kM7: return r.allow_missing_post(q);
            case Node::kM16: return f.method == http::Method::kDelete;
            case Node::kM20: return r.delete_resource(q);
            case Node::kM20b: return r.delete_completed(q);
            case Node::kN5: return r.allow_missing_post(q);
            case Node::kN11: {
                if (r.post_is_create(q)) {
                    location_ = r.create_path(q);
                    return false;
                }
                if (!r.process_post(q)) [[unlikely]] halt_ = 500;
                return false;
            }
            case Node::kN16: return f.method == http::Method::kPost;
            case Node::kO14: return r.is_conflict(q);
            case Node::kO16: return f.method == http::Method::kPut;
            case Node::kO18: return true;
            case Node::kO18b: return r.multiple_choices(q);
            case Node::kO18c: return false;
            case Node::kO18d: return false;
            case Node::kO18e: return false;
            case Node::kO20: return media_type_.has_value();
            case Node::kP3: return r.is_conflict(q);
            case Node::kP11: return !location_.empty();
            case Node::kCount: break;
        }
        return false;
    }

    std::expected<bool, http::Refusal> chose_media_type()
    {
        const auto handlers = resource_.content_types_provided();
        std::vector<http::MediaType> provided;
        provided.reserve(handlers.size());
        for (const webmachine::MediaTypeHandler &handler : handlers) {
            const auto media_type = http::parse_media_type(handler.media_type);
            if (!media_type) [[unlikely]] return std::unexpected(media_type.error());
            provided.push_back(*media_type);
        }
        const auto chosen = http::choose_media_type(provided, facts_.accept);
        if (!chosen) [[unlikely]] return std::unexpected(chosen.error());
        if (!*chosen) return false;
        media_type_ = (*chosen)->at;
        return true;
    }

    std::expected<bool, http::Refusal> chose_language()
    {
        const auto languages = resource_.languages_provided();
        if (languages.empty()) return true;
        uint16_t best = 0;
        for (const std::string_view tag : languages) {
            const auto weight = http::language_weight(facts_.accept_language, tag);
            if (!weight) [[unlikely]] return std::unexpected(weight.error());
            if (*weight > best) {
                best = *weight;
                content_language_ = tag;
            }
        }
        return best != 0;
    }

    std::expected<bool, http::Refusal> chose_coding()
    {
        const auto encodings = resource_.encodings_provided();
        if (encodings.empty()) {
            const auto weight = http::coding_weight(facts_.accept_encoding, "identity");
            if (!weight) [[unlikely]] return std::unexpected(weight.error());
            return *weight != 0;
        }
        uint16_t best = 0;
        for (const webmachine::EncodingHandler &encoding : encodings) {
            const auto weight = http::coding_weight(facts_.accept_encoding, encoding.coding);
            if (!weight) [[unlikely]] return std::unexpected(weight.error());
            if (*weight > best) {
                best = *weight;
                content_coding_ = encoding.coding;
            }
        }
        return best != 0;
    }

    const webmachine::Resource &resource_;
    const http1::Request &request_;
    const Facts &facts_;
    std::optional<size_t> media_type_;
    std::optional<std::string_view> content_language_;
    std::optional<std::string_view> content_coding_;
    std::string_view location_;
    std::optional<uint16_t> halt_;
#if defined(MRB_DEBUG)
    std::array<Step, kPathMost> path_{};
    size_t path_length_ = 0;
#endif
};

inline std::expected<Outcome, http::Refusal>
walk(const webmachine::Resource &resource, const http1::Request &request, const Facts &facts)
{
    Walk w(resource, request, facts);
    return w.run();
}

}
