// A memory mapping that this process owns, over mmap(2), munmap(2),
// madvise(2) and memfd_create(2): anonymous and private, or one memfd
// mapped twice in a row so that its end runs on into its start.
// No other file of this tree calls these functions.
#pragma once

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stdexcept>
#include <system_error>

namespace os
{

enum class Advice : int {
    free = MADV_FREE,
    dont_need = MADV_DONTNEED,
    huge_page = MADV_HUGEPAGE,
};

struct Pages {
    size_t first;
    size_t count;
};

inline std::error_code error_of_errno()
{
    return {errno, std::system_category()};
}

class Mapping
{
  public:
    static std::expected<Mapping, std::error_code> reserved(const size_t bytes)
    {
        const long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) [[unlikely]]
            return std::unexpected(error_of_errno());
        const size_t page_bytes = static_cast<size_t>(page);
        const size_t pages = pages_for(bytes, page_bytes);
        void *const at = mmap(nullptr, pages * page_bytes, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (at == MAP_FAILED) [[unlikely]]
            return std::unexpected(error_of_errno());
        return Mapping(static_cast<std::byte *>(at), pages, page_bytes, pages * page_bytes);
    }

    static std::expected<Mapping, std::error_code> twice(const size_t bytes)
    {
        const long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) [[unlikely]]
            return std::unexpected(error_of_errno());
        const size_t page_bytes = static_cast<size_t>(page);
        const size_t pages = pages_for(bytes, page_bytes);
        const size_t length = pages * page_bytes;
        if (length > SIZE_MAX / 2) [[unlikely]]
            throw std::logic_error("a double mapping of more bytes than size_t holds");
        const int memory = memfd_create("ring", MFD_CLOEXEC);
        if (memory < 0) [[unlikely]]
            return std::unexpected(error_of_errno());
        if (ftruncate(memory, static_cast<off_t>(length)) != 0) [[unlikely]] {
            const std::error_code failed = error_of_errno();
            close(memory);
            return std::unexpected(failed);
        }
        void *const span = mmap(nullptr, 2 * length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (span == MAP_FAILED) [[unlikely]] {
            const std::error_code failed = error_of_errno();
            close(memory);
            return std::unexpected(failed);
        }
        std::byte *const at = static_cast<std::byte *>(span);
        void *const lower = mmap(at, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, memory, 0);
        void *const upper = mmap(at + length, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, memory, 0);
        const std::error_code failed = lower == MAP_FAILED || upper == MAP_FAILED ? error_of_errno() : std::error_code{};
        close(memory);
        if (failed) [[unlikely]] {
            munmap(span, 2 * length);
            return std::unexpected(failed);
        }
        return Mapping(at, pages, page_bytes, 2 * length);
    }

    Mapping(Mapping &&other) noexcept
        : at_(other.at_), pages_(other.pages_), page_bytes_(other.page_bytes_), mapped_(other.mapped_)
    {
        other.at_ = nullptr;
    }
    Mapping &operator=(Mapping &&) = delete;
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    ~Mapping()
    {
        if (at_ != nullptr)
            munmap(at_, mapped_);
    }

    size_t bytes() const
    {
        return pages_ * page_bytes_;
    }

    std::span<std::byte> bytes_of(const Pages range) const
    {
        checked(range);
        return {at_ + range.first * page_bytes_, range.count * page_bytes_};
    }

    std::span<std::byte> all() const
    {
        return bytes_of({0, pages_});
    }

    std::span<std::byte> all_and_its_start_again() const
    {
        if (mapped_ != 2 * pages_ * page_bytes_) [[unlikely]]
            throw std::logic_error("a mapping that is not mapped twice");
        return {at_, mapped_};
    }

    std::expected<void, std::error_code> advise(const Pages range, const Advice advice) const
    {
        checked(range);
        if (madvise(at_ + range.first * page_bytes_, range.count * page_bytes_, static_cast<int>(advice)) != 0)
            [[unlikely]]
            return std::unexpected(error_of_errno());
        return {};
    }

    std::expected<void, std::error_code> advise_all(const Advice advice) const
    {
        return advise({0, pages_}, advice);
    }

  private:
    Mapping(std::byte *const at, const size_t pages, const size_t page_bytes, const size_t mapped)
        : at_(at), pages_(pages), page_bytes_(page_bytes), mapped_(mapped)
    {
    }

    static size_t pages_for(const size_t bytes, const size_t page_bytes)
    {
        if (bytes == 0 || bytes > SIZE_MAX - page_bytes) [[unlikely]]
            throw std::logic_error("a mapping of no bytes or of more bytes than size_t holds");
        return (bytes + page_bytes - 1) / page_bytes;
    }

    void checked(const Pages range) const
    {
        if (at_ == nullptr || range.count == 0 || range.first >= pages_ || range.count > pages_ - range.first)
            [[unlikely]]
            throw std::logic_error("a page range outside the mapping");
    }

    std::byte *at_;
    size_t pages_;
    size_t page_bytes_;
    size_t mapped_;
};

} // namespace os
