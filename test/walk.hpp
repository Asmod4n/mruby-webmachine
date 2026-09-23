#pragma once

#include <mruby.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include <chrono>
#include <string>
#include <string_view>

#include "../src/walk.hpp"

namespace
{

constexpr webmachine::MediaTypeHandler kSpecTypes[] = {{"text/html", "to_html"}, {"application/json", "to_json"}};

class SpecResource : public webmachine::Resource
{
  public:
    bool exists = true;
    std::string etag;

    bool resource_exists(const http1::Request &) const override { return exists; }
    std::span<const webmachine::MediaTypeHandler> content_types_provided() const override { return kSpecTypes; }
    std::string_view generate_etag(const http1::Request &) const override { return etag; }
};

mrb_value spec_walk(mrb_state *mrb, mrb_value)
{
    const char *asked = nullptr;
    mrb_int length = 0;
    mrb_value options = mrb_nil_value();
    mrb_get_args(mrb, "s|H", &asked, &length, &options);
    SpecResource resource;
    if (mrb_hash_p(options)) {
        const mrb_value exists = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_lit(mrb, "exists")));
        if (!mrb_nil_p(exists)) resource.exists = mrb_test(exists);
        const mrb_value etag = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_lit(mrb, "etag")));
        if (mrb_string_p(etag)) resource.etag.assign(RSTRING_PTR(etag), static_cast<size_t>(RSTRING_LEN(etag)));
    }
    const std::string text(asked, static_cast<size_t>(length));
    const auto request = http1::parse_request(text);
    if (!request) return mrb_symbol_value(mrb_intern_lit(mrb, "unparsed"));
    const flow::Facts facts = flow::facts_of(*request, std::chrono::year{2026});
    const auto outcome = flow::walk(resource, *request, facts);
    if (!outcome) return mrb_symbol_value(mrb_intern_lit(mrb, "refused"));
    const mrb_value answer = mrb_ary_new(mrb);
    mrb_ary_push(mrb, answer, mrb_fixnum_value(outcome->status));
    mrb_ary_push(mrb, answer, mrb_str_new_cstr(mrb, std::string(flow::name_of(outcome->halted_at)).c_str()));
    mrb_ary_push(mrb, answer,
                 outcome->media_type ? mrb_str_new_cstr(mrb, std::string(kSpecTypes[*outcome->media_type].media_type).c_str())
                                     : mrb_nil_value());
    return answer;
}

} // namespace

inline void walk_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecWalk");
    mrb_define_module_function(mrb, sp, "walk", spec_walk, MRB_ARGS_ARG(1, 1));
}
