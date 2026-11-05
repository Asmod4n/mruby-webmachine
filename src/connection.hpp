// The state of one connection: the TCP state that a server process can
// observe, after RFC 9293 3.3.2 (https://www.rfc-editor.org/rfc/rfc9293),
// and inside it the HTTP/1.1 state, after RFC 9112
// (https://www.rfc-editor.org/rfc/rfc9112). A table holds for each state
// the states it may move to, and a move that the table refuses throws.
#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace tcp
{

enum class State : uint8_t {
    closed,
    established,
    fin_wait_1,
    close_wait,
    last_ack,
};

inline constexpr size_t kStates = 5;

inline constexpr uint8_t bit_of(const State state)
{
    return static_cast<uint8_t>(1u << static_cast<unsigned>(state));
}

inline constexpr std::array<uint8_t, kStates> kMovesTo = {
    0,
    bit_of(State::close_wait) | bit_of(State::fin_wait_1) | bit_of(State::closed),
    bit_of(State::closed),
    bit_of(State::last_ack) | bit_of(State::closed),
    bit_of(State::closed),
};

inline constexpr bool may_move(const State from, const State to)
{
    return (kMovesTo.at(static_cast<size_t>(from)) & bit_of(to)) != 0;
}

inline constexpr State state_after_close(const State now, const int result)
{
    if (result == -ECONNRESET || result == -EPIPE || result == -ETIMEDOUT)
        return State::closed;
    return now == State::close_wait ? State::last_ack : State::fin_wait_1;
}

} // namespace tcp

namespace http1
{

enum class State : uint8_t {
    header_section,
    message_body,
    dispatch,
    suspended,
    response,
    persistent,
    client_error,
    tear_down,
};

inline constexpr size_t kStates = 8;

inline constexpr uint8_t bit_of(const State state)
{
    return static_cast<uint8_t>(1u << static_cast<unsigned>(state));
}

inline constexpr std::array<uint8_t, kStates> kMovesTo = {
    bit_of(State::message_body) | bit_of(State::dispatch) | bit_of(State::client_error),
    bit_of(State::dispatch) | bit_of(State::client_error),
    bit_of(State::suspended) | bit_of(State::response),
    bit_of(State::response),
    bit_of(State::persistent) | bit_of(State::tear_down),
    bit_of(State::header_section) | bit_of(State::tear_down),
    bit_of(State::tear_down),
    0,
};

inline constexpr bool may_move(const State from, const State to)
{
    return (kMovesTo.at(static_cast<size_t>(from)) & bit_of(to)) != 0;
}

} // namespace http1

namespace wm
{

struct Connection {
    tcp::State tcp;
    http1::State http;
    uint32_t generation;

    constexpr void moves_to(const tcp::State to)
    {
        if (!tcp::may_move(tcp, to)) [[unlikely]]
            throw std::logic_error("a connection took a TCP move that RFC 9293 does not allow");
        tcp = to;
    }

    constexpr void moves_to(const http1::State to)
    {
        if (!http1::may_move(http, to)) [[unlikely]]
            throw std::logic_error("a connection took an HTTP move that this tree does not allow");
        http = to;
    }

    constexpr bool reads() const
    {
        return tcp == tcp::State::established;
    }

    constexpr bool drains() const
    {
        return tcp == tcp::State::fin_wait_1;
    }

    constexpr bool is_open() const
    {
        return tcp == tcp::State::established || tcp == tcp::State::close_wait;
    }
};

inline constexpr Connection accepted_after(const Connection before)
{
    return {tcp::State::established, http1::State::header_section, before.generation + 1};
}

} // namespace wm
