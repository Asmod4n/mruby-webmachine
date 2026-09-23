#pragma once

#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <mruby.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/presym.h>
#include <mruby/string.h>

#include "fingerprint.hpp"

namespace config
{

class ConfigError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct TomlText {
    std::string_view text;
};

inline mrb_value
toml_parse_in_protected_call(mrb_state *mrb, void *const user_data)
{
    const TomlText *const ask = static_cast<const TomlText *>(user_data);
    const mrb_value text = mrb_str_new(mrb, ask->text.data(), static_cast<mrb_int>(ask->text.size()));
    const mrb_value toml = mrb_obj_value(mrb_module_get_id(mrb, MRB_SYM(TOML)));
    return mrb_funcall_argv(mrb, toml, MRB_SYM(parse), 1, &text);
}

struct TableAsk {
    mrb_value   document;
    std::string name;
};

inline mrb_value
toml_table_in_protected_call(mrb_state *mrb, void *const user_data)
{
    const TableAsk *const ask = static_cast<const TableAsk *>(user_data);
    const mrb_value name = mrb_str_new(mrb, ask->name.data(), static_cast<mrb_int>(ask->name.size()));
    return mrb_funcall_argv(mrb, ask->document, MRB_OPSYM(aref), 1, &name);
}

inline std::string
message_of(mrb_state *mrb, const mrb_value exception)
{
    const mrb_value message = mrb_funcall_argv(mrb, exception, MRB_SYM(message), 0, nullptr);
    return std::string(RSTRING_PTR(message), static_cast<size_t>(RSTRING_LEN(message)));
}

inline std::expected<mrb_value, ConfigError>
document_of(mrb_state *mrb, const std::string_view text)
{
    TomlText ask{text};
    mrb_bool raised = false;
    const mrb_value document = mrb_protect_error(mrb, toml_parse_in_protected_call, &ask, &raised);
    if (raised) [[unlikely]] {
        mrb->exc = nullptr;
        return std::unexpected(ConfigError("webmachine.toml: " + message_of(mrb, document)));
    }
    return document;
}

inline std::optional<mrb_value>
table_in(mrb_state *mrb, const mrb_value document, const std::string_view name)
{
    TableAsk ask{document, std::string(name)};
    mrb_bool raised = false;
    const mrb_value table = mrb_protect_error(mrb, toml_table_in_protected_call, &ask, &raised);
    if (raised) {
        mrb->exc = nullptr;
        return std::nullopt;
    }
    return table;
}

inline std::expected<std::optional<fingerprint::Key>, ConfigError>
fingerprint_key_in(mrb_state *mrb, const std::string_view text)
{
    const std::expected<mrb_value, ConfigError> document = document_of(mrb, text);
    if (!document) [[unlikely]] return std::unexpected(document.error());
    const std::optional<mrb_value> log = table_in(mrb, *document, "log");
    if (!log) return std::nullopt;
    if (!mrb_hash_p(*log)) [[unlikely]] return std::unexpected(ConfigError("webmachine.toml: [log] is not a table"));
    const mrb_value value = mrb_hash_get(mrb, *log, mrb_str_new_lit(mrb, "fingerprint_key"));
    if (mrb_nil_p(value)) return std::nullopt;
    if (!mrb_string_p(value)) [[unlikely]]
        return std::unexpected(ConfigError("webmachine.toml: [log] fingerprint_key is not a string"));
    const std::optional<fingerprint::Key> key =
        fingerprint::key_of(std::string_view(RSTRING_PTR(value), static_cast<size_t>(RSTRING_LEN(value))));
    if (!key) [[unlikely]]
        return std::unexpected(ConfigError("webmachine.toml: [log] fingerprint_key is not 64 hex digits"));
    return key;
}

}
