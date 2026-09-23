#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <miniz.h>

namespace zip
{

enum class Problem : uint16_t {
    kReader,
    kEncrypted,
    kNotStored,
    kLocalFileHeader,
    kDataOutside,
    kCrc32,
};

struct Refusal {
    Problem      problem;
    mz_zip_error reader_error;
    uint32_t     entry;
};

inline constexpr std::array<std::string_view, 6> kProblemTitles{
    "the zip reader refused the pack",
    "APPNOTE 4.4.4: an entry is encrypted",
    "APPNOTE 4.4.5: an entry is compressed, and only stored entries are read",
    "APPNOTE 4.3.7: a local file header is not valid",
    "APPNOTE 4.3.8: file data lies outside the pack",
    "APPNOTE 4.4.7: the CRC-32 does not match the data",
};

struct Entry {
    std::string_view           name;
    std::span<const std::byte> data;
    std::span<const std::byte> extra;
};

inline constexpr uint32_t kLocalFileHeaderSignature = 0x04034b50;
inline constexpr size_t   kLocalFileHeaderSize = 30;

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

class Reader
{
  public:
    Reader() = default;
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    ~Reader()
    {
        if (open_) mz_zip_reader_end(&archive_);
    }

    std::optional<mz_zip_error> open_over(const std::span<const std::byte> bytes)
    {
        if (!mz_zip_reader_init_mem(&archive_, bytes.data(), bytes.size(), 0)) [[unlikely]]
            return mz_zip_get_last_error(&archive_);
        open_ = true;
        return std::nullopt;
    }

    mz_uint count() { return mz_zip_reader_get_num_files(&archive_); }

    std::optional<mz_zip_archive_file_stat> stat_of(const mz_uint index)
    {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive_, index, &stat)) [[unlikely]] return std::nullopt;
        return stat;
    }

    mz_zip_error last_error() { return mz_zip_get_last_error(&archive_); }

  private:
    mz_zip_archive archive_{};
    bool           open_ = false;
};

inline std::expected<Entry, Refusal>
entry_of(const std::span<const std::byte> pack, const mz_zip_archive_file_stat &stat, const uint32_t index)
{
    const auto refused = [index](const Problem p) { return std::unexpected(Refusal{p, MZ_ZIP_NO_ERROR, index}); };
    if (stat.m_is_encrypted) [[unlikely]] return refused(Problem::kEncrypted);
    if (stat.m_method != 0 || stat.m_comp_size != stat.m_uncomp_size) [[unlikely]] return refused(Problem::kNotStored);
    const size_t local = static_cast<size_t>(stat.m_local_header_ofs);
    if (local + kLocalFileHeaderSize > pack.size() || le32_at(pack, local) != kLocalFileHeaderSignature) [[unlikely]]
        return refused(Problem::kLocalFileHeader);
    const size_t name_length = le16_at(pack, local + 26);
    const size_t extra_length = le16_at(pack, local + 28);
    const size_t name_at = local + kLocalFileHeaderSize;
    const size_t data_at = name_at + name_length + extra_length;
    const size_t size = static_cast<size_t>(stat.m_comp_size);
    if (data_at > pack.size() || size > pack.size() - data_at) [[unlikely]] return refused(Problem::kDataOutside);
    const std::span<const std::byte> data = pack.subspan(data_at, size);
    if (mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(data.data()), data.size()) != stat.m_crc32)
        [[unlikely]]
        return refused(Problem::kCrc32);
    const std::span<const std::byte> name = pack.subspan(name_at, name_length);
    return Entry{std::string_view(reinterpret_cast<const char *>(name.data()), name.size()), data,
                 pack.subspan(name_at + name_length, extra_length)};
}

inline std::expected<std::vector<Entry>, Refusal>
entries_of(const std::span<const std::byte> pack)
{
    Reader reader;
    if (const std::optional<mz_zip_error> error = reader.open_over(pack); error) [[unlikely]]
        return std::unexpected(Refusal{Problem::kReader, *error, 0});
    std::vector<Entry> entries;
    entries.reserve(reader.count());
    for (mz_uint i = 0; i < reader.count(); i++) {
        const std::optional<mz_zip_archive_file_stat> stat = reader.stat_of(i);
        if (!stat) [[unlikely]] return std::unexpected(Refusal{Problem::kReader, reader.last_error(), i});
        if (stat->m_is_directory) continue;
        const std::expected<Entry, Refusal> entry = entry_of(pack, *stat, i);
        if (!entry) [[unlikely]] return std::unexpected(entry.error());
        entries.push_back(*entry);
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
