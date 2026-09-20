#pragma once

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/string.h>
#include <mruby/cpp_to_mrb_value.hpp>

#include <algorithm>
#include <variant>
#include <vector>

#include "../src/flow.hpp"
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

mrb_value spec_parse_media_type(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_media_type(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    mrb_value out[3] = {
        cpp_to_mrb_value(mrb, got->type),
        cpp_to_mrb_value(mrb, got->subtype),
        cpp_to_mrb_value(mrb, got->parameters),
    };
    return mrb_ary_new_from_values(mrb, 3, out);
}

mrb_value spec_value_of_parameter(mrb_state *mrb, mrb_value)
{
    const char *parameters = nullptr;
    const char *name = nullptr;
    mrb_int parameters_length = 0;
    mrb_int name_length = 0;
    mrb_get_args(mrb, "ss", &parameters, &parameters_length, &name, &name_length);
    const std::string_view whole(parameters, static_cast<size_t>(parameters_length));
    const auto got =
        http::value_of_parameter(whole, std::string_view(name, static_cast<size_t>(name_length)));
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    if (!*got)
        return mrb_nil_value();
    return cpp_to_mrb_value(mrb, **got);
}

mrb_value spec_unquoted_token(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return cpp_to_mrb_value(
        mrb, http::unquoted_token(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_content_coding(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return cpp_to_mrb_value(
        mrb, static_cast<int>(
                 http::content_coding(std::string_view(text, static_cast<size_t>(length)))));
}

mrb_value spec_is_language_tag(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(
        http::is_language_tag(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_parse_content_length(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_content_length(whole);
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_parse_qvalue(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_qvalue(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_weight_of(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::weight_of(whole);
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_media_type_weight(mrb_state *mrb, mrb_value)
{
    const char *accept = nullptr;
    const char *provided = nullptr;
    mrb_int accept_length = 0;
    mrb_int provided_length = 0;
    mrb_get_args(mrb, "ss", &accept, &accept_length, &provided, &provided_length);
    const std::string_view field(accept, static_cast<size_t>(accept_length));
    const auto media =
        http::parse_media_type(std::string_view(provided, static_cast<size_t>(provided_length)));
    if (!media)
        return cpp_to_mrb_value(mrb, std::string_view("the provided type does not parse"));
    const auto got = http::media_type_weight(field, *media);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), field).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_coding_weight(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    const char *coding = nullptr;
    mrb_int field_length = 0;
    mrb_int coding_length = 0;
    mrb_get_args(mrb, "ss", &field, &field_length, &coding, &coding_length);
    const Padded whole(field, field_length);
    const Padded named(coding, coding_length);
    const auto got = http::coding_weight(whole.view(), named.view());
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole.view()).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_language_weight(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    const char *tag = nullptr;
    mrb_int field_length = 0;
    mrb_int tag_length = 0;
    mrb_get_args(mrb, "ss", &field, &field_length, &tag, &tag_length);
    const std::string_view whole(field, static_cast<size_t>(field_length));
    const auto got =
        http::language_weight(whole, std::string_view(tag, static_cast<size_t>(tag_length)));
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    return cpp_to_mrb_value(mrb, *got);
}

// The provided lists arrive from Ruby, which has no padding behind its
// strings, and choose_coding asks is_token, which reads wide. So every
// one of them is copied where the wall is real.
mrb_value spec_choose_media_type(mrb_state *mrb, mrb_value)
{
    const char *accept = nullptr;
    mrb_int accept_length = 0;
    mrb_value list;
    mrb_get_args(mrb, "sA", &accept, &accept_length, &list);
    std::vector<Padded> held;
    std::vector<http::MediaType> provided;
    for (mrb_int at = 0; at < RARRAY_LEN(list); at++) {
        const mrb_value one = mrb_ary_ref(mrb, list, at);
        held.emplace_back(RSTRING_PTR(one), RSTRING_LEN(one));
    }
    for (const Padded &one : held) {
        const auto media = http::parse_media_type(one.view());
        if (!media)
            return cpp_to_mrb_value(mrb, std::string_view("a provided type does not parse"));
        provided.push_back(*media);
    }
    const auto got = http::choose_media_type(provided,
                                             std::string_view(accept,
                                                              static_cast<size_t>(accept_length)));
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), "").rule());
    if (!*got)
        return mrb_nil_value();
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, (*got)->at),
        cpp_to_mrb_value(mrb, (*got)->weight),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_choose_coding(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    mrb_int field_length = 0;
    mrb_value list;
    mrb_get_args(mrb, "sA", &field, &field_length, &list);
    const Padded whole(field, field_length);
    std::vector<Padded> held;
    std::vector<std::string_view> provided;
    for (mrb_int at = 0; at < RARRAY_LEN(list); at++) {
        const mrb_value one = mrb_ary_ref(mrb, list, at);
        held.emplace_back(RSTRING_PTR(one), RSTRING_LEN(one));
    }
    for (const Padded &one : held)
        provided.push_back(one.view());
    const auto got = http::choose_coding(provided, whole.view());
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole.view()).rule());
    if (!*got)
        return mrb_nil_value();
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, (*got)->at),
        cpp_to_mrb_value(mrb, (*got)->weight),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_choose_language(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    mrb_int field_length = 0;
    mrb_value list;
    mrb_get_args(mrb, "sA", &field, &field_length, &list);
    const std::string_view whole(field, static_cast<size_t>(field_length));
    std::vector<Padded> held;
    std::vector<std::string_view> provided;
    for (mrb_int at = 0; at < RARRAY_LEN(list); at++) {
        const mrb_value one = mrb_ary_ref(mrb, list, at);
        held.emplace_back(RSTRING_PTR(one), RSTRING_LEN(one));
    }
    for (const Padded &one : held)
        provided.push_back(one.view());
    const auto got = http::choose_language(provided, whole);
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    if (!*got)
        return mrb_nil_value();
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, (*got)->at),
        cpp_to_mrb_value(mrb, (*got)->weight),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_field_combining(mrb_state *mrb, mrb_value)
{
    const char *name = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &name, &length);
    return cpp_to_mrb_value(
        mrb, static_cast<int>(
                 http::field_combining(std::string_view(name, static_cast<size_t>(length)))));
}

mrb_value spec_parse_content_length_list(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_content_length_list(whole);
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_is_language_range(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    return mrb_bool_value(
        http::is_language_range(std::string_view(text, static_cast<size_t>(length))));
}

mrb_value spec_language_range_matches(mrb_state *mrb, mrb_value)
{
    const char *range = nullptr;
    const char *tag = nullptr;
    mrb_int range_length = 0;
    mrb_int tag_length = 0;
    mrb_get_args(mrb, "ss", &range, &range_length, &tag, &tag_length);
    return mrb_bool_value(
        http::language_range_matches(std::string_view(range, static_cast<size_t>(range_length)),
                                     std::string_view(tag, static_cast<size_t>(tag_length))));
}

mrb_value spec_parse_ranges_specifier(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const std::string_view whole = padded.view();
    const auto got = http::parse_ranges_specifier(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, got->range_unit),
        cpp_to_mrb_value(mrb, got->range_set),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

// [first_pos, last_pos] for an int-range, [nil, suffix_length] for a
// suffix-range, and the rule with the offset for a refusal.
mrb_value spec_parse_byte_range_spec(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const std::string_view whole(text, static_cast<size_t>(length));
    const auto got = http::parse_byte_range_spec(whole);
    if (!got) {
        mrb_value out[2] = {
            cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule()),
            cpp_to_mrb_value(mrb, got.error().offset),
        };
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    if (const http::SuffixRange *const suffix = std::get_if<http::SuffixRange>(&*got)) {
        mrb_value out[2] = {mrb_nil_value(), cpp_to_mrb_value(mrb, suffix->suffix_length)};
        return mrb_ary_new_from_values(mrb, 2, out);
    }
    const http::IntRange &range = std::get<http::IntRange>(*got);
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, range.first_pos),
        range.last_pos ? cpp_to_mrb_value(mrb, *range.last_pos) : mrb_nil_value(),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_resolved_range(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_int complete_length = 0;
    mrb_get_args(mrb, "si", &text, &length, &complete_length);
    const auto spec = http::parse_byte_range_spec(std::string_view(text, static_cast<size_t>(length)));
    if (!spec)
        return mrb_nil_value();
    const auto got = http::resolved_range(*spec, static_cast<uint64_t>(complete_length));
    if (!got)
        return mrb_false_value();
    mrb_value out[2] = {
        cpp_to_mrb_value(mrb, got->first_pos),
        cpp_to_mrb_value(mrb, got->last_pos),
    };
    return mrb_ary_new_from_values(mrb, 2, out);
}

// The table as data: a name, its callback, its clause and its two
// targets. A target is the name of a node, or "" and a status where the
// walk ends.
mrb_value spec_flow_node(mrb_state *mrb, mrb_value)
{
    const char *want = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &want, &length);
    const std::string_view name(want, static_cast<size_t>(length));
    for (size_t at = 0; at < flow::kFlow.size(); at++) {
        const flow::FlowNode &node = flow::kFlow.at(at);
        if (flow::name_of(node.id) != name)
            continue;
        mrb_value out[6] = {
            cpp_to_mrb_value(mrb, node.callback == nullptr ? "" : node.callback),
            cpp_to_mrb_value(mrb, node.clause),
            cpp_to_mrb_value(mrb, flow::name_of(node.on_true.node)),
            cpp_to_mrb_value(mrb, node.on_true.status),
            cpp_to_mrb_value(mrb, flow::name_of(node.on_false.node)),
            cpp_to_mrb_value(mrb, node.on_false.status),
        };
        return mrb_ary_new_from_values(mrb, 6, out);
    }
    return mrb_nil_value();
}

mrb_value spec_flow_names(mrb_state *mrb, mrb_value)
{
    const mrb_value out = mrb_ary_new_capa(mrb, static_cast<mrb_int>(flow::kFlow.size()));
    for (const flow::FlowNode &node : flow::kFlow)
        mrb_ary_push(mrb, out, cpp_to_mrb_value(mrb, flow::name_of(node.id)));
    return out;
}

std::optional<http::EntityTag> spec_selected_tag(const mrb_value tag)
{
    if (mrb_nil_p(tag))
        return std::nullopt;
    const auto got = http::parse_entity_tag(
        std::string_view(RSTRING_PTR(tag), static_cast<size_t>(RSTRING_LEN(tag))));
    if (!got)
        return std::nullopt;
    return *got;
}

mrb_value spec_if_match_passes(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    mrb_int length = 0;
    mrb_bool exists = FALSE;
    mrb_value tag = mrb_nil_value();
    mrb_get_args(mrb, "sbo", &field, &length, &exists, &tag);
    const std::string_view whole(field, static_cast<size_t>(length));
    const auto got = http::if_match_passes(whole, exists != FALSE, spec_selected_tag(tag));
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return mrb_bool_value(*got);
}

mrb_value spec_if_none_match_passes(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    mrb_int length = 0;
    mrb_bool exists = FALSE;
    mrb_value tag = mrb_nil_value();
    mrb_get_args(mrb, "sbo", &field, &length, &exists, &tag);
    const std::string_view whole(field, static_cast<size_t>(length));
    const auto got = http::if_none_match_passes(whole, exists != FALSE, spec_selected_tag(tag));
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return mrb_bool_value(*got);
}

std::optional<std::chrono::sys_seconds> spec_moment(const mrb_value seconds)
{
    if (mrb_nil_p(seconds))
        return std::nullopt;
    return std::chrono::sys_seconds{std::chrono::seconds{mrb_integer(seconds)}};
}

mrb_value spec_if_modified_since_passes(mrb_state *mrb, mrb_value)
{
    mrb_int since = 0;
    mrb_value last_modified = mrb_nil_value();
    mrb_get_args(mrb, "io", &since, &last_modified);
    return mrb_bool_value(http::if_modified_since_passes(
        std::chrono::sys_seconds{std::chrono::seconds{since}}, spec_moment(last_modified)));
}

mrb_value spec_if_unmodified_since_passes(mrb_state *mrb, mrb_value)
{
    mrb_int since = 0;
    mrb_value last_modified = mrb_nil_value();
    mrb_get_args(mrb, "io", &since, &last_modified);
    return mrb_bool_value(http::if_unmodified_since_passes(
        std::chrono::sys_seconds{std::chrono::seconds{since}}, spec_moment(last_modified)));
}

mrb_value spec_if_range_passes(mrb_state *mrb, mrb_value)
{
    const char *field = nullptr;
    mrb_int length = 0;
    mrb_value tag = mrb_nil_value();
    mrb_value last_modified = mrb_nil_value();
    mrb_get_args(mrb, "soo", &field, &length, &tag, &last_modified);
    const std::string_view whole(field, static_cast<size_t>(length));
    const auto got = http::if_range_passes(whole, spec_selected_tag(tag),
                                           spec_moment(last_modified), std::chrono::year{2026});
    if (!got)
        return cpp_to_mrb_value(mrb, http::ParseError(got.error(), whole).rule());
    return mrb_bool_value(*got);
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
    mrb_define_module_function(mrb, sp, "parse_media_type", spec_parse_media_type,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "value_of_parameter", spec_value_of_parameter,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "unquoted_token", spec_unquoted_token,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "content_coding", spec_content_coding,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "language_tag?", spec_is_language_tag,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_content_length",
                               spec_parse_content_length, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_qvalue", spec_parse_qvalue,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "weight_of", spec_weight_of, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "media_type_weight", spec_media_type_weight,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "field_combining", spec_field_combining,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_content_length_list",
                               spec_parse_content_length_list, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "coding_weight", spec_coding_weight, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "choose_media_type", spec_choose_media_type,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "choose_coding", spec_choose_coding, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "choose_language", spec_choose_language,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "language_weight", spec_language_weight,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "language_range?", spec_is_language_range,
                               MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "language_range_matches?", spec_language_range_matches,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "parse_ranges_specifier",
                               spec_parse_ranges_specifier, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_byte_range_spec",
                               spec_parse_byte_range_spec, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "resolved_range", spec_resolved_range,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "flow_node", spec_flow_node, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "flow_names", spec_flow_names, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "if_match_passes", spec_if_match_passes,
                               MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "if_none_match_passes",
                               spec_if_none_match_passes, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "if_modified_since_passes",
                               spec_if_modified_since_passes, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "if_unmodified_since_passes",
                               spec_if_unmodified_since_passes, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "if_range_passes", spec_if_range_passes,
                               MRB_ARGS_REQ(3));
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
