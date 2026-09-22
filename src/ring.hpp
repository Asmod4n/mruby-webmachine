#pragma once

#include <cerrno>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include <liburing.h>

namespace wm
{

enum class Doing : uint8_t {
    kAccepting = 1,
    kReceiving,
    kSending,
    kClosing,
};

inline constexpr uint64_t marked(const Doing doing, const uint32_t slot)
{
    return (static_cast<uint64_t>(doing) << 32) | slot;
}

inline constexpr Doing doing_of(const uint64_t mark)
{
    return static_cast<Doing>(mark >> 32);
}

inline constexpr uint32_t slot_of(const uint64_t mark)
{
    return static_cast<uint32_t>(mark);
}

static_assert(doing_of(marked(Doing::kReceiving, 4095)) == Doing::kReceiving,
              "a mark says what was being done");
static_assert(slot_of(marked(Doing::kReceiving, 4095)) == 4095,
              "and which slot it was being done to");
static_assert(marked(Doing::kAccepting, 0) != marked(Doing::kReceiving, 0),
              "two things done to one slot are two marks");

class QueueIsFull : public std::runtime_error
{
  public:
    QueueIsFull() : std::runtime_error("the submission queue is full")
    {
    }
};

inline constexpr uint16_t kBufferGroup = 1;
inline constexpr uint32_t kBufferCount = 4096;
inline constexpr uint32_t kBufferBytes = 2048;
inline constexpr uint32_t kListeners = 4;

class Ring
{
  public:
    Ring() = default;
    Ring(const Ring &) = delete;
    Ring &operator=(const Ring &) = delete;

    ~Ring()
    {
        if (buffers_ != nullptr)
            io_uring_free_buf_ring(&ring_, buffers_, kBufferCount, kBufferGroup);
        if (room_ != nullptr)
            munmap(room_, static_cast<size_t>(kBufferCount) * kBufferBytes);
        if (standing_)
            io_uring_queue_exit(&ring_);
    }

    int stood_up(const unsigned entries)
    {
        rlimit limit = {};
        if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
            return -errno;
        const uint32_t open_files = limit.rlim_cur == RLIM_INFINITY
                                        ? 65536u
                                        : static_cast<uint32_t>(limit.rlim_cur);
        if (open_files <= kListeners)
            return -EMFILE;
        connections_ = open_files - kListeners;

        const int begun = io_uring_queue_init(entries, &ring_, 0);
        if (begun < 0)
            return begun;
        standing_ = true;

        const int sparse = io_uring_register_files_sparse(&ring_, connections_ + kListeners);
        if (sparse != 0)
            return sparse;
        const int ranged = io_uring_register_file_alloc_range(&ring_, 0, connections_);
        if (ranged != 0)
            return ranged;

        void *const mapped =
            mmap(nullptr, static_cast<size_t>(kBufferCount) * kBufferBytes,
                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped == MAP_FAILED)
            return -errno;
        room_ = static_cast<uint8_t *>(mapped);

        int trouble = 0;
        buffers_ = io_uring_setup_buf_ring(&ring_, kBufferCount, kBufferGroup, 0, &trouble);
        if (buffers_ == nullptr)
            return trouble;
        const int mask = io_uring_buf_ring_mask(kBufferCount);
        for (uint32_t at = 0; at < kBufferCount; at++)
            io_uring_buf_ring_add(buffers_, room_ + static_cast<size_t>(at) * kBufferBytes,
                                  kBufferBytes, static_cast<uint16_t>(at), mask,
                                  static_cast<int>(at));
        io_uring_buf_ring_advance(buffers_, kBufferCount);
        return 0;
    }

    int listens_on(const uint16_t port)
    {
        if (listeners_ >= kListeners)
            return -ENOSPC;
        const uint32_t slot = connections_ + listeners_;

        io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_socket_direct(sqe, AF_INET, SOCK_STREAM, 0, slot, 0);
        int answer = one_at_a_time(sqe);
        if (answer < 0)
            return answer;

        const int on = 1;
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_cmd_sock(sqe, SOCKET_URING_OP_SETSOCKOPT, static_cast<int>(slot),
                               SOL_SOCKET, SO_REUSEADDR, const_cast<int *>(&on), sizeof on);
        sqe->flags |= IOSQE_FIXED_FILE;
        answer = one_at_a_time(sqe);
        if (answer < 0)
            return answer;

        sockaddr_in where = {};
        where.sin_family = AF_INET;
        where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        where.sin_port = htons(port);
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_bind(sqe, static_cast<int>(slot),
                           reinterpret_cast<sockaddr *>(&where), sizeof where);
        sqe->flags |= IOSQE_FIXED_FILE;
        answer = one_at_a_time(sqe);
        if (answer < 0)
            return answer;

        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_listen(sqe, static_cast<int>(slot), 4096);
        sqe->flags |= IOSQE_FIXED_FILE;
        answer = one_at_a_time(sqe);
        if (answer < 0)
            return answer;

        sockaddr_in took = {};
        socklen_t took_length = sizeof took;
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_cmd_sock(sqe, SOCKET_URING_OP_GETSOCKNAME, static_cast<int>(slot), 0, 0,
                               &took, static_cast<int>(took_length));
        sqe->flags |= IOSQE_FIXED_FILE;
        answer = one_at_a_time(sqe);
        port_[listeners_] = answer >= 0 ? ntohs(took.sin_port) : port;

        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_multishot_accept_direct(sqe, static_cast<int>(slot), nullptr, nullptr, 0);
        sqe->flags |= IOSQE_FIXED_FILE;
        io_uring_sqe_set_data64(sqe, marked(Doing::kAccepting, slot));
        io_uring_submit(&ring_);
        listeners_++;
        return 0;
    }

