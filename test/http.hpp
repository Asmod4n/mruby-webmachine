#ifndef WEBMACHINE_TEST_HTTP_HPP
#define WEBMACHINE_TEST_HTTP_HPP

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/cpp_to_mrb_value.hpp>

#include <algorithm>
#include <vector>

#include "../src/http.hpp"

namespace
{

// A wide read goes up to kWidePadding bytes past the run it was given.
// In the server that is the ring's guard buffer, and the server parses
// nothing else. A test hands over an mruby string, which has nothing
// behind it, so the bytes are copied where the padding is real and the
// sanitizer can see the wall.
class Padded
{
public:
    Padded(const char *from, const mrb_int size)
        : bytes_(static_cast<size_t>(size) + http::kWidePadding, '\0'),
          length_(static_cast<size_t>(size))
    {
        std::copy_n(from, length_, bytes_.begin());
    }

    std::string_view view() const { return {bytes_.data(), length_}; }

private:
    std::vector<char> bytes_;
    size_t length_;
};

mrb_value spec_is_tchar(mrb_state *mrb, mrb_value)
{
    mrb_int byte = 0;
    mrb_get_args(mrb, "i", &byte);
    return mrb_bool_value(http::is_tchar(static_cast<char>(byte)));
}

mrb_value spec_ascii_lowered(mrb_state *mrb, mrb_value)
{
    mrb_int byte = 0;
    mrb_get_args(mrb, "i", &byte);
    return cpp_to_mrb_value(
        mrb, static_cast<unsigned char>(http::ascii_lowered(static_cast<char>(byte))));
}

mrb_value spec_equal_ignoring_case(mrb_state *mrb, mrb_value)
{
    const char *left = nullptr;
    const char *right = nullptr;
    mrb_int left_length = 0;
    mrb_int right_length = 0;
    mrb_get_args(mrb, "ss", &left, &left_length, &right, &right_length);
    return mrb_bool_value(http::equal_ignoring_case(
        std::string_view(left, static_cast<size_t>(left_length)),
        std::string_view(right, static_cast<size_t>(right_length))));
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
        cpp_to_mrb_value(mrb, got.error().offset),
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).found_byte()),
    };
    return mrb_ary_new_from_values(mrb, 3, out);
}

mrb_value spec_parse_list_element(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const auto got = http::parse_list_element(std::string_view(text, static_cast<size_t>(length)));
    if (!got)
        return mrb_nil_value();
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, got->element),
        cpp_to_mrb_value(mrb, got->rest),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
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
            cpp_to_mrb_value(mrb, got.error().offset),
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
        cpp_to_mrb_value(mrb, got.error().offset),
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
        cpp_to_mrb_value(mrb, got.error().offset),
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
        cpp_to_mrb_value(mrb, got.error().offset),
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
        cpp_to_mrb_value(mrb, got.error().offset),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_is_token(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(http::is_token(Padded(text, length).view()));
}

mrb_value spec_is_lowercase_token(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(http::is_lowercase_token(Padded(text, length).view()));
}

// The scalar overload, so a test can hold the two ways against each
// other. The server never calls it where a wide read is possible.
mrb_value spec_is_token_narrow(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(http::every_byte_is_allowed(
        std::string_view(text, static_cast<size_t>(length)), http::kTchar));
}

mrb_value spec_is_reg_name_narrow(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(http::every_byte_is_allowed(
        std::string_view(text, static_cast<size_t>(length)), http::kRegName));
}

mrb_value spec_is_reg_name(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(http::is_reg_name(Padded(text, length).view()));
}

