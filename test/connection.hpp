#pragma once

#include <mruby.h>
#include <mruby/array.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "../src/connection.hpp"
#include "../src/ring.hpp"

namespace
{

constexpr std::array<std::string_view, tcp::kStates> kTcpNames = {
    "closed", "established", "fin_wait_1", "close_wait", "last_ack"};

constexpr std::array<std::string_view, http1::kStates> kHttpNames = {
    "header_section", "message_body", "dispatch",     "suspended",
    "response",       "persistent",   "client_error", "tear_down"};

template <class State, size_t kCount>
State spec_state_of(mrb_state *mrb, const std::array<std::string_view, kCount> &names,
                    const mrb_sym name)
{
    mrb_int length = 0;
    const char *const at = mrb_sym_name_len(mrb, name, &length);
    const std::string_view wanted(at, static_cast<size_t>(length));
    for (size_t i = 0; i < names.size(); i++)
        if (names.at(i) == wanted)
            return static_cast<State>(i);
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "no state %n", name);
    return State{};
}

mrb_value spec_symbols_of(mrb_state *mrb, const auto &names)
{
    const mrb_value all = mrb_ary_new(mrb);
    for (const std::string_view name : names)
        mrb_ary_push(mrb, all, mrb_symbol_value(mrb_intern(mrb, name.data(), name.size())));
    return all;
}

mrb_value spec_tcp_states(mrb_state *mrb, mrb_value)
{
    return spec_symbols_of(mrb, kTcpNames);
}

mrb_value spec_http_states(mrb_state *mrb, mrb_value)
{
    return spec_symbols_of(mrb, kHttpNames);
}

mrb_value spec_tcp_may_move(mrb_state *mrb, mrb_value)
{
    mrb_sym from = 0;
    mrb_sym to = 0;
    mrb_get_args(mrb, "nn", &from, &to);
    return mrb_bool_value(tcp::may_move(spec_state_of<tcp::State>(mrb, kTcpNames, from),
                                        spec_state_of<tcp::State>(mrb, kTcpNames, to)));
}

mrb_value spec_http_may_move(mrb_state *mrb, mrb_value)
{
    mrb_sym from = 0;
    mrb_sym to = 0;
    mrb_get_args(mrb, "nn", &from, &to);
    return mrb_bool_value(http1::may_move(spec_state_of<http1::State>(mrb, kHttpNames, from),
                                          spec_state_of<http1::State>(mrb, kHttpNames, to)));
}

mrb_value spec_tcp_moves(mrb_state *mrb, mrb_value)
{
    mrb_sym from = 0;
    mrb_sym to = 0;
    mrb_get_args(mrb, "nn", &from, &to);
    wm::Connection connection{spec_state_of<tcp::State>(mrb, kTcpNames, from),
                              http1::State::header_section, 0};
    try {
        connection.moves_to(spec_state_of<tcp::State>(mrb, kTcpNames, to));
    } catch (const std::logic_error &) {
        return mrb_false_value();
    }
    return mrb_true_value();
}

mrb_value spec_http_moves(mrb_state *mrb, mrb_value)
{
    mrb_sym from = 0;
    mrb_sym to = 0;
    mrb_get_args(mrb, "nn", &from, &to);
    wm::Connection connection{tcp::State::established,
                              spec_state_of<http1::State>(mrb, kHttpNames, from), 0};
    try {
        connection.moves_to(spec_state_of<http1::State>(mrb, kHttpNames, to));
    } catch (const std::logic_error &) {
        return mrb_false_value();
    }
    return mrb_true_value();
}

mrb_value spec_state_after_close(mrb_state *mrb, mrb_value)
{
    mrb_sym now = 0;
    mrb_int result = 0;
    mrb_get_args(mrb, "ni", &now, &result);
    const tcp::State after = tcp::state_after_close(spec_state_of<tcp::State>(mrb, kTcpNames, now),
                                                    static_cast<int>(result));
    const std::string_view name = kTcpNames.at(static_cast<size_t>(after));
    return mrb_symbol_value(mrb_intern(mrb, name.data(), name.size()));
}

mrb_value spec_errno(mrb_state *mrb, mrb_value)
{
    mrb_sym name = 0;
    mrb_get_args(mrb, "n", &name);
    mrb_int length = 0;
    const char *const at = mrb_sym_name_len(mrb, name, &length);
    const std::string_view named(at, static_cast<size_t>(length));
    if (named == "ECONNRESET")
        return mrb_fixnum_value(ECONNRESET);
    if (named == "EPIPE")
        return mrb_fixnum_value(EPIPE);
    if (named == "EIO")
        return mrb_fixnum_value(EIO);
    if (named == "ENOMEM")
        return mrb_fixnum_value(ENOMEM);
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "no errno %n", name);
    return mrb_nil_value();
}

mrb_value spec_accepted_after(mrb_state *mrb, mrb_value)
{
    mrb_int generation = 0;
    mrb_get_args(mrb, "i", &generation);
    const wm::Connection before{tcp::State::closed, http1::State::tear_down,
                                static_cast<uint32_t>(generation)};
    const wm::Connection after = wm::accepted_after(before);
    const mrb_value all = mrb_ary_new(mrb);
    mrb_ary_push(mrb, all,
                 mrb_symbol_value(
                     mrb_intern_cstr(mrb, kTcpNames.at(static_cast<size_t>(after.tcp)).data())));
    mrb_ary_push(mrb, all,
                 mrb_symbol_value(
                     mrb_intern_cstr(mrb, kHttpNames.at(static_cast<size_t>(after.http)).data())));
    mrb_ary_push(mrb, all, mrb_int_value(mrb, after.generation));
    return all;
}

mrb_value spec_mark(mrb_state *mrb, mrb_value)
{
    mrb_int doing = 0;
    mrb_int slot = 0;
    mrb_int generation = 0;
    mrb_get_args(mrb, "iii", &doing, &slot, &generation);
    const uint64_t mark = wm::marked(static_cast<wm::Doing>(doing), static_cast<uint32_t>(slot),
                                     static_cast<uint32_t>(generation));
    const mrb_value all = mrb_ary_new(mrb);
    mrb_ary_push(mrb, all, mrb_int_value(mrb, static_cast<mrb_int>(wm::doing_of(mark))));
    mrb_ary_push(mrb, all, mrb_int_value(mrb, wm::slot_of(mark)));
    mrb_ary_push(mrb, all, mrb_int_value(mrb, wm::generation_of(mark)));
    return all;
}

mrb_value spec_slots_most(mrb_state *mrb, mrb_value)
{
    return mrb_int_value(mrb, wm::kSlotsMost);
}

} // namespace

inline void connection_spec(mrb_state *mrb)
{
    struct RClass *wm = mrb_define_module(mrb, "Webmachine");
    struct RClass *sp = mrb_define_module_under(mrb, wm, "SpecConnection");
    mrb_define_module_function(mrb, sp, "tcp_states", spec_tcp_states, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "http_states", spec_http_states, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, sp, "tcp_may_move?", spec_tcp_may_move, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "http_may_move?", spec_http_may_move, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "tcp_moves?", spec_tcp_moves, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "http_moves?", spec_http_moves, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "state_after_close", spec_state_after_close,
                               MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, sp, "errno", spec_errno, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "accepted_after", spec_accepted_after, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, sp, "mark", spec_mark, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, sp, "slots_most", spec_slots_most, MRB_ARGS_NONE());
}
