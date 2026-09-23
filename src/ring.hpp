#pragma once

#include <cerrno>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>

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

struct Answered {
    size_t head;
    std::string_view body;
    const void *held;
    size_t taken;
};

class QueueIsFull : public std::runtime_error
{
  public:
    QueueIsFull() : std::runtime_error("the submission queue is full")
    {
    }
};

inline constexpr uint16_t kBufferGroup = 1;
inline constexpr uint32_t kBufferCount = 2048;
inline constexpr uint32_t kBufferBytes = 4096;
inline constexpr uint32_t kListeners = 4;
inline constexpr uint32_t kAnswerBytes = 16384;
inline constexpr uint32_t kPiecesMost = 16;
inline constexpr uint32_t kHeldMost = kPiecesMost / 2;
inline constexpr size_t kBufferRoomLeast = 512;

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
        if (answers_ != nullptr)
            munmap(answers_, static_cast<size_t>(connections_) * kAnswerBytes);
        if (owed_ != nullptr)
            munmap(owed_, static_cast<size_t>(connections_) * sizeof(Owed));
        free(free_rooms_);
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

        constexpr unsigned kAsked = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN |
                                    IORING_SETUP_COOP_TASKRUN;
        const unsigned floor = entries < 1024 ? entries : 1024;
        int begun = -EINVAL;
        for (unsigned wanted = entries;; wanted /= 2) {
            io_uring_params asking = {};
            asking.flags = kAsked;
            begun = io_uring_queue_init_params(wanted, &ring_, &asking);
            if (begun == 0) {
                entries_ = asking.sq_entries;
                break;
            }
            if (wanted <= floor)
                return begun;
        }
        standing_ = true;
        const int by_index = io_uring_register_ring_fd(&ring_);
        if (by_index < 0 && by_index != -EINVAL)
            return by_index;

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
        madvise(mapped, static_cast<size_t>(kBufferCount) * kBufferBytes, MADV_HUGEPAGE);

        void *const answering = mmap(nullptr, static_cast<size_t>(connections_) * kAnswerBytes,
                                     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (answering == MAP_FAILED)
            return -errno;
        answers_ = static_cast<uint8_t *>(answering);
        madvise(answering, static_cast<size_t>(connections_) * kAnswerBytes, MADV_HUGEPAGE);
        void *const owing = mmap(nullptr, static_cast<size_t>(connections_) * sizeof(Owed), PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        owed_ = owing == MAP_FAILED ? nullptr : static_cast<Owed *>(owing);
        if (owed_ != nullptr)
            madvise(owing, static_cast<size_t>(connections_) * sizeof(Owed), MADV_HUGEPAGE);
        if (owed_ == nullptr)
            return -ENOMEM;
        free_rooms_ = static_cast<uint8_t **>(calloc(connections_, sizeof *free_rooms_));
        if (free_rooms_ == nullptr)
            return -ENOMEM;
        bundles_ = (ring_.features & IORING_FEAT_RECVSEND_BUNDLE) != 0;

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

    int listens_on(const char *const path)
    {
        if (listeners_ >= kListeners)
            return -ENOSPC;
        if (path == nullptr || *path == '\0')
            return -EINVAL;
        const uint32_t slot = connections_ + listeners_;

        io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_socket_direct(sqe, AF_UNIX, SOCK_STREAM, 0, slot, 0);
        int answer = one_at_a_time(sqe);
        if (answer < 0)
            return answer;

        sockaddr_un where = {};
        where.sun_family = AF_UNIX;
        const size_t room = sizeof where.sun_path - 1;
        const size_t length = strlen(path);
        if (length > room)
            return -ENAMETOOLONG;
        memcpy(where.sun_path, path, length);
        unlink(path);
        sqe = io_uring_get_sqe(&ring_);
        if (sqe == nullptr)
            return -EBUSY;
        io_uring_prep_bind(sqe, static_cast<int>(slot), reinterpret_cast<sockaddr *>(&where),
                           sizeof where);
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

    unsigned entries_taken() const
    {
        return entries_;
    }

    uint16_t port_taken(const uint32_t which) const
    {
        return which < kListeners ? port_[which] : 0;
    }

    template <class Answering, class Releasing, class Waking>
    void serves(Answering answering, Releasing released, Waking woken, const bool &until)
    {
        while (!until) {
            io_uring_cqe *first = nullptr;
            __kernel_timespec a_second = {1, 0};
            const int waited =
                io_uring_submit_and_wait_timeout(&ring_, &first, 1, &a_second, nullptr);
            if (waited < 0 && waited != -ETIME && waited != -EINTR)
                break;
            woken();
            io_uring_cqe *cqe = nullptr;
            unsigned head = 0;
            unsigned seen = 0;
            io_uring_for_each_cqe(&ring_, head, cqe)
            {
                took(cqe, answering, released);
                seen++;
            }
            io_uring_cq_advance(&ring_, seen);
            given_back();
        }
    }

  private:
    struct alignas(64) Owed {
        uint32_t filled;
        uint32_t first;
        uint32_t pieces;
        uint32_t helds;
        bool sending;
        bool closing;
        uint16_t bid;
        bool holds_bid;
        uint8_t *room;
        iovec piece[kPiecesMost];
        const void *held[kHeldMost];
        msghdr message;
    };

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

    void given_back()
    {
        if (returned_count_ == 0)
            return;
        const int mask = io_uring_buf_ring_mask(kBufferCount);
        for (uint32_t at = 0; at < returned_count_; at++) {
            const uint16_t which = returned_[at];
            io_uring_buf_ring_add(buffers_, room_ + static_cast<size_t>(which) * kBufferBytes, kBufferBytes, which,
                                  mask, static_cast<int>(at));
        }
        io_uring_buf_ring_advance(buffers_, static_cast<int>(returned_count_));
        returned_count_ = 0;
    }

    void buffer_returned(const uint16_t which)
    {
        returned_[returned_count_++] = which;
    }

    void bid_given_back(Owed &owed)
    {
        if (!owed.holds_bid)
            return;
        buffer_returned(owed.bid);
        owed.holds_bid = false;
    }

    unsigned sends(const uint32_t slot)
    {
        Owed &owed = owed_[slot];
        if (owed.sending || owed.first == owed.pieces)
            return 0;
        io_uring_sqe *const sqe = room_for_one_more();
        if (owed.pieces - owed.first == 1) {
            const iovec &only = owed.piece[owed.first];
            io_uring_prep_send(sqe, static_cast<int>(slot), only.iov_base, only.iov_len, MSG_NOSIGNAL | MSG_WAITALL);
        } else {
            owed.message = msghdr{};
            owed.message.msg_iov = owed.piece + owed.first;
            owed.message.msg_iovlen = owed.pieces - owed.first;
            io_uring_prep_sendmsg(sqe, static_cast<int>(slot), &owed.message, MSG_NOSIGNAL | MSG_WAITALL);
        }
        sqe->flags |= IOSQE_FIXED_FILE;
        io_uring_sqe_set_data64(sqe, marked(Doing::kSending, slot));
        owed.sending = true;
        return 1;
    }

    template <class Releasing> void all_released(Owed &owed, Releasing &released)
    {
        for (uint32_t at = 0; at < owed.helds; at++)
            released(owed.held[at]);
        owed.helds = 0;
    }

    bool owes_a_piece(Owed &owed, uint8_t *const at, const size_t length)
    {
        if (length == 0)
            return true;
        if (owed.pieces > 0) {
            iovec &last = owed.piece[owed.pieces - 1];
            if (static_cast<uint8_t *>(last.iov_base) + last.iov_len == at && owed.first < owed.pieces &&
                !owed.sending) {
                last.iov_len += length;
                return true;
            }
        }
        if (owed.pieces >= kPiecesMost)
            return false;
        owed.piece[owed.pieces++] = iovec{at, length};
        return true;
    }

    uint8_t *room_taken()
    {
        if (free_room_count_ > 0)
            return free_rooms_[--free_room_count_];
        if (fresh_rooms_ < connections_)
            return answers_ + static_cast<size_t>(fresh_rooms_++) * kAnswerBytes;
        return nullptr;
    }

    void room_given_back(Owed &owed)
    {
        if (owed.room == nullptr)
            return;
        free_rooms_[free_room_count_++] = owed.room;
        owed.room = nullptr;
    }

    template <class Releasing> unsigned closes(const uint32_t slot, Releasing &released)
    {
        Owed &owed = owed_[slot];
        if (owed.closing)
            return 0;
        if (owed.sending) {
            owed.closing = true;
        } else {
            all_released(owed, released);
            room_given_back(owed);
            bid_given_back(owed);
            owed = Owed{};
        }
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

    template <class Answering, class Releasing>
    unsigned took(io_uring_cqe *const cqe, Answering &answering, Releasing &released)
    {
        unsigned armed = 0;
        const uint64_t mark = io_uring_cqe_get_data64(cqe);
        const uint32_t slot = slot_of(mark);
        switch (doing_of(mark)) {
        case Doing::kAccepting:
            if (cqe->res >= 0) {
                owed_[cqe->res] = Owed{};
                armed += receives(static_cast<uint32_t>(cqe->res));
            }
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
                    armed += closes(slot, released);
                return armed;
            }
            const uint32_t which = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            const size_t took_bytes = static_cast<size_t>(cqe->res);
            const size_t from = static_cast<size_t>(which) * kBufferBytes;
            const size_t pool = static_cast<size_t>(kBufferCount) * kBufferBytes;
            if (from + took_bytes > pool) {
                buffer_returned(static_cast<uint16_t>(which));
                return armed + closes(slot, released);
            }
            Owed &owed = owed_[slot];
            if (owed.closing) {
                buffer_returned(static_cast<uint16_t>(which));
                return armed;
            }
            const bool in_the_buffer = owed.room == nullptr && !owed.holds_bid && !owed.sending &&
                                       owed.first == owed.pieces && kBufferBytes - took_bytes >= kBufferRoomLeast;
            if (!in_the_buffer && owed.room == nullptr) {
                owed.room = room_taken();
                if (owed.room == nullptr) [[unlikely]] {
                    buffer_returned(static_cast<uint16_t>(which));
                    return armed + closes(slot, released);
                }
            }
            uint8_t *const into = in_the_buffer ? room_ + from + took_bytes : owed.room;
            const size_t area = in_the_buffer ? kBufferBytes - took_bytes : kAnswerBytes;
            std::string_view left(reinterpret_cast<const char *>(room_ + from), took_bytes);
            while (!left.empty() && owed.filled < area && owed.pieces + 2 <= kPiecesMost && owed.helds < kHeldMost) {
                const std::span<char> room(reinterpret_cast<char *>(into + owed.filled), area - owed.filled);
                const Answered said = answering(left, room);
                if (said.held != nullptr)
                    owed.held[owed.helds++] = said.held;
                if (said.taken == 0)
                    break;
                if (said.head == 0 || said.head > room.size())
                    break;
                owes_a_piece(owed, into + owed.filled, said.head);
                owed.filled += static_cast<uint32_t>(said.head);
                owes_a_piece(owed, reinterpret_cast<uint8_t *>(const_cast<char *>(said.body.data())),
                             said.body.size());
                left.remove_prefix(said.taken);
            }
            if (in_the_buffer && owed.first != owed.pieces) {
                owed.bid = static_cast<uint16_t>(which);
                owed.holds_bid = true;
            } else {
                buffer_returned(static_cast<uint16_t>(which));
            }
            armed += sends(slot);
            if ((cqe->flags & IORING_CQE_F_MORE) == 0)
                armed += receives(slot);
            return armed;
        }
        case Doing::kSending: {
            Owed &owed = owed_[slot];
            owed.sending = false;
            if (owed.closing) {
                all_released(owed, released);
                room_given_back(owed);
                bid_given_back(owed);
                owed = Owed{};
                return armed;
            }
            if (cqe->res <= 0)
                return armed + closes(slot, released);
            size_t sent = static_cast<size_t>(cqe->res);
            while (sent > 0 && owed.first < owed.pieces) {
                iovec &piece = owed.piece[owed.first];
                if (sent < piece.iov_len) {
                    piece.iov_base = static_cast<uint8_t *>(piece.iov_base) + sent;
                    piece.iov_len -= sent;
                    sent = 0;
                } else {
                    sent -= piece.iov_len;
                    owed.first++;
                }
            }
            if (owed.first == owed.pieces) {
                all_released(owed, released);
                room_given_back(owed);
                bid_given_back(owed);
                owed.first = owed.pieces = 0;
                owed.filled = 0;
            } else {
                armed += sends(slot);
            }
            return armed;
        }
        case Doing::kClosing:
        default:
            return armed;
        }
    }

    io_uring ring_ = {};
    bool standing_ = false;
    io_uring_buf_ring *buffers_ = nullptr;
    uint8_t *room_ = nullptr;
    uint8_t *answers_ = nullptr;
    uint8_t **free_rooms_ = nullptr;
    uint32_t free_room_count_ = 0;
    uint32_t fresh_rooms_ = 0;
    uint16_t returned_[kBufferCount] = {};
    uint32_t returned_count_ = 0;
    Owed *owed_ = nullptr;
    bool bundles_ = false;
    unsigned entries_ = 0;
    uint32_t connections_ = 0;
    uint32_t listeners_ = 0;
    uint16_t port_[kListeners] = {};
};

} // namespace wm
