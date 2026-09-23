#pragma once

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../src/fingerprint.hpp"
#include "../src/problem.hpp"
#include "../src/zip.hpp"

namespace
{

std::optional<std::string> spec_optional_string(mrb_state *mrb, const mrb_value v)
{
    if (mrb_nil_p(v))
        return std::nullopt;
    const mrb_value s = mrb_obj_as_string(mrb, v);
    return std::string(RSTRING_PTR(s), static_cast<size_t>(RSTRING_LEN(s)));
}

mrb_value field_of(mrb_state *mrb, const mrb_value hash, const char *const name)
{
    return mrb_hash_get(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, name)));
}

problem::Details spec_details_of(mrb_state *mrb, const mrb_int status, const mrb_value hash)
{
    problem::Details d = problem::details_of(static_cast<uint16_t>(status));
    const auto field = [&](const char *const name) {
        return mrb_hash_get(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, name)));
    };
    d.instance = spec_optional_string(mrb, field("instance"));
    d.fingerprint = spec_optional_string(mrb, field("fingerprint"));
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
    const std::optional<std::string> picture = spec_optional_string(mrb, field_of(mrb, hash, "picture"));
    if (name == "html")
        page = problem::html_of(d, picture ? std::optional<std::string_view>(*picture) : std::nullopt);
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

std::string spec_string_of(mrb_state *mrb, const mrb_value v)
{
    const mrb_value s = mrb_obj_as_string(mrb, v);
    return std::string(RSTRING_PTR(s), static_cast<size_t>(RSTRING_LEN(s)));
}

mrb_value spec_problem_instance(mrb_state *mrb, mrb_value)
{
    const char *bytes = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &bytes, &length);
    if (length != 16)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "16 bytes");
    const std::span<const std::byte, 16> random(reinterpret_cast<const std::byte *>(bytes), 16);
    const std::string urn = problem::instance_of(random);
    return mrb_str_new(mrb, urn.data(), static_cast<mrb_int>(urn.size()));
}

mrb_value spec_problem_form_of(mrb_state *mrb, mrb_value)
{
    mrb_value accept = mrb_nil_value();
    mrb_bool has_picture = false;
    mrb_get_args(mrb, "ob", &accept, &has_picture);
    const std::optional<std::string> text = spec_optional_string(mrb, accept);
    const auto form = problem::form_of(text ? std::optional<std::string_view>(*text) : std::nullopt, has_picture);
    if (!form)
        return mrb_symbol_value(mrb_intern_lit(mrb, "refused"));
    const std::string_view media_type = problem::media_type_of(*form);
    return mrb_str_new(mrb, media_type.data(), static_cast<mrb_int>(media_type.size()));
}

std::vector<std::byte> spec_pack_bytes()
{
    const std::string path = std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/')) +
                             "/../share/error-assets.zip";
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(chars.size());
    for (size_t i = 0; i < chars.size(); i++)
        bytes.at(i) = static_cast<std::byte>(chars.at(i));
    return bytes;
}

mrb_value spec_zip_entries(mrb_state *mrb, mrb_value)
{
    mrb_int flip = -1;
    mrb_int cut = 0;
    mrb_get_args(mrb, "|ii", &flip, &cut);
    std::vector<std::byte> bytes = spec_pack_bytes();
    if (flip >= 0)
        bytes.at(static_cast<size_t>(flip)) ^= std::byte{0x01};
    bytes.resize(bytes.size() - static_cast<size_t>(cut));
    const auto entries = zip::entries_of(bytes);
    if (!entries) {
        std::string title(zip::kProblemTitles.at(static_cast<size_t>(entries.error().problem)));
        if (entries.error().problem == zip::Problem::kReader)
            title += std::string(": ") + mz_zip_get_error_string(entries.error().reader_error);
        return mrb_str_new(mrb, title.data(), static_cast<mrb_int>(title.size()));
    }
    const mrb_value names = mrb_ary_new(mrb);
    for (const zip::Entry &entry : *entries) {
        const mrb_value pair = mrb_ary_new(mrb);
        mrb_ary_push(mrb, pair, mrb_str_new(mrb, entry.name.data(), static_cast<mrb_int>(entry.name.size())));
        mrb_ary_push(mrb, pair,
                     mrb_str_new(mrb, reinterpret_cast<const char *>(entry.data.data()),
                                 static_cast<mrb_int>(std::min<size_t>(entry.data.size(), 3))));
        mrb_ary_push(mrb, names, pair);
    }
    return names;
}

