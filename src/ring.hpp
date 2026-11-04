#pragma once

#include <cerrno>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <functional>
#include <optional>
#include <vector>
#include <span>
#include <string_view>
#include <chrono>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "os_mapping.hpp"
#include "uring.hpp"

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
inline constexpr uint16_t kSendGroupFirst = 2;
inline constexpr unsigned kSendEntries = 8;
inline constexpr uint32_t kSendGroupEpochsIdle = 2;
inline constexpr int kBacklog = 4096;
inline constexpr long kWaitSeconds = 1;

class Ring
{
  public:
    Ring() = default;
    Ring(const Ring &) = delete;
    Ring &operator=(const Ring &) = delete;

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
        for (unsigned wanted = entries;; wanted /= 2) {
            const auto begun = ring_.queue_init_params(wanted, kAsked);
            if (begun) {
                entries_ = *begun;
                break;
            }
            if (wanted <= floor)
                return -begun.error().value();
        }
        const auto by_index = ring_.register_ring_fd();
        if (!by_index && by_index.error().value() != EINVAL)
            return -by_index.error().value();

        const auto sparse = ring_.register_files_sparse(connections_ + kListeners);
        if (!sparse)
            return -sparse.error().value();
        const auto ranged = ring_.register_file_alloc_range(0, connections_);
        if (!ranged)
            return -ranged.error().value();

        auto room = os::Mapping::reserved(static_cast<size_t>(kBufferCount) * kBufferBytes);
        if (!room)
            return -room.error().value();
        room_map_.emplace(std::move(*room));
        room_ = reinterpret_cast<uint8_t *>(room_map_->all().data());
        (void)room_map_->advise_all(os::Advice::huge_page);

        auto answering = os::Mapping::reserved(static_cast<size_t>(connections_) * kAnswerBytes);
        if (!answering)
            return -answering.error().value();
        answers_map_.emplace(std::move(*answering));
        answers_ = reinterpret_cast<uint8_t *>(answers_map_->all().data());
        (void)answers_map_->advise_all(os::Advice::huge_page);
        auto owing = os::Mapping::reserved(static_cast<size_t>(connections_) * sizeof(Owed));
        if (!owing)
            return -ENOMEM;
        owed_map_.emplace(std::move(*owing));
        owed_ = reinterpret_cast<Owed *>(owed_map_->all().data());
        (void)owed_map_->advise_all(os::Advice::huge_page);
        free_rooms_.assign(connections_, nullptr);