mrb_value spec_method_number(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return cpp_to_mrb_value(
        mrb, http::method_number(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_request_target_form(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    const char *method = nullptr;
    mrb_int length = 0;
    mrb_int method_length = 0;
    mrb_get_args(mrb, "ss", &text, &length, &method, &method_length);
    const auto got = http::request_target_form(
        std::string_view(text, static_cast<size_t>(length)),
        http::method_number(std::string_view(method, static_cast<size_t>(method_length))));
    if (!got)
        return mrb_nil_value();
    return cpp_to_mrb_value(mrb, static_cast<int>(*got));
}

mrb_value spec_parse_host(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const std::string_view whole = padded.view();
    const auto got = http::parse_host(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, got->uri_host),
        got->port ? cpp_to_mrb_value(mrb, *got->port) : mrb_nil_value(),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_parse_request_target(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    const char *method = nullptr;
    mrb_int length = 0;
    mrb_int method_length = 0;
    mrb_get_args(mrb, "ss", &text, &length, &method, &method_length);
    const Padded padded(text, length);
    const std::string_view whole = padded.view();
    const auto got = http::parse_request_target(
        whole, http::method_number(std::string_view(method, static_cast<size_t>(method_length))));
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    mrb_value out[6] = {
        cpp_to_mrb_value(mrb, static_cast<int>(got->form)),
        cpp_to_mrb_value(mrb, got->scheme),
        cpp_to_mrb_value(mrb, got->authority.uri_host),
        got->authority.port ? cpp_to_mrb_value(mrb, *got->authority.port) : mrb_nil_value(),
        cpp_to_mrb_value(mrb, got->path),
        cpp_to_mrb_value(mrb, got->query),
    };
    return mrb_ary_new_from_values(mrb, 6, out);
}

mrb_value spec_next_segment(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const http::PathWalk walk =
        http::next_segment(std::string_view(text, static_cast<size_t>(length)));
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, walk.segment),
        cpp_to_mrb_value(mrb, walk.rest),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_path_has_dot_segment(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(
        http::path_has_dot_segment(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_remove_dot_segments(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return cpp_to_mrb_value(
        mrb, http::remove_dot_segments(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_percent_decode(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::percent_decode(whole);
    if (got.has_value())
        return cpp_to_mrb_value(mrb, *got);
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
        cpp_to_mrb_value(mrb, got.error().offset),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_parse_entity_tag(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_entity_tag(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, got->opaque_tag),
        mrb_bool_value(got->weak),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_strong_comparison(mrb_state *mrb, mrb_value)
{
    const char *left = nullptr;
    const char *right = nullptr;
    mrb_int left_length = 0;
    mrb_int right_length = 0;
    mrb_get_args(mrb, "ss", &left, &left_length, &right, &right_length);
    const auto one = http::parse_entity_tag(std::string_view(left, static_cast<size_t>(left_length)));
    const auto other =
        http::parse_entity_tag(std::string_view(right, static_cast<size_t>(right_length)));
    if (!one || !other)
        return mrb_nil_value();
    return mrb_bool_value(http::strong_comparison(*one, *other));
}

mrb_value spec_weak_comparison(mrb_state *mrb, mrb_value)
{
    const char *left = nullptr;
    const char *right = nullptr;
    mrb_int left_length = 0;
    mrb_int right_length = 0;
    mrb_get_args(mrb, "ss", &left, &left_length, &right, &right_length);
    const auto one = http::parse_entity_tag(std::string_view(left, static_cast<size_t>(left_length)));
    const auto other =
        http::parse_entity_tag(std::string_view(right, static_cast<size_t>(right_length)));
    if (!one || !other)
        return mrb_nil_value();
    return mrb_bool_value(http::weak_comparison(*one, *other));
}

} // namespace

inline void http_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecHttp");
    mrb_define_module_function(mrb, sp, "tchar?", spec_is_tchar, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "ascii_lowered", spec_ascii_lowered, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "equal_ignoring_case", spec_equal_ignoring_case,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "token?", spec_is_token, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "lowercase_token?", spec_is_lowercase_token,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "reg_name?", spec_is_reg_name, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "token_narrow?", spec_is_token_narrow, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "reg_name_narrow?", spec_is_reg_name_narrow,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "method_number", spec_method_number, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "request_target_form", spec_request_target_form,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "parse_request_target", spec_parse_request_target,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "parse_host", spec_parse_host, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "next_segment", spec_next_segment, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "path_has_dot_segment?", spec_path_has_dot_segment,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "remove_dot_segments", spec_remove_dot_segments,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "percent_decode", spec_percent_decode,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_entity_tag", spec_parse_entity_tag,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "strong_comparison", spec_strong_comparison,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "weak_comparison", spec_weak_comparison,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "parse_error", spec_parse_error, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "parse_quoted_string", spec_parse_quoted_string,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_list_element", spec_parse_list_element,
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