mrb_value spec_problem_picture(mrb_state *mrb, mrb_value)
{
    mrb_int status = 0;
    mrb_get_args(mrb, "i", &status);
    const std::vector<std::byte> bytes = spec_pack_bytes();
    const auto entries = zip::entries_of(bytes);
    if (!entries)
        return mrb_nil_value();
    const auto picture = problem::picture_of(*entries, static_cast<uint16_t>(status));
    if (!picture)
        return mrb_nil_value();
    return mrb_str_new(mrb, picture->img.data(), static_cast<mrb_int>(picture->img.size()));
}

mrb_value spec_fingerprint_key_valid(mrb_state *mrb, mrb_value)
{
    const char *hex = nullptr;
    mrb_int length = 0;
    mrb_get_args(mrb, "s", &hex, &length);
    return mrb_bool_value(fingerprint::key_of(std::string_view(hex, static_cast<size_t>(length))).has_value());
}

mrb_value spec_fingerprint_of(mrb_state *mrb, mrb_value)
{
    mrb_value key_hex, method, target, callback, exception, lines;
    mrb_int status = 0;
    mrb_get_args(mrb, "SSSSSiA", &key_hex, &method, &target, &callback, &exception, &status, &lines);
    const auto key = fingerprint::key_of(spec_string_of(mrb, key_hex));
    if (!key)
        return mrb_nil_value();
    std::vector<std::string> kept;
    for (mrb_int i = 0; i < RARRAY_LEN(lines); i++)
        kept.push_back(spec_string_of(mrb, mrb_ary_entry(lines, i)));
    std::vector<std::string_view> backtrace(kept.begin(), kept.end());
    const std::string m = spec_string_of(mrb, method), t = spec_string_of(mrb, target),
                      c = spec_string_of(mrb, callback), e = spec_string_of(mrb, exception);
    const fingerprint::Facts facts{fingerprint::Digest{}, m, t, c, e, backtrace, static_cast<uint16_t>(status)};
    const auto digest = fingerprint::fingerprint_of(*key, facts);
    if (!digest)
        return mrb_nil_value();
    const std::string hex = fingerprint::hex_of(*digest);
    return mrb_str_new(mrb, hex.data(), static_cast<mrb_int>(hex.size()));
}

mrb_value spec_build_of(mrb_state *mrb, mrb_value)
{
    mrb_value key_hex, bytecode;
    mrb_get_args(mrb, "SS", &key_hex, &bytecode);
    const auto key = fingerprint::key_of(spec_string_of(mrb, key_hex));
    if (!key)
        return mrb_nil_value();
    const std::string b = spec_string_of(mrb, bytecode);
    const auto digest = fingerprint::build_of(*key, std::as_bytes(std::span(b)));
    if (!digest)
        return mrb_nil_value();
    const std::string hex = fingerprint::hex_of(*digest);
    return mrb_str_new(mrb, hex.data(), static_cast<mrb_int>(hex.size()));
}

} // namespace

inline void problem_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecProblem");
    mrb_define_module_function(mrb, sp, "form", spec_problem_form, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "debug?", spec_problem_debug, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "instance", spec_problem_instance, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "form_of", spec_problem_form_of, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "zip_entries", spec_zip_entries, MRB_ARGS_OPT(2));
    mrb_define_module_function(mrb, sp, "picture", spec_problem_picture, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "key_valid?", spec_fingerprint_key_valid, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "fingerprint", spec_fingerprint_of, MRB_ARGS_REQ(7));
    mrb_define_module_function(mrb, sp, "build", spec_build_of, MRB_ARGS_REQ(2));
}
