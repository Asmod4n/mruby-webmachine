// The state of one connection: the TCP state that a server process can
// observe, after RFC 9293 3.3.2 (https://www.rfc-editor.org/rfc/rfc9293),
// and inside it the HTTP/1.1 state, after RFC 9112
// (https://www.rfc-editor.org/rfc/rfc9112). A table holds for each state
// the states it may move to, and a move that the table refuses throws.
#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string_view>
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

struct Timeouts {
    std::chrono::seconds header;
    std::chrono::seconds body;
    std::chrono::seconds send;
    std::chrono::seconds idle;
    std::chrono::seconds linger;
};

inline constexpr Timeouts kTimeoutsByDefault = {std::chrono::seconds{60}, std::chrono::seconds{60},
                                               std::chrono::seconds{60}, std::chrono::seconds{75},
                                               std::chrono::seconds{5}};
inline constexpr std::chrono::seconds kLingerMost{30};
inline constexpr std::chrono::seconds kTimeoutMost{std::chrono::hours{24}};

inline constexpr std::optional<std::string_view> wrong_timeout_of(const Timeouts timeouts)
{
    if (timeouts.header.count() <= 0)
        return "client_header_timeout is not above 0 s";
    if (timeouts.body.count() <= 0)
        return "client_body_timeout is not above 0 s";
    if (timeouts.send.count() <= 0)
        return "send_timeout is not above 0 s";
    if (timeouts.idle.count() <= 0)
        return "keepalive_timeout is not above 0 s";
    if (timeouts.linger.count() <= 0)
        return "lingering_time is not above 0 s";
    if (timeouts.linger > kLingerMost)
        return "lingering_time is above 30 s";
    if (timeouts.header > kTimeoutMost || timeouts.body > kTimeoutMost || timeouts.send > kTimeoutMost ||
        timeouts.idle > kTimeoutMost)
        return "a timeout is above 24 h";
    return std::nullopt;
}

inline constexpr Connection accepted_after(const Connection before)
{
    return {tcp::State::established, http1::State::header_section, before.generation + 1};
}

} // namespace wm
