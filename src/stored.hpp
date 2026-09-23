#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cache.h"
#include "http.hpp"
#include "http1.hpp"
#include "webmachine.hpp"

namespace stored
{

inline std::optional<std::string_view>
field_of(cache_held *const held, const uint64_t route, const uint8_t field, const uint64_t now)
{
    const cache_answer answer = cache_asked(held, route, field, now);
    if (answer.value == nullptr) return std::nullopt;
    return std::string_view(reinterpret_cast<const char *>(answer.value), answer.length);
}

inline std::optional<std::string_view>
body_of(cache_held *const held, const uint64_t route, const uint64_t now)
{
    const cache_answer answer = cache_body_asked(held, route, now);
    if (answer.value == nullptr) return std::nullopt;
    return std::string_view(reinterpret_cast<const char *>(answer.value), answer.length);
}

class Resource : public webmachine::Resource
{
  public:
    Resource(const webmachine::Resource &inner, cache_held *const held, const uint64_t route,
           const std::chrono::sys_seconds now, const std::chrono::year current_year)
        : inner_(inner), held_(held), route_(route), now_(now), current_year_(current_year)
    {
    }

    std::string_view generate_etag(const http1::Request &request) const override
    {
        const auto stored = field_of(held_, route_, kCacheFieldEntityTag, seconds_now());
        if (stored) return *stored;
        return inner_.generate_etag(request);
    }
    std::optional<std::chrono::sys_seconds> last_modified(const http1::Request &request) const override
    {
        const auto stored = date_of(kCacheFieldLastModified);
        if (stored) return *stored;
        return inner_.last_modified(request);
    }
    std::optional<std::chrono::sys_seconds> expires(const http1::Request &request) const override
    {
        const auto stored = date_of(kCacheFieldExpires);
        if (stored) return *stored;
        return inner_.expires(request);
    }
    bool resource_exists(const http1::Request & request) const override
    {
        return inner_.resource_exists(request);
    }
    bool service_available(const http1::Request & request) const override
    {
        return inner_.service_available(request);
    }
    bool is_authorized(const std::string_view authorization) const override
    {
        return inner_.is_authorized(authorization);
    }
    bool forbidden(const http1::Request & request) const override
    {
        return inner_.forbidden(request);
    }
    bool allow_missing_post(const http1::Request & request) const override
    {
        return inner_.allow_missing_post(request);
    }
    bool malformed_request(const http1::Request & request) const override
    {
        return inner_.malformed_request(request);
    }
    bool uri_too_long(const std::string_view uri) const override
    {
        return inner_.uri_too_long(uri);
    }
    bool known_content_type(const std::string_view content_type) const override
    {
        return inner_.known_content_type(content_type);
    }
    bool valid_content_headers(const http1::Request & request) const override
    {
        return inner_.valid_content_headers(request);
    }
    bool valid_entity_length(const std::optional<uint64_t> length) const override
    {
        return inner_.valid_entity_length(length);
    }
    std::span<const webmachine::FieldToSend> options() const override
    {
        return inner_.options();
    }
    std::span<const http::Method> allowed_methods() const override
    {
        return inner_.allowed_methods();
    }
    std::span<const http::Method> known_methods() const override
    {
        return inner_.known_methods();
    }
    bool delete_resource(const http1::Request & request) const override
    {
        return inner_.delete_resource(request);
    }
    bool delete_completed(const http1::Request & request) const override
    {
        return inner_.delete_completed(request);
    }
    bool post_is_create(const http1::Request & request) const override
    {
        return inner_.post_is_create(request);
    }
    std::string_view create_path(const http1::Request & request) const override
    {
        return inner_.create_path(request);
    }
    std::string_view base_uri(const http1::Request & request) const override
    {
        return inner_.base_uri(request);
    }
    bool process_post(const http1::Request & request) const override
    {
        return inner_.process_post(request);
    }
    std::span<const webmachine::MediaTypeHandler> content_types_provided() const override
    {
        return inner_.content_types_provided();
    }
    std::span<const webmachine::MediaTypeHandler> content_types_accepted() const override
    {
        return inner_.content_types_accepted();
    }
    std::span<const webmachine::CharsetHandler> charsets_provided() const override
    {
        return inner_.charsets_provided();
    }
    std::span<const std::string_view> languages_provided() const override
    {
        return inner_.languages_provided();
    }
    void language_chosen(const std::string_view language_tag) const override
    {
        inner_.language_chosen(language_tag);
    }
    std::span<const webmachine::EncodingHandler> encodings_provided() const override
    {
        return inner_.encodings_provided();
    }
    std::span<const std::string_view> variances() const override
    {
        return inner_.variances();
    }
    bool is_conflict(const http1::Request & request) const override
    {
        return inner_.is_conflict(request);
    }
    bool multiple_choices(const http1::Request & request) const override
    {
        return inner_.multiple_choices(request);
    }
    bool previously_existed(const http1::Request & request) const override
    {
        return inner_.previously_existed(request);
    }
    bool moved_permanently(const http1::Request & request) const override
    {
        return inner_.moved_permanently(request);
    }
    bool moved_temporarily(const http1::Request & request) const override
    {
        return inner_.moved_temporarily(request);
    }
    void finish_request(const http1::Request & request) const override
    {
        inner_.finish_request(request);
    }
    std::optional<bool> validate_content_checksum(const http1::Request & request) const override
    {
        return inner_.validate_content_checksum(request);
    }

  private:
    uint64_t seconds_now() const
    {
        return static_cast<uint64_t>(now_.time_since_epoch().count());
    }
    std::optional<std::chrono::sys_seconds> date_of(const uint8_t field) const
    {
        const auto stored = field_of(held_, route_, field, seconds_now());
        if (!stored) return std::nullopt;
        const auto date = http::parse_http_date(*stored, current_year_);
        if (!date) [[unlikely]] return std::nullopt;
        return *date;
    }

    const webmachine::Resource &inner_;
    cache_held *const held_;
    const uint64_t route_;
    const std::chrono::sys_seconds now_;
    const std::chrono::year current_year_;
};

}