    uint16_t port_taken(const uint32_t which) const
    {
        return which < kListeners ? port_[which] : 0;
    }

    template <class Answering> void serves(Answering answering, const bool &until)
    {
        while (!until) {
            io_uring_cqe *first = nullptr;
            __kernel_timespec a_second = {1, 0};
            const int waited =
                io_uring_submit_and_wait_timeout(&ring_, &first, 1, &a_second, nullptr);
            if (waited < 0 && waited != -ETIME && waited != -EINTR)
                break;
            unsigned head = 0;
            unsigned seen = 0;
            unsigned armed = 0;
            io_uring_cqe *cqe = nullptr;
            io_uring_for_each_cqe(&ring_, head, cqe)
            {
                seen++;
                armed += took(cqe, answering);
            }
            io_uring_cq_advance(&ring_, seen);
            if (armed != 0)
                io_uring_submit(&ring_);
        }
    }

  private:
    int one_at_a_time(io_uring_sqe *const sqe)
    {
        io_uring_sqe_set_data64(sqe, 0);
        io_uring_submit(&ring_);
        io_uring_cqe *cqe = nullptr;
        if (io_uring_wait_cqe(&ring_, &cqe) != 0)
            return -EIO;
        const int answer = cqe->res;
        io_uring_cqe_seen(&ring_, cqe);
        return answer;
    }

    io_uring_sqe *room_for_one_more()
    {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (sqe != nullptr)
            return sqe;
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            throw QueueIsFull();
        return sqe;
    }

    unsigned closes(const uint32_t slot)
    {
        io_uring_sqe *const sqe = room_for_one_more();
        io_uring_prep_close_direct(sqe, slot);
        io_uring_sqe_set_data64(sqe, marked(Doing::kClosing, slot));
        return 1;
    }

    unsigned receives(const uint32_t slot)
    {
        io_uring_sqe *const sqe = room_for_one_more();
        io_uring_prep_recv_multishot(sqe, static_cast<int>(slot), nullptr, 0, 0);
        sqe->flags |= IOSQE_FIXED_FILE | IOSQE_BUFFER_SELECT;
        sqe->buf_group = kBufferGroup;
        io_uring_sqe_set_data64(sqe, marked(Doing::kReceiving, slot));
        return 1;
    }

    template <class Answering> unsigned took(io_uring_cqe *const cqe, Answering &answering)
    {
        unsigned armed = 0;
        const uint64_t mark = io_uring_cqe_get_data64(cqe);
        const uint32_t slot = slot_of(mark);
        switch (doing_of(mark)) {
        case Doing::kAccepting:
            if (cqe->res >= 0)
                armed += receives(static_cast<uint32_t>(cqe->res));
            if ((cqe->flags & IORING_CQE_F_MORE) == 0) {
                io_uring_sqe *const sqe = room_for_one_more();
                io_uring_prep_multishot_accept_direct(sqe, static_cast<int>(slot), nullptr,
                                                      nullptr, 0);
                sqe->flags |= IOSQE_FIXED_FILE;
                io_uring_sqe_set_data64(sqe, mark);
                armed++;
            }
            return armed;
        case Doing::kReceiving: {
            if (cqe->res <= 0) {
                if (cqe->res == -ENOBUFS)
                    armed += receives(slot);
                else
                    armed += closes(slot);
                return armed;
            }
            const uint32_t which = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            const uint8_t *const taken = room_ + static_cast<size_t>(which) * kBufferBytes;
            size_t length = 0;
            const uint8_t *const answer =
                answering(taken, static_cast<size_t>(cqe->res), length);
            io_uring_buf_ring_add(buffers_, room_ + static_cast<size_t>(which) * kBufferBytes,
                                  kBufferBytes, static_cast<uint16_t>(which),
                                  io_uring_buf_ring_mask(kBufferCount), 0);
            io_uring_buf_ring_advance(buffers_, 1);
            if (answer != nullptr && length > 0) {
                io_uring_sqe *const sqe = room_for_one_more();
                io_uring_prep_send(sqe, static_cast<int>(slot), answer, length, MSG_NOSIGNAL);
                sqe->flags |= IOSQE_FIXED_FILE;
                io_uring_sqe_set_data64(sqe, marked(Doing::kSending, slot));
                armed++;
            }
            if ((cqe->flags & IORING_CQE_F_MORE) == 0)
                armed += receives(slot);
            return armed;
        }
        case Doing::kSending:
            if (cqe->res < 0)
                armed += closes(slot);
            return armed;
        case Doing::kClosing:
        default:
            return armed;
        }
    }

    io_uring ring_ = {};
    bool standing_ = false;
    io_uring_buf_ring *buffers_ = nullptr;
    uint8_t *room_ = nullptr;
    uint32_t connections_ = 0;
    uint32_t listeners_ = 0;
    uint16_t port_[kListeners] = {};
};

} // namespace wm
