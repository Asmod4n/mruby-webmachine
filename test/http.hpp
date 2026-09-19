#ifndef WEBMACHINE_TEST_HTTP_HPP
#define WEBMACHINE_TEST_HTTP_HPP

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/cpp_to_mrb_value.hpp>

#include "../src/http.hpp"

namespace
{

mrb_value spec_is_tchar(mrb_state *mrb, mrb_value)
{
    mrb_int byte = 0;
    mrb_get_args(mrb, "i", &byte);
    return mrb_bool_value(http::is_tchar(static_cast<char>(byte)));
}

mrb_value spec_parse_error(mrb_state *mrb, mrb_value)
{
    mrb_int problem = 0;
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_int offset = 0;
    mrb_get_args(mrb, "isi", &problem, &text, &length, &offset);
    const std::string_view whole(text, static_cast<size_t>(length));
    const http::ParseError error(
        http::Refusal{static_cast<uint16_t>(problem), static_cast<uint32_t>(offset)}, whole);
    mrb_value out[8] = {
        cpp_to_mrb_value(mrb, error.section()),    cpp_to_mrb_value(mrb, error.rule()),
        cpp_to_mrb_value(mrb, error.title()),      cpp_to_mrb_value(mrb, error.allowed()),
        cpp_to_mrb_value(mrb, error.status()),     cpp_to_mrb_value(mrb, error.offset()),
        cpp_to_mrb_value(mrb, error.found_byte()), cpp_to_mrb_value(mrb, error.excerpt()),
    };
    return mrb_ary_new_from_values(mrb, 8, out);
}

mrb_value spec_parse_quoted_string(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got =
        http::parse_quoted_string(whole);
    if (got.has_value())
        return cpp_to_mrb_value(mrb, *got);
    mrb_value out[3] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).found_byte()),
    };
    return mrb_ary_new_from_values(mrb, 3, out);
}

mrb_value spec_parse_field_value_parameter(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got =
        http::parse_field_value_parameter(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    if (!*got)
        return mrb_nil_value();
    mrb_value out[3] = {
        cpp_to_mrb_value(mrb, (*got)->name),
        cpp_to_mrb_value(mrb, (*got)->value),
        cpp_to_mrb_value(mrb, (*got)->rest),
    };
    return mrb_ary_new_from_values(mrb, 3, out);
}

mrb_value spec_parse_imf_fixdate(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_imf_fixdate(whole);
    if (got.has_value())
        return cpp_to_mrb_value(mrb, got->time_since_epoch().count());
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_parse_rfc850_date(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_int current_year = 0;
    mrb_get_args(mrb, "si", &text, &length, &current_year);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_rfc850_date(whole,
                                             std::chrono::year{static_cast<int>(current_year)});
    if (got.has_value())
        return cpp_to_mrb_value(mrb, got->time_since_epoch().count());
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_parse_asctime_date(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_asctime_date(whole);
    if (got.has_value())
        return cpp_to_mrb_value(mrb, got->time_since_epoch().count());
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_parse_http_date(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_int current_year = 0;
    mrb_get_args(mrb, "si", &text, &length, &current_year);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_http_date(whole,
                                           std::chrono::year{static_cast<int>(current_year)});
    if (got.has_value())
        return cpp_to_mrb_value(mrb, got->time_since_epoch().count());
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).offset()),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

} // namespace

inline void http_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecHttp");
    mrb_define_module_function(mrb, sp, "tchar?", spec_is_tchar, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_error", spec_parse_error, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "parse_quoted_string", spec_parse_quoted_string,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_field_value_parameter",
                               spec_parse_field_value_parameter, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_imf_fixdate", spec_parse_imf_fixdate,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_rfc850_date", spec_parse_rfc850_date,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "parse_asctime_date", spec_parse_asctime_date,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_http_date", spec_parse_http_date,
                               MRB_ARGS_REQ(2));
}

#endif
