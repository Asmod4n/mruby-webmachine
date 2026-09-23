#include <algorithm>
#include <array>
#include <charconv>
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <string_view>

#include "arm.hpp"

namespace
{

constexpr size_t kMost = 1 << 20;

const std::array<unsigned char, kMost> &bytes()
{
    static const std::array<unsigned char, kMost> filled = [] {
        std::array<unsigned char, kMost> made{};
        unsigned int x = 2463534242u;
        for (unsigned char &b : made) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            b = static_cast<unsigned char>(x);
        }
        return made;
    }();
    return filled;
}

size_t number_of(const char *const text)
{
    size_t value = 0;
    std::from_chars(text, text + std::strlen(text), value);
    return std::min(value, kMost);
}

} // namespace

extern "C" size_t run(size_t argument_count, ...)
{
    va_list arguments;
    va_start(arguments, argument_count);
    const size_t length = argument_count > 0 ? number_of(va_arg(arguments, const char *)) : 4096;
    va_end(arguments);
    const std::string_view seen(reinterpret_cast<const char *>(bytes().data()), length);
    return static_cast<size_t>(std::count(seen.begin(), seen.end(), 'a'));
}
