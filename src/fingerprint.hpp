#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/rand.h>

namespace fingerprint
{

inline constexpr size_t kKeySize = 32;
inline constexpr size_t kDigestSize = 16;

using Key = std::array<std::byte, kKeySize>;
using Digest = std::array<std::byte, kDigestSize>;

struct Facts {
    Digest                             build;
    std::string_view                   method;
    std::string_view                   request_target;
    std::string_view                   callback;
    std::string_view                   exception;
    std::span<const std::string_view> backtrace;
    uint16_t                           status;
};

struct MacFree {
    void operator()(EVP_MAC *const mac) const { EVP_MAC_free(mac); }
};

struct MacContextFree {
    void operator()(EVP_MAC_CTX *const ctx) const { EVP_MAC_CTX_free(ctx); }
};

constexpr std::optional<uint8_t>
nibble_of(const char c)
{
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return std::nullopt;
}

constexpr std::optional<Key>
key_of(const std::string_view hex)
{
    if (hex.size() != 2 * kKeySize) [[unlikely]] return std::nullopt;
    Key key{};
    for (size_t i = 0; i < kKeySize; i++) {
        const std::optional<uint8_t> high = nibble_of(hex.at(2 * i));
        const std::optional<uint8_t> low = nibble_of(hex.at(2 * i + 1));
        if (!high || !low) [[unlikely]] return std::nullopt;
        key.at(i) = static_cast<std::byte>(*high << 4 | *low);
    }
    return key;
}

template <size_t N>
constexpr std::string
hex_of(const std::array<std::byte, N> &bytes)
{
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out(2 * N, '0');
    for (size_t i = 0; i < N; i++) {
        const uint8_t b = std::to_integer<uint8_t>(bytes.at(i));
        out.at(2 * i) = kHex.at(b >> 4);
        out.at(2 * i + 1) = kHex.at(b & 0xf);
    }
    return out;
}

inline std::optional<Key>
random_key()
{
    Key key{};
    if (RAND_bytes(reinterpret_cast<unsigned char *>(key.data()), static_cast<int>(key.size())) != 1) [[unlikely]]
        return std::nullopt;
    return key;
}

class Mac
{
  public:
    static std::optional<Mac> keyed_with(const Key &key)
    {
        std::unique_ptr<EVP_MAC, MacFree> mac(EVP_MAC_fetch(nullptr, "BLAKE2BMAC", nullptr));
        if (!mac) [[unlikely]] return std::nullopt;
        std::unique_ptr<EVP_MAC_CTX, MacContextFree> ctx(EVP_MAC_CTX_new(mac.get()));
        if (!ctx) [[unlikely]] return std::nullopt;
        size_t size = kDigestSize;
        const std::array<OSSL_PARAM, 2> params{
            OSSL_PARAM_construct_size_t(OSSL_MAC_PARAM_SIZE, &size),
            OSSL_PARAM_construct_end(),
        };
        if (EVP_MAC_init(ctx.get(), reinterpret_cast<const unsigned char *>(key.data()), key.size(),
                         params.data()) != 1) [[unlikely]]
            return std::nullopt;
        return Mac(std::move(ctx));
    }

    bool update_with_length(const std::span<const std::byte> bytes)
    {
        const uint32_t length = static_cast<uint32_t>(bytes.size());
        const std::array<std::byte, 4> prefix{
            static_cast<std::byte>(length),
            static_cast<std::byte>(length >> 8),
            static_cast<std::byte>(length >> 16),
            static_cast<std::byte>(length >> 24),
        };
        return update(prefix) && update(bytes);
    }

    bool update_with_length(const std::string_view text)
    {
        return update_with_length(std::as_bytes(std::span(text)));
    }

    std::optional<Digest> final()
    {
        Digest out{};
        size_t written = 0;
        if (EVP_MAC_final(ctx_.get(), reinterpret_cast<unsigned char *>(out.data()), &written, out.size()) != 1 ||
            written != out.size()) [[unlikely]]
            return std::nullopt;
        return out;
    }

  private:
    explicit Mac(std::unique_ptr<EVP_MAC_CTX, MacContextFree> ctx) : ctx_(std::move(ctx)) {}

    bool update(const std::span<const std::byte> bytes)
    {
        return EVP_MAC_update(ctx_.get(), reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()) == 1;
    }

    std::unique_ptr<EVP_MAC_CTX, MacContextFree> ctx_;
};

inline std::optional<Digest>
build_of(const Key &key, const std::span<const std::byte> bytecode)
{
    std::optional<Mac> mac = Mac::keyed_with(key);
    if (!mac) [[unlikely]] return std::nullopt;
    if (!mac->update_with_length(bytecode)) [[unlikely]] return std::nullopt;
    return mac->final();
}

inline std::optional<Digest>
fingerprint_of(const Key &key, const Facts &facts)
{
    std::optional<Mac> mac = Mac::keyed_with(key);
    if (!mac) [[unlikely]] return std::nullopt;
    const std::array<std::byte, 2> status{static_cast<std::byte>(facts.status),
                                          static_cast<std::byte>(facts.status >> 8)};
    bool ok = mac->update_with_length(std::span<const std::byte>(facts.build)) &&
              mac->update_with_length(facts.method) && mac->update_with_length(facts.request_target) &&
              mac->update_with_length(facts.callback) && mac->update_with_length(facts.exception) &&
              mac->update_with_length(std::span<const std::byte>(status));
    for (const std::string_view line : facts.backtrace) ok = ok && mac->update_with_length(line);
    if (!ok) [[unlikely]] return std::nullopt;
    return mac->final();
}

}
