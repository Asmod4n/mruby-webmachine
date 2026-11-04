// A safe C++ layer over liburing: the ring and each provided buffer ring
// are owned and not copyable, a submission entry exists only when the
// queue gave one, and a failure is a value. The names are liburing's,
// without the io_uring_ prefix. No other file of this tree calls liburing.
#pragma once

#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>

namespace uring
{

inline std::error_code error_of(const int negative_errno)
{
    return {-negative_errno, std::system_category()};
}

struct Cqe {
    int res;
    uint32_t flags;
    uint64_t data;
};

class Ring;

class Sqe
{
  public:
    void prep_socket_direct(const int domain, const int type, const int protocol, const uint32_t file_index)
    {
        io_uring_prep_socket_direct(sqe_, domain, type, protocol, file_index, 0);
    }

    void prep_bind(const int fd, const sockaddr_un &address)
    {
        io_uring_prep_bind(sqe_, fd, reinterpret_cast<sockaddr *>(const_cast<sockaddr_un *>(&address)), sizeof address);
    }

    void prep_bind(const int fd, const sockaddr_in &address)
    {
        io_uring_prep_bind(sqe_, fd, reinterpret_cast<sockaddr *>(const_cast<sockaddr_in *>(&address)), sizeof address);
    }

    void prep_listen(const int fd, const int backlog)
    {
        io_uring_prep_listen(sqe_, fd, backlog);
    }

    void prep_cmd_setsockopt(const int fd, const int level, const int name, const int &value)
    {
        io_uring_prep_cmd_sock(sqe_, SOCKET_URING_OP_SETSOCKOPT, fd, level, name, const_cast<int *>(&value),
                               sizeof value);
    }

    void prep_cmd_getsockname(const int fd, sockaddr_in &address)
    {
        io_uring_prep_cmd_sock(sqe_, SOCKET_URING_OP_GETSOCKNAME, fd, 0, 0, &address,
                               static_cast<int>(sizeof address));
    }

    void prep_multishot_accept_direct(const int fd)
    {
        io_uring_prep_multishot_accept_direct(sqe_, fd, nullptr, nullptr, 0);
    }

    void prep_recv_multishot(const int fd)
    {
        io_uring_prep_recv_multishot(sqe_, fd, nullptr, 0, 0);
    }

    void prep_send(const int fd, const std::span<const std::byte> bytes, const int flags)
    {
        io_uring_prep_send(sqe_, fd, bytes.data(), bytes.size(), flags);
    }

    void prep_send_selected(const int fd, const int flags)
    {
        io_uring_prep_send(sqe_, fd, nullptr, 0, flags);
    }

    void prep_sendmsg(const int fd, const msghdr &message, const int flags)
    {
        io_uring_prep_sendmsg(sqe_, fd, &message, static_cast<unsigned>(flags));
    }

    void prep_close_direct(const uint32_t file_index)
    {
        io_uring_prep_close_direct(sqe_, file_index);
    }

    void fixed_file()
    {
        sqe_->flags |= IOSQE_FIXED_FILE;
    }

    void buffer_select(const uint16_t group)
    {
        sqe_->flags |= IOSQE_BUFFER_SELECT;
        sqe_->buf_group = group;
    }

    void set_data64(const uint64_t data)
    {
        io_uring_sqe_set_data64(sqe_, data);
    }

  private:
    friend class Ring;
    explicit Sqe(io_uring_sqe *const sqe) : sqe_(sqe)
    {
    }
    io_uring_sqe *sqe_;
};

class BufferRing;

class Ring
{
  public:
    Ring() = default;
    Ring(const Ring &) = delete;
    Ring &operator=(const Ring &) = delete;
    Ring(Ring &&) = delete;
    Ring &operator=(Ring &&) = delete;
    ~Ring()
    {
        if (queued_)
            io_uring_queue_exit(&ring_);
    }

    std::expected<uint32_t, std::error_code> queue_init_params(const uint32_t entries, const uint32_t flags)
    {
        if (queued_) [[unlikely]]
            throw std::logic_error("a ring is queued twice");
        io_uring_params asking = {};
        asking.flags = flags;
        const int begun = io_uring_queue_init_params(entries, &ring_, &asking);
        if (begun != 0) [[unlikely]]
            return std::unexpected(error_of(begun));
        queued_ = true;
        return asking.sq_entries;
    }

    std::expected<void, std::error_code> register_ring_fd()
    {
        const int registered = io_uring_register_ring_fd(queued());
        if (registered < 0) [[unlikely]]
            return std::unexpected(error_of(registered));
        return {};
    }

