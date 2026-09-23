#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace zip
{

enum class Problem : uint16_t {
    kNoEndOfCentralDirectory,
    kCentralDirectoryOutside,
    kCentralFileHeader,
    kLocalFileHeader,
    kNotStored,
    kSizeMismatch,
    kDataOutside,
    kCrc32,
};

struct Refusal {
    Problem  problem;
    uint32_t offset;
};

inline constexpr std::array<std::string_view, 8> kProblemTitles{
    "APPNOTE 4.3.16: no end of central directory record",
    "APPNOTE 4.3.16: the central directory lies outside the archive",
    "APPNOTE 4.3.12: a central file header is not valid",
    "APPNOTE 4.3.7: a local file header is not valid",
    "APPNOTE 4.4.5: an entry is compressed, and only stored entries are read",
    "APPNOTE 4.4.8: compressed and uncompressed sizes differ for a stored entry",
    "APPNOTE 4.3.8: file data lies outside the archive",
    "APPNOTE 4.4.7: the CRC-32 does not match the data",
};

struct Entry {
    std::string_view           name;
    std::span<const std::byte> data;
    std::span<const std::byte> extra;
};

inline constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054b50;
inline constexpr uint32_t kCentralFileHeaderSignature = 0x02014b50;
inline constexpr uint32_t kLocalFileHeaderSignature = 0x04034b50;
inline constexpr size_t   kEndOfCentralDirectorySize = 22;
inline constexpr size_t   kCentralFileHeaderSize = 46;
inline constexpr size_t   kLocalFileHeaderSize = 30;
inline constexpr size_t   kCommentMax = 0xffff;

constexpr uint16_t
le16_at(const std::span<const std::byte> bytes, const size_t at)
{
    return static_cast<uint16_t>(std::to_integer<uint16_t>(bytes[at]) |
                                 std::to_integer<uint16_t>(bytes[at + 1]) << 8);
}

constexpr uint32_t
le32_at(const std::span<const std::byte> bytes, const size_t at)
{
    return static_cast<uint32_t>(le16_at(bytes, at)) | static_cast<uint32_t>(le16_at(bytes, at + 2)) << 16;
}

constexpr std::array<uint32_t, 256>
crc32_table_of()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table.at(n) = c;
    }
    return table;
}

inline constexpr std::array<uint32_t, 256> kCrc32Table = crc32_table_of();

constexpr uint32_t
crc32_of(const std::span<const std::byte> bytes)
{
    uint32_t c = 0xffffffffu;
    for (const std::byte b : bytes) c = kCrc32Table.at((c ^ std::to_integer<uint32_t>(b)) & 0xff) ^ (c >> 8);
    return c ^ 0xffffffffu;
}

constexpr std::optional<size_t>
end_of_central_directory_at(const std::span<const std::byte> archive)
{
    if (archive.size() < kEndOfCentralDirectorySize) [[unlikely]] return std::nullopt;
    const size_t last = archive.size() - kEndOfCentralDirectorySize;
    const size_t first = last > kCommentMax ? last - kCommentMax : 0;
    for (size_t at = last + 1; at-- > first;) {
        if (le32_at(archive, at) == kEndOfCentralDirectorySignature &&
            at + kEndOfCentralDirectorySize + le16_at(archive, at + 20) == archive.size())
            return at;
    }
    return std::nullopt;
}

constexpr std::expected<Entry, Refusal>
entry_at(const std::span<const std::byte> archive, const size_t header)
{
    const auto refused = [header](const Problem p) {
        return std::unexpected(Refusal{p, static_cast<uint32_t>(header)});
    };
    if (header + kCentralFileHeaderSize > archive.size() ||
        le32_at(archive, header) != kCentralFileHeaderSignature) [[unlikely]]
        return refused(Problem::kCentralFileHeader);
    const uint16_t method = le16_at(archive, header + 10);
    const uint32_t crc32 = le32_at(archive, header + 16);
    const uint32_t compressed = le32_at(archive, header + 20);
    const uint32_t uncompressed = le32_at(archive, header + 24);
    const uint16_t name_length = le16_at(archive, header + 28);
    const uint16_t extra_length = le16_at(archive, header + 30);
    const uint32_t local = le32_at(archive, header + 42);
    if (method != 0) [[unlikely]] return refused(Problem::kNotStored);
    if (compressed != uncompressed) [[unlikely]] return refused(Problem::kSizeMismatch);
    const size_t name_at = header + kCentralFileHeaderSize;
    if (name_at + name_length + extra_length > archive.size()) [[unlikely]]
        return refused(Problem::kCentralFileHeader);
    if (size_t{local} + kLocalFileHeaderSize > archive.size() ||
        le32_at(archive, local) != kLocalFileHeaderSignature) [[unlikely]]
        return refused(Problem::kLocalFileHeader);
    const size_t data_at =
        size_t{local} + kLocalFileHeaderSize + le16_at(archive, local + 26) + le16_at(archive, local + 28);
    if (data_at + compressed > archive.size()) [[unlikely]] return refused(Problem::kDataOutside);
    const std::span<const std::byte> data = archive.subspan(data_at, compressed);
    if (crc32_of(data) != crc32) [[unlikely]] return refused(Problem::kCrc32);
    const std::span<const std::byte> name = archive.subspan(name_at, name_length);
    return Entry{std::string_view(reinterpret_cast<const char *>(name.data()), name.size()), data,
                 archive.subspan(name_at + name_length, extra_length)};
}

inline std::expected<std::vector<Entry>, Refusal>
entries_of(const std::span<const std::byte> archive)
{
    const std::optional<size_t> end = end_of_central_directory_at(archive);
    if (!end) [[unlikely]] return std::unexpected(Refusal{Problem::kNoEndOfCentralDirectory, 0});
    const uint16_t count = le16_at(archive, *end + 10);
    const uint32_t size = le32_at(archive, *end + 12);
    const uint32_t start = le32_at(archive, *end + 16);
    if (size_t{start} + size > *end) [[unlikely]]
        return std::unexpected(Refusal{Problem::kCentralDirectoryOutside, static_cast<uint32_t>(*end)});
    std::vector<Entry> entries;
    entries.reserve(count);
    size_t header = start;
    for (uint16_t i = 0; i < count; i++) {
        const std::expected<Entry, Refusal> entry = entry_at(archive, header);
        if (!entry) [[unlikely]] return std::unexpected(entry.error());
        entries.push_back(*entry);
        header += kCentralFileHeaderSize + entry->name.size() + entry->extra.size() + le16_at(archive, header + 32);
    }
    return entries;
}

constexpr std::optional<std::span<const std::byte>>
extra_field_of(const std::span<const std::byte> extra, const uint16_t header_id)
{
    size_t at = 0;
    while (at + 4 <= extra.size()) {
        const uint16_t id = le16_at(extra, at);
        const uint16_t size = le16_at(extra, at + 2);
        if (at + 4 + size > extra.size()) [[unlikely]] return std::nullopt;
        if (id == header_id) return extra.subspan(at + 4, size);
        at += 4 + size;
    }
    return std::nullopt;
}

}