        auto buffers = uring::BufferRing::setup_buf_ring(ring_, kBufferCount, kBufferGroup);
        if (!buffers)
            return -buffers.error().value();
        buffers_.emplace(std::move(*buffers));
        for (uint32_t at = 0; at < kBufferCount; at++)
            buffers_->buf_ring_add(receive_buffer(static_cast<uint16_t>(at)), static_cast<uint16_t>(at), at);
        buffers_->buf_ring_advance(kBufferCount);
        return 0;
    }

    int listens_on(const char *const path)
    {
        if (listeners_ >= kListeners)
            return -ENOSPC;
        if (path == nullptr || *path == '\0')
            return -EINVAL;
        const uint32_t slot = connections_ + listeners_;
        const int fd = static_cast<int>(slot);

        auto sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_socket_direct(AF_UNIX, SOCK_STREAM, 0, slot);
        int answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sockaddr_un where = {};
        where.sun_family = AF_UNIX;
        const std::string_view named(path);
        if (named.size() > sizeof where.sun_path - 1)
            return -ENAMETOOLONG;
        std::ranges::copy(named, std::begin(where.sun_path));
        unlink(path);
        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_bind(fd, where);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_listen(fd, kBacklog);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_multishot_accept_direct(fd);
        sqe->fixed_file();
        sqe->set_data64(marked(Doing::kAccepting, slot));
        ring_.submit();
        listeners_++;
        return 0;
    }

    int listens_on(const uint16_t port)
    {
        if (listeners_ >= kListeners)
            return -ENOSPC;
        const uint32_t slot = connections_ + listeners_;
        const int fd = static_cast<int>(slot);

        auto sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_socket_direct(AF_INET, SOCK_STREAM, 0, slot);
        int answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        const int on = 1;
        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_cmd_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, on);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sockaddr_in where = {};
        where.sin_family = AF_INET;
        where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        where.sin_port = htons(port);
        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_bind(fd, where);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_listen(fd, kBacklog);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        if (answer < 0)
            return answer;

        sockaddr_in took = {};
        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_cmd_getsockname(fd, took);
        sqe->fixed_file();
        answer = one_at_a_time(*sqe);
        port_[listeners_] = answer >= 0 ? ntohs(took.sin_port) : port;

        sqe = ring_.get_sqe();
        if (!sqe)
            return -EBUSY;
        sqe->prep_multishot_accept_direct(fd);
        sqe->fixed_file();
        sqe->set_data64(marked(Doing::kAccepting, slot));
        ring_.submit();
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
            const int waited = ring_.submit_and_wait_timeout(kWaitSeconds);
            if (waited < 0 && waited != -ETIME && waited != -EINTR)
                break;
            woken();
            const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            if (now != send_epoch_at_) {
                send_epoch_at_ = now;
                send_epoch_++;
            }
            send_group_collected_step();
            ring_.for_each_cqe([&](const uring::Cqe &cqe) { took(cqe, answering, released); });
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
        bool holds_group;
        uint16_t group;
        uint8_t *room;
        iovec piece[kPiecesMost];
        const void *held[kHeldMost];
        msghdr message;
    };

    int one_at_a_time(uring::Sqe &sqe)
    {
        sqe.set_data64(0);
        ring_.submit();
        const auto cqe = ring_.wait_cqe();
        if (!cqe)
            return -EIO;
        return cqe->res;
    }

    uring::Sqe room_for_one_more()
    {
        auto sqe = ring_.get_sqe();
        if (sqe)
            return *sqe;
        ring_.submit();
        sqe = ring_.get_sqe();
        if (!sqe)
            throw QueueIsFull();
        return *sqe;
    }

    std::span<std::byte> receive_buffer(const uint16_t which) const
    {
        return room_map_->all().subspan(static_cast<size_t>(which) * kBufferBytes, kBufferBytes);
    }

    void given_back()
    {
        if (returned_count_ == 0)
            return;
        for (uint32_t at = 0; at < returned_count_; at++) {
            const uint16_t which = returned_[at];
            buffers_->buf_ring_add(receive_buffer(which), which, at);
        }
        buffers_->buf_ring_advance(returned_count_);
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

    std::optional<uint16_t> send_group_taken()
    {
        while (!free_send_groups_.empty()) {
            std::ranges::pop_heap(free_send_groups_, std::ranges::greater{});
            const uint16_t at = free_send_groups_.back();
            free_send_groups_.pop_back();
            if (at >= send_groups_.size() || !send_groups_[at].free)
                continue;
            send_groups_[at].free = false;
            send_groups_[at].used_in = send_epoch_;
            return at;
        }
        if (send_groups_.size() + kSendGroupFirst > 0xFFFF)
            return std::nullopt;
        const uint16_t at = static_cast<uint16_t>(send_groups_.size());
        auto buffers =
            uring::BufferRing::setup_buf_ring(ring_, kSendEntries, static_cast<uint16_t>(kSendGroupFirst + at));
        if (!buffers) [[unlikely]]
            return std::nullopt;
        send_groups_.push_back(SendGroup{std::move(*buffers), send_epoch_, false});
        return at;
    }

    void send_group_given_back(Owed &owed)
    {
        if (!owed.holds_group)
            return;
        send_groups_[owed.group].free = true;
        free_send_groups_.push_back(owed.group);
        std::ranges::push_heap(free_send_groups_, std::ranges::greater{});
        owed.holds_group = false;
    }

    void send_group_collected_step()
    {
        if (send_groups_.empty())
            return;
        const SendGroup &youngest = send_groups_.back();
        if (!youngest.free || send_epoch_ - youngest.used_in < kSendGroupEpochsIdle)
            return;
        send_groups_.pop_back();
    }

    static std::span<std::byte> bytes_of(const iovec &piece)
    {
        return {static_cast<std::byte *>(piece.iov_base), piece.iov_len};
    }

    bool in_the_receive_pool(const iovec &piece) const
    {
        const uint8_t *const at = static_cast<const uint8_t *>(piece.iov_base);
        return at >= room_ && at + piece.iov_len <= room_ + static_cast<size_t>(kBufferCount) * kBufferBytes;
    }

    unsigned sends(const uint32_t slot)
    {
        Owed &owed = owed_[slot];
        if (owed.sending || owed.first == owed.pieces)
            return 0;
        uring::Sqe sqe = room_for_one_more();
        if (owed.pieces - owed.first == 1 && in_the_receive_pool(owed.piece[owed.first])) {
            const std::optional<uint16_t> group = send_group_taken();
            if (group) {
                owed.group = *group;
                owed.holds_group = true;
                uring::BufferRing &own = send_groups_[*group].buffers;
                own.buf_ring_add(bytes_of(owed.piece[owed.first]), 0, 0);
                own.buf_ring_advance(1);
                sqe.prep_send_selected(static_cast<int>(slot), MSG_NOSIGNAL | MSG_WAITALL);
                sqe.fixed_file();
                sqe.buffer_select(own.group());
                sqe.set_data64(marked(Doing::kSending, slot));
                owed.sending = true;
                return 1;
            }
        }
        if (owed.pieces - owed.first == 1) {
            sqe.prep_send(static_cast<int>(slot), bytes_of(owed.piece[owed.first]), MSG_NOSIGNAL | MSG_WAITALL);
        } else {
            owed.message = msghdr{};
            owed.message.msg_iov = owed.piece + owed.first;
            owed.message.msg_iovlen = owed.pieces - owed.first;
            sqe.prep_sendmsg(static_cast<int>(slot), owed.message, MSG_NOSIGNAL | MSG_WAITALL);
        }
        sqe.fixed_file();
        sqe.set_data64(marked(Doing::kSending, slot));
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
            send_group_given_back(owed);
            owed = Owed{};
        }
        uring::Sqe sqe = room_for_one_more();
        sqe.prep_close_direct(slot);
        sqe.set_data64(marked(Doing::kClosing, slot));
        return 1;
    }

    unsigned receives(const uint32_t slot)
    {
        uring::Sqe sqe = room_for_one_more();
        sqe.prep_recv_multishot(static_cast<int>(slot));
        sqe.fixed_file();
        sqe.buffer_select(kBufferGroup);
        sqe.set_data64(marked(Doing::kReceiving, slot));
        return 1;
    }

    template <class Answering, class Releasing>
    unsigned took(const uring::Cqe &cqe, Answering &answering, Releasing &released)
    {
        unsigned armed = 0;
        const uint64_t mark = cqe.data;
        const uint32_t slot = slot_of(mark);
        switch (doing_of(mark)) {
        case Doing::kAccepting:
            if (cqe.res >= 0) {
                owed_[cqe.res] = Owed{};
                armed += receives(static_cast<uint32_t>(cqe.res));
            }
            if ((cqe.flags & IORING_CQE_F_MORE) == 0) {
                uring::Sqe sqe = room_for_one_more();
                sqe.prep_multishot_accept_direct(static_cast<int>(slot));
                sqe.fixed_file();
                sqe.set_data64(mark);
                armed++;
            }
            return armed;
        case Doing::kReceiving: {
            if (cqe.res <= 0) {
                if (cqe.res == -ENOBUFS)
                    armed += receives(slot);
                else
                    armed += closes(slot, released);
                return armed;
            }
            const uint32_t which = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
            const size_t took_bytes = static_cast<size_t>(cqe.res);
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
            if ((cqe.flags & IORING_CQE_F_MORE) == 0)
                armed += receives(slot);
            return armed;
        }
        case Doing::kSending: {
            Owed &owed = owed_[slot];
            owed.sending = false;
            send_group_given_back(owed);
            if (owed.closing) {
                all_released(owed, released);
                room_given_back(owed);
                bid_given_back(owed);
                owed = Owed{};
                return armed;
            }
            if (cqe.res <= 0)
                return armed + closes(slot, released);
            size_t sent = static_cast<size_t>(cqe.res);
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

    uring::Ring ring_;
    std::optional<uring::BufferRing> buffers_;
    std::optional<os::Mapping> room_map_;
    std::optional<os::Mapping> answers_map_;
    std::optional<os::Mapping> owed_map_;
    uint8_t *room_ = nullptr;
    uint8_t *answers_ = nullptr;
    std::vector<uint8_t *> free_rooms_;
    struct SendGroup {
        uring::BufferRing buffers;
        uint32_t used_in;
        bool free;
    };
    std::vector<SendGroup> send_groups_;
    std::vector<uint16_t> free_send_groups_;
    uint32_t send_epoch_ = 0;
    std::chrono::sys_seconds send_epoch_at_{};
    uint32_t free_room_count_ = 0;
    uint32_t fresh_rooms_ = 0;
    uint16_t returned_[kBufferCount] = {};
    uint32_t returned_count_ = 0;
    Owed *owed_ = nullptr;
    unsigned entries_ = 0;
    uint32_t connections_ = 0;
    uint32_t listeners_ = 0;
    uint16_t port_[kListeners] = {};
};

} // namespace wm
