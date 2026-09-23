#pragma once

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include <optional>
#include <string>
#include <string_view>

#include "../src/problem.hpp"

namespace
{

std::optional<std::string> spec_optional_string(mrb_state *mrb, const mrb_value v)
{
    if (mrb_nil_p(v))
        return std::nullopt;
    const mrb_value s = mrb_obj_as_string(mrb, v);
    return std::string(RSTRING_PTR(s), static_cast<size_t>(RSTRING_LEN(s)));
}

problem::Details spec_details_of(mrb_state *mrb, const mrb_int status, const mrb_value hash)
{
    problem::Details d = problem::details_of(static_cast<uint16_t>(status));
    const auto field = [&](const char *const name) {
        return mrb_hash_get(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, name)));
    };
    d.instance = spec_optional_string(mrb, field("instance"));
#if defined(MRB_DEBUG)
    d.exception = spec_optional_string(mrb, field("exception"));
    d.message = spec_optional_string(mrb, field("message"));
    const mrb_value lines = field("backtrace");
    if (mrb_array_p(lines)) {
        for (mrb_int i = 0; i < RARRAY_LEN(lines); i++)
            d.backtrace.push_back(*spec_optional_string(mrb, mrb_ary_entry(lines, i)));
    }
#endif
    return d;
}

mrb_value spec_problem_form(mrb_state *mrb, mrb_value)
{
    mrb_sym form = 0;
    mrb_int status = 0;
    mrb_value hash = mrb_nil_value();
    mrb_get_args(mrb, "niH", &form, &status, &hash);
    const problem::Details d = spec_details_of(mrb, status, hash);
    const std::string_view name = mrb_sym_name(mrb, form);
    std::optional<std::string> page;
    if (name == "html")
        page = problem::html_of(d);
    else if (name == "json")
        page = problem::json_of(d);
    else if (name == "xml")
        page = problem::xml_of(d);
    else if (name == "text")
        page = problem::text_of(d);
    else
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "no form %n", form);
    if (!page)
        return mrb_nil_value();
    return mrb_str_new(mrb, page->data(), static_cast<mrb_int>(page->size()));
}

mrb_value spec_problem_debug(mrb_state *, mrb_value)
{
#if defined(MRB_DEBUG)
    return mrb_true_value();
#else
    return mrb_false_value();
#endif
}

} // namespace

inline void problem_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecProblem");
    mrb_define_module_function(mrb, sp, "form", spec_problem_form, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "debug?", spec_problem_debug, MRB_ARGS_NONE());
}
