#pragma once

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/cpp_to_mrb_value.hpp>

#include <algorithm>
#include <vector>

#include "../src/http1.hpp"
#include "http.hpp"

namespace
{

mrb_value spec_http1_refusal(mrb_state *mrb, const http::Refusal refusal)
{
    mrb_value out[2] = {cpp_to_mrb_value(mrb, static_cast<int>(refusal.problem)),
                        cpp_to_mrb_value(mrb, refusal.offset)};
    return mrb_ary_new_from_values(mrb, 2, out);
}

mrb_value spec_http1_parse_request(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const auto got = http1::parse_request(padded.view());
    if (!got)
        return spec_http1_refusal(mrb, got.error());
    return cpp_to_mrb_value(mrb, got->bytes);
}

mrb_value spec_http1_parse_host_field(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const auto request = http1::parse_request(padded.view());
    if (!request)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "the request does not parse");
    const auto got = http1::parse_host_field(*request);
    if (!got)
        return spec_http1_refusal(mrb, got.error());
    if (!*got)
        return mrb_nil_value();
    return cpp_to_mrb_value(mrb, (*got)->uri_host);
}

mrb_value spec_http1_field_line_forms_disagree(mrb_state *mrb, mrb_value)
{
    mrb_int disagree = 0;
    std::vector<char> bytes(80 + http::kWidePadding, '\r');
    for (size_t length = 0; length <= 80; length++) {
        for (size_t at = 0; at < length; at++) {
            for (unsigned byte = 0; byte < 256; byte++) {
                std::fill(bytes.begin(), bytes.end(), 'a');
                std::fill(std::next(bytes.begin(), static_cast<std::ptrdiff_t>(length)), bytes.end(), '\r');
                bytes.at(at) = static_cast<char>(byte);
                const std::string_view lines(bytes.data(), length);
                const bool floor = http1::floor_field_lines_are_valid(lines);
                disagree += http1::field_lines_are_valid(lines) != floor;
#if defined(__AVX2__)
                disagree += http1::avx2_field_lines_are_valid(lines) != floor;
#endif
#if defined(__AVX512BW__)
                disagree += http1::avx512_field_lines_are_valid(lines) != floor;
#endif
#if defined(__ARM_NEON) && defined(__aarch64__)
                disagree += http1::neon_field_lines_are_valid(lines) != floor;
#endif
            }
        }
    }
    return mrb_fixnum_value(disagree);
}

mrb_value spec_http1_parse_message_body_length(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_int most = 0;
    mrb_get_args(mrb, "si", &text, &length, &most);
    const Padded padded(text, length);
    const auto request = http1::parse_request(padded.view());
    if (!request)
        return spec_http1_refusal(mrb, request.error());
    const auto got = http1::parse_message_body_length(*request, static_cast<uint64_t>(most));
    if (!got)
        return spec_http1_refusal(mrb, got.error());
    return cpp_to_mrb_value(mrb, *got);
}

mrb_value spec_http1_is_persistent(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const auto request = http1::parse_request(padded.view());
    if (!request)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "the request does not parse");
    return mrb_bool_value(http1::is_persistent(*request));
}

mrb_value spec_http1_expects_continue(mrb_state *mrb, mrb_value)
{
    const char *text = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &text, &length);
    const Padded padded(text, length);
    const auto request = http1::parse_request(padded.view());
    if (!request)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "the request does not parse");
    return mrb_bool_value(request->expect_continue);
}

mrb_value spec_http1_problems(mrb_state *mrb, mrb_value)
{
    const mrb_value all = mrb_hash_new(mrb);
    const auto named = [&](const char *const name, const uint16_t problem) {
        mrb_hash_set(mrb, all, mrb_symbol_value(mrb_intern_cstr(mrb, name)), mrb_fixnum_value(problem));
    };
    named("field_line", http1::kFieldLineProblem);
    named("start_line", http1::kStartLineProblem);
    named("not_whole_yet", http1::kNotWholeYet);
    named("uri_too_long", http1::kUriTooLong);
    named("request_header_fields_too_large", http1::kRequestHeaderFieldsTooLarge);
    named("host", http::kHostProblem);
    named("content_length", http::kContentLengthProblem);
    named("transfer_encoding", http1::kTransferEncodingProblem);
    named("length_required", http1::kLengthRequired);
    named("content_too_large", http1::kContentTooLarge);
    named("port", http::kPortProblem);
    return all;
}

mrb_value spec_http1_limits(mrb_state *mrb, mrb_value)
{
    mrb_value out[2] = {cpp_to_mrb_value(mrb, http1::kMostRequestLineBytes),
                        cpp_to_mrb_value(mrb, http1::kMostHeaderSectionBytes)};
    return mrb_ary_new_from_values(mrb, 2, out);
}

} // namespace

inline void http1_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecHttp1");
    mrb_define_module_function(mrb, sp, "parse_request", spec_http1_parse_request, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "parse_host_field", spec_http1_parse_host_field, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "field_line_forms_disagree", spec_http1_field_line_forms_disagree,
                               MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "parse_message_body_length", spec_http1_parse_message_body_length,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "persistent?", spec_http1_is_persistent, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "expects_continue?", spec_http1_expects_continue, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "problems", spec_http1_problems, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "limits", spec_http1_limits, MRB_ARGS_NONE());
}
