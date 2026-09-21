#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>

// The four fields a negotiated GET wants, and the one function that puts a
// field into its slot. Two translation units include this: the plain copy
// of picohttpparser and the hooked one.
namespace wanted
{

struct Wanted {
    std::string_view host;
    std::string_view accept;
    std::string_view accept_encoding;
    std::string_view accept_language;
};

// Eight bytes of a name, lowercased, against the same eight bytes of a
// literal, which the compiler folds to a constant. The load may pass the
// end of the name because the pool has kWidePadding behind it, and that
// is the one thing neither picohttpparser nor strncasecmp is allowed to
// do.
constexpr uint64_t ascii_word_at(const std::string_view text, const size_t at)
{
    uint64_t word = 0;
    for (size_t index = 0; index < 8 && at + index < text.size(); index++)
        word |= static_cast<uint64_t>(static_cast<unsigned char>(text.at(at + index)))
                << (index * 8);
    return word;
}

inline uint64_t padded_word_at(const std::string_view text, const size_t at)
{
    uint64_t word = 0;
    std::memcpy(&word, std::next(text.data(), static_cast<ptrdiff_t>(at)), 8);
    const size_t left = text.size() - at;
    return left >= 8 ? word : word & ((uint64_t{1} << (left * 8)) - 1);
}

// Only 'A' to 'Z' fold: 0x41 + 0x3f sets bit 7 and 0x5a + 0x25 does not,
// so the two carries name the range without a branch. Bit 7 is taken out
// of the range test and put back as the last and, so a byte above 0x7f
// neither folds nor carries into its neighbour.
inline uint64_t ascii_lowered_word(const uint64_t word)
{
    const uint64_t high = 0x8080808080808080ull;
    const uint64_t seven = word & ~high;
    const uint64_t at_least_a = seven + 0x3f3f3f3f3f3f3f3full;
    const uint64_t at_most_z = seven + 0x2525252525252525ull;
    return word | ((at_least_a & ~at_most_z & ~word & high) >> 2);
}

inline bool name_is(const std::string_view name, const std::string_view lowercase)
{
    if (name.size() != lowercase.size())
        return false;
    if (ascii_lowered_word(padded_word_at(name, 0)) != ascii_word_at(lowercase, 0))
        return false;
    if (name.size() <= 8)
        return true;
    return ascii_lowered_word(padded_word_at(name, 8)) == ascii_word_at(lowercase, 8);
}

inline void note_field(const std::string_view name, const std::string_view value, Wanted &wanted)
{
    switch (name.size()) {
        case 4:
            if (name_is(name, "host"))
                wanted.host = value;
            break;
        case 6:
            if (name_is(name, "accept"))
                wanted.accept = value;
            break;
        case 15:
            if (name_is(name, "accept-encoding"))
                wanted.accept_encoding = value;
            else if (name_is(name, "accept-language"))
                wanted.accept_language = value;
            break;
        default:
            break;
    }
}

} // namespace wanted
