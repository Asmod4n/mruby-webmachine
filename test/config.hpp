#pragma once

#include <mruby.h>
#include <mruby/string.h>

#include <string_view>

#include "../src/config.hpp"

namespace
{

mrb_value spec_fingerprint_key_in(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const auto key = config::fingerprint_key_in(mrb, std::string_view(text, static_cast<size_t>(length)));
    if (!key)
        return mrb_str_new_cstr(mrb, key.error().what());
    if (!*key)
        return mrb_nil_value();
    const std::string hex = fingerprint::hex_of(**key);
    return mrb_symbol_value(mrb_intern(mrb, hex.data(), hex.size()));
}

} // namespace

inline void config_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecConfig");
    mrb_define_module_function(mrb, sp, "fingerprint_key_in", spec_fingerprint_key_in, MRB_ARGS_REQ(1));
}
