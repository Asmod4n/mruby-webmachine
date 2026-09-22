#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string_view>

#include "http.hpp"
#include "http1.hpp"

namespace webmachine
{

struct MediaTypeHandler {
    std::string_view media_type;
    std::string_view handler;
};

struct CharsetHandler {
    std::string_view charset;
    std::string_view handler;
};

struct EncodingHandler {
    std::string_view coding;
    std::string_view handler;
};

struct FieldToSend {
    std::string_view field_name;
    std::string_view field_value;
};

struct Raised {
    std::string_view callback;
    std::string_view klass;
    std::string_view message;
    std::span<const std::string_view> backtrace;
};

class Resource
{
  public:
    virtual ~Resource() = default;

    virtual bool resource_exists(const http1::Request &) const
    {
        return true;
    }
    virtual bool service_available(const http1::Request &) const
    {
        return true;
    }
    virtual bool is_authorized(const std::string_view) const
    {
        return true;
    }
    virtual bool forbidden(const http1::Request &) const
    {
        return false;
    }
    virtual bool allow_missing_post(const http1::Request &) const
    {
        return false;
    }
    virtual bool malformed_request(const http1::Request &) const
    {
        return false;
    }
    virtual bool uri_too_long(const std::string_view) const
    {
        return false;
    }
    virtual bool known_content_type(const std::string_view) const
    {
        return true;
    }
    virtual bool valid_content_headers(const http1::Request &) const
    {
        return true;
    }
    virtual bool valid_entity_length(const std::optional<uint64_t>) const
    {
        return true;
    }
    virtual std::span<const FieldToSend> options() const
    {
        return {};
    }
    virtual std::span<const http::Method> allowed_methods() const
    {
        return http::kAllowedMethods;
    }
    virtual std::span<const http::Method> known_methods() const
    {
        return http::kKnownMethods;
    }
    virtual bool delete_resource(const http1::Request &) const
    {
        return false;
    }
    virtual bool delete_completed(const http1::Request &) const
    {
        return true;
    }
    virtual bool post_is_create(const http1::Request &) const
    {
        return false;
    }
    virtual std::string_view create_path(const http1::Request &) const
    {
        return {};
    }
    virtual std::string_view base_uri(const http1::Request &) const
    {
        return {};
    }
    virtual bool process_post(const http1::Request &) const
    {
        return false;
    }
    virtual std::span<const MediaTypeHandler> content_types_provided() const = 0;
    virtual std::span<const MediaTypeHandler> content_types_accepted() const
    {
        return {};
    }
    virtual std::span<const CharsetHandler> charsets_provided() const
    {
        return {};
    }
    virtual std::span<const std::string_view> languages_provided() const
    {
        return {};
    }
    virtual void language_chosen(const std::string_view) const
    {
    }
    virtual std::span<const EncodingHandler> encodings_provided() const
    {
        return {};
    }
    virtual std::span<const std::string_view> variances() const
    {
        return {};
    }
    virtual bool is_conflict(const http1::Request &) const
    {
        return false;
    }
    virtual bool multiple_choices(const http1::Request &) const
    {
        return false;
    }
    virtual bool previously_existed(const http1::Request &) const
    {
        return false;
    }
    virtual bool moved_permanently(const http1::Request &) const
    {
        return false;
    }
    virtual bool moved_temporarily(const http1::Request &) const
    {
        return false;
    }
    virtual std::optional<std::chrono::sys_seconds> last_modified(const http1::Request &) const
    {
        return {};
    }
    virtual std::optional<std::chrono::sys_seconds> expires(const http1::Request &) const
    {
        return {};
    }
    virtual std::string_view generate_etag(const http1::Request &) const
    {
        return {};
    }
    virtual void finish_request(const http1::Request &) const
    {
    }
    virtual std::optional<bool> validate_content_checksum(const http1::Request &) const
    {
        return {};
    }
};

class ErrorResource : public Resource
{
  public:
    virtual std::string_view handle_exception(const Raised &) const = 0;
};

} // namespace webmachine