    std::expected<void, std::error_code> register_files_sparse(const uint32_t count)
    {
        const int registered = io_uring_register_files_sparse(queued(), count);
        if (registered != 0) [[unlikely]]
            return std::unexpected(error_of(registered));
        return {};
    }

    std::expected<void, std::error_code> register_file_alloc_range(const uint32_t first, const uint32_t count)
    {
        const int registered = io_uring_register_file_alloc_range(queued(), first, count);
        if (registered != 0) [[unlikely]]
            return std::unexpected(error_of(registered));
        return {};
    }

    std::optional<Sqe> get_sqe()
    {
        io_uring_sqe *const sqe = io_uring_get_sqe(queued());
        if (sqe == nullptr) [[unlikely]]
            return std::nullopt;
        return Sqe(sqe);
    }

    int submit()
    {
        return io_uring_submit(queued());
    }

    std::expected<Cqe, std::error_code> wait_cqe()
    {
        io_uring_cqe *cqe = nullptr;
        const int waited = io_uring_wait_cqe(queued(), &cqe);
        if (waited != 0) [[unlikely]]
            return std::unexpected(error_of(waited));
        const Cqe seen{cqe->res, cqe->flags, io_uring_cqe_get_data64(cqe)};
        io_uring_cqe_seen(&ring_, cqe);
        return seen;
    }

    int submit_and_wait_timeout(const long seconds)
    {
        io_uring_cqe *first = nullptr;
        __kernel_timespec timeout = {seconds, 0};
        return io_uring_submit_and_wait_timeout(queued(), &first, 1, &timeout, nullptr);
    }

    template <class Taking> unsigned for_each_cqe(Taking &&taking)
    {
        io_uring_cqe *cqe = nullptr;
        unsigned head = 0;
        unsigned seen = 0;
        io_uring_for_each_cqe(queued(), head, cqe)
        {
            taking(Cqe{cqe->res, cqe->flags, io_uring_cqe_get_data64(cqe)});
            seen++;
        }
        io_uring_cq_advance(&ring_, seen);
        return seen;
    }

  private:
    friend class BufferRing;
    io_uring *queued()
    {
        if (!queued_) [[unlikely]]
            throw std::logic_error("a ring that is not queued");
        return &ring_;
    }

    io_uring ring_ = {};
    bool queued_ = false;
};

class BufferRing
{
  public:
    static std::expected<BufferRing, std::error_code> setup_buf_ring(Ring &ring, const uint32_t entries,
                                                                      const uint16_t group)
    {
        int failed = 0;
        io_uring_buf_ring *const buffers =
            io_uring_setup_buf_ring(ring.queued(), entries, static_cast<int>(group), 0, &failed);
        if (buffers == nullptr) [[unlikely]]
            return std::unexpected(error_of(failed));
        return BufferRing(ring, buffers, entries, group);
    }

    BufferRing(BufferRing &&other) noexcept
        : ring_(other.ring_), buffers_(other.buffers_), entries_(other.entries_), group_(other.group_)
    {
        other.buffers_ = nullptr;
    }
    BufferRing &operator=(BufferRing &&) = delete;
    BufferRing(const BufferRing &) = delete;
    BufferRing &operator=(const BufferRing &) = delete;
    ~BufferRing()
    {
        if (buffers_ != nullptr)
            io_uring_free_buf_ring(&ring_->ring_, buffers_, entries_, static_cast<int>(group_));
    }

    void buf_ring_add(const std::span<std::byte> buffer, const uint16_t id, const uint32_t offset)
    {
        if (buffers_ == nullptr || buffer.size() > UINT32_MAX) [[unlikely]]
            throw std::logic_error("a buffer added to a buffer ring that is gone, or larger than 4 GiB");
        io_uring_buf_ring_add(buffers_, buffer.data(), static_cast<unsigned>(buffer.size()), id,
                              io_uring_buf_ring_mask(entries_), static_cast<int>(offset));
    }

    void buf_ring_advance(const uint32_t count)
    {
        if (buffers_ == nullptr) [[unlikely]]
            throw std::logic_error("a buffer ring that is gone");
        io_uring_buf_ring_advance(buffers_, static_cast<int>(count));
    }

    uint16_t group() const
    {
        return group_;
    }

  private:
    BufferRing(Ring &ring, io_uring_buf_ring *const buffers, const uint32_t entries, const uint16_t group)
        : ring_(&ring), buffers_(buffers), entries_(entries), group_(group)
    {
    }

    Ring *ring_;
    io_uring_buf_ring *buffers_;
    uint32_t entries_;
    uint16_t group_;
};

} // namespace uring
