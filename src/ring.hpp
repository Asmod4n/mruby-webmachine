#pragma once

#include <cerrno>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <deque>
#include <ctime>
#include <functional>
#include <memory>
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

#include "connection.hpp"
#include "http1.hpp"
#include "os_mapping.hpp"
#include "uring.hpp"

namespace wm
{

enum class Doing : uint8_t {
    kAccepting = 1,
    kReceiving,
    kSending,
    kClosing,
    kShuttingDown,
    kLingering,
    kCancelling,
    kRemovingTimer,
};

inline constexpr unsigned kSlotBits = 20;
inline constexpr unsigned kGenerationBits = 32;
inline constexpr unsigned kDoingShift = kSlotBits + kGenerationBits;
inline constexpr uint32_t kSlotsMost = uint32_t{1} << kSlotBits;

static_assert(kDoingShift + 8 <= 64, "a mark holds the slot, the generation and what was being done");

inline constexpr uint64_t marked(const Doing doing, const uint32_t slot, const uint32_t generation)
{
    return (static_cast<uint64_t>(doing) << kDoingShift) | (static_cast<uint64_t>(generation) << kSlotBits) |
           (slot & (kSlotsMost - 1));
}

inline constexpr Doing doing_of(const uint64_t mark)
{
    return static_cast<Doing>(mark >> kDoingShift);
}

inline constexpr uint32_t slot_of(const uint64_t mark)
{
    return static_cast<uint32_t>(mark & (kSlotsMost - 1));
}

inline constexpr uint32_t generation_of(const uint64_t mark)
{
    return static_cast<uint32_t>(mark >> kSlotBits);
}

struct Answered {
    size_t head;
    std::string_view body;
    const void *held;
    size_t taken;
    bool closes;
    bool refused;
};

class QueueIsFull : public std::runtime_error
{
  public:
    QueueIsFull() : std::runtime_error("the submission queue is full")
    {
    }
};

inline constexpr uint16_t kBufferGroup = 1;
inline constexpr uint32_t kBufferCount = 8192;
inline constexpr uint32_t kBufferBytes = 4096;
inline constexpr uint32_t kListeners = 4;
inline constexpr uint32_t kAnswerBytes = 16384;
inline constexpr uint32_t kPiecesMost = 16;
inline constexpr uint32_t kHeldMost = kPiecesMost / 2;
inline constexpr int kBacklog = 4096;
inline constexpr long kWaitSeconds = 1;
inline constexpr size_t kHeadEndBytes = 4;
inline constexpr size_t kRestBytes = http1::kMostRequestLineBytes + http1::kMostHeaderSectionBytes + 8;
struct Timeouts {
    std::chrono::seconds header;
    std::chrono::seconds body;
    std::chrono::seconds send;
    std::chrono::seconds idle;
    std::chrono::seconds linger;
};

inline constexpr Timeouts kTimeoutsByDefault = {std::chrono::seconds{60}, std::chrono::seconds{60},
                                               std::chrono::seconds{60}, std::chrono::seconds{75},
                                               std::chrono::seconds{5}};
inline constexpr std::chrono::seconds kLingerMost{30};

enum class Waiting : uint8_t {
    header,
    send,
    idle,
    nothing,
};

inline constexpr size_t kWaitingKinds = 3;

class Ring
{
  public:
    Ring() = default;
    Ring(const Ring &) = delete;
    Ring &operator=(const Ring &) = delete;

    int stood_up(const unsigned entries, const Timeouts timeouts)
    {
        if (timeouts.linger > kLingerMost || timeouts.linger.count() <= 0 || timeouts.header.count() <= 0 ||
            timeouts.send.count() <= 0 || timeouts.idle.count() <= 0 || timeouts.body.count() <= 0) [[unlikely]]
            return -EINVAL;
        timeouts_ = timeouts;
        linger_ = __kernel_timespec{timeouts.linger.count(), 0};
        started_ = std::chrono::steady_clock::now();
        rlimit limit = {};
        if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
            return -errno;
        const uint32_t open_files = limit.rlim_cur == RLIM_INFINITY
                                        ? 65536u
                                        : static_cast<uint32_t>(limit.rlim_cur);
        if (open_files <= kListeners)
            return -EMFILE;
        if (limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > kSlotsMost) [[unlikely]]
            return -ERANGE;
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

        auto room = os::Mapping::twice(static_cast<size_t>(kBufferCount) * kBufferBytes);
        if (!room)
            return -room.error().value();
        room_map_.emplace(std::move(*room));
        bundles_ = (ring_.features() & IORING_FEAT_RECVSEND_BUNDLE) != 0;

        auto owing = os::Mapping::reserved(static_cast<size_t>(connections_) * sizeof(Owed));
        if (!owing)
            return -ENOMEM;
        owed_map_.emplace(std::move(*owing));
        owed_ = reinterpret_cast<Owed *>(owed_map_->all().data());
        (void)owed_map_->advise_all(os::Advice::huge_page);
        heads_.assign(connections_, nullptr);
        rests_.assign(connections_, Rest{});

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
        sqe->set_data64(marked(Doing::kAccepting, slot, 0));
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
        sqe->set_data64(marked(Doing::kAccepting, slot, 0));
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

    template <class Answering, class TimingOut, class Releasing, class Waking>
    void serves(Answering answering, TimingOut timing_out, Releasing released, Waking woken, const bool &until)
    {
        while (!until) {
            const int waited = ring_.submit_and_wait_timeout(kWaitSeconds);
            if (waited < 0 && waited != -ETIME && waited != -EINTR)
                break;
            woken();
            now_ = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count());
            ring_.for_each_cqe([&](const uring::Cqe &cqe) { took(cqe, answering, released); });
            expires(timing_out, released);
            given_back();
        }
    }

  private:
    struct alignas(64) Owed {
        Connection connection;
        Waiting waiting;
        uint32_t deadline;
        std::array<uint32_t, kWaitingKinds> pushed;
        uint32_t filled;
        uint32_t first;
        uint32_t pieces;
        uint32_t helds;
        uint16_t running;
        uint8_t *room;
        iovec piece[kPiecesMost];
        const void *held[kHeldMost];
        msghdr message;

        static constexpr uint16_t bit_of(const Doing doing)
        {
            return static_cast<uint16_t>(1u << static_cast<unsigned>(doing));
        }

        constexpr bool runs(const Doing doing) const
        {
            return (running & bit_of(doing)) != 0;
        }

        constexpr void starts(const Doing doing)
        {
            if (runs(doing)) [[unlikely]]
                throw std::logic_error("one slot runs the same operation twice");
            running = static_cast<uint16_t>(running | bit_of(doing));
        }

        constexpr void finishes(const Doing doing)
        {
            running = static_cast<uint16_t>(running & ~bit_of(doing));
        }
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
        if (consumed_ == 0)
            return;
        for (uint32_t at = 0; at < consumed_; at++) {
            const uint16_t which = static_cast<uint16_t>((given_back_ + at) % kBufferCount);
            buffers_->buf_ring_add(receive_buffer(which), which, at);
        }
        buffers_->buf_ring_advance(consumed_);
        given_back_ += consumed_;
        consumed_ = 0;
    }

    static constexpr uint32_t buffers_of(const size_t bytes)
    {
        return static_cast<uint32_t>((bytes + kBufferBytes - 1) / kBufferBytes);
    }

    static void clears(Owed &owed)
    {
        const Connection kept = owed.connection;
        const uint16_t running = owed.running;
        owed = Owed{};
        owed.connection = kept;
        owed.running = running;
    }

    static std::span<std::byte> bytes_of(const iovec &piece)
    {
        return {static_cast<std::byte *>(piece.iov_base), piece.iov_len};
    }

    unsigned sends(const uint32_t slot)
    {
        Owed &owed = owed_[slot];
        if (owed.runs(Doing::kSending) || owed.first == owed.pieces)
            return 0;
        uring::Sqe sqe = room_for_one_more();
        if (owed.pieces - owed.first == 1) {
            sqe.prep_send(static_cast<int>(slot), bytes_of(owed.piece[owed.first]), MSG_NOSIGNAL | MSG_WAITALL);
        } else {
            owed.message = msghdr{};
            owed.message.msg_iov = owed.piece + owed.first;
            owed.message.msg_iovlen = owed.pieces - owed.first;
            sqe.prep_sendmsg(static_cast<int>(slot), owed.message, MSG_NOSIGNAL | MSG_WAITALL);
        }
        sqe.fixed_file();
        sqe.set_data64(marked(Doing::kSending, slot, owed.connection.generation));
        owed.starts(Doing::kSending);
        waits_for(slot, Waiting::send);
        return 1;
    }

    uint32_t seconds_of(const Waiting kind) const
    {
        switch (kind) {
            case Waiting::header: return static_cast<uint32_t>(timeouts_.header.count());
            case Waiting::send: return static_cast<uint32_t>(timeouts_.send.count());
            case Waiting::idle: return static_cast<uint32_t>(timeouts_.idle.count());
            default: throw std::logic_error("a deadline for no kind of waiting");
        }
    }

    void waits_for(const uint32_t slot, const Waiting kind)
    {
        Owed &owed = owed_[slot];
        if (kind == Waiting::nothing) {
            owed.waiting = Waiting::nothing;
            return;
        }
        if (kind == Waiting::header && owed.waiting == Waiting::header)
            return;
        const uint32_t at = now_ + seconds_of(kind);
        owed.waiting = kind;
        owed.deadline = at;
        uint32_t &pushed = owed.pushed.at(static_cast<size_t>(kind));
        if (pushed == at)
            return;
        pushed = at;
        deadlines_.at(static_cast<size_t>(kind)).push_back(Deadline{at, slot, owed.connection.generation});
    }

    void waits_after_the_answers(const uint32_t slot)
    {
        const Owed &owed = owed_[slot];
        if (owed.runs(Doing::kSending) || !owed.connection.reads())
            return;
        if (rests_.at(slot).needs_more)
            waits_for(slot, Waiting::header);
        else if (owed.connection.http == http1::State::persistent)
            waits_for(slot, Waiting::idle);
    }

    template <class TimingOut, class Releasing> void expires(TimingOut &timing_out, Releasing &released)
    {
        for (size_t kind = 0; kind < kWaitingKinds; kind++) {
            std::deque<Deadline> &waiting = deadlines_.at(kind);
            while (!waiting.empty() && waiting.front().at <= now_) {
                const Deadline due = waiting.front();
                waiting.pop_front();
                Owed &owed = owed_[due.slot];
                if (owed.connection.generation != due.generation || owed.waiting != static_cast<Waiting>(kind) ||
                    owed.deadline != due.at)
                    continue;
                owed.waiting = Waiting::nothing;
                times_out(due.slot, static_cast<Waiting>(kind), timing_out, released);
            }
        }
    }

    template <class TimingOut, class Releasing>
    unsigned times_out(const uint32_t slot, const Waiting kind, TimingOut &timing_out, Releasing &released)
    {
        Owed &owed = owed_[slot];
        if (kind == Waiting::send)
            return owed.connection.drains() ? ends_the_linger(slot) : closes(slot, released, -ETIMEDOUT);
        if (kind == Waiting::idle || rests_.at(slot).size == 0 || owed.runs(Doing::kSending))
            return closes(slot, released, 0);
        if (owed.room == nullptr)
            owed.room = room_taken(slot);
        const std::span<char> room(reinterpret_cast<char *>(owed.room + owed.filled), kAnswerBytes - owed.filled);
        const Answered said = timing_out(room);
        if (said.head == 0 || said.head > room.size()) [[unlikely]]
            return closes(slot, released, 0);
        owes_a_piece(owed, owed.room + owed.filled, said.head);
        owed.filled += static_cast<uint32_t>(said.head);
        rests_.at(slot) = Rest{};
        if (owed.connection.http == http1::State::persistent)
            owed.connection.moves_to(http1::State::header_section);
        owed.connection.moves_to(http1::State::client_error);
        owed.connection.moves_to(http1::State::tear_down);
        return sends(slot);
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
                !owed.runs(Doing::kSending)) {
                last.iov_len += length;
                return true;
            }
        }
        if (owed.pieces >= kPiecesMost)
            return false;
        owed.piece[owed.pieces++] = iovec{at, length};
        return true;
    }

    uint8_t *room_taken(const uint32_t slot)
    {
        std::shared_ptr<uint8_t[]> &head = heads_.at(slot);
        if (!head)
            head = std::make_shared_for_overwrite<uint8_t[]>(kAnswerBytes);
        return head.get();
    }

    void room_given_back(const uint32_t slot)
    {
        owed_[slot].room = nullptr;
    }

    template <class Releasing> void gives_back_everything(const uint32_t slot, Releasing &released)
    {
        Owed &owed = owed_[slot];
        rests_.at(slot) = Rest{};
        heads_.at(slot).reset();
        all_released(owed, released);
        room_given_back(slot);
        clears(owed);
    }

    template <class Preparing> unsigned starts(const uint32_t slot, const Doing doing, Preparing preparing)
    {
        Owed &owed = owed_[slot];
        owed.starts(doing);
        uring::Sqe sqe = room_for_one_more();
        preparing(sqe);
        sqe.set_data64(marked(doing, slot, owed.connection.generation));
        return 1;
    }

    unsigned receives(const uint32_t slot)
    {
        return starts(slot, Doing::kReceiving, [&](uring::Sqe &sqe) {
            sqe.prep_recv_multishot(static_cast<int>(slot));
            sqe.fixed_file();
            sqe.buffer_select(kBufferGroup);
            if (bundles_)
                sqe.recvsend_bundle();
        });
    }

    unsigned closes_the_slot(const uint32_t slot)
    {
        return starts(slot, Doing::kClosing, [&](uring::Sqe &sqe) { sqe.prep_close_direct(slot); });
    }

    unsigned lingers(const uint32_t slot)
    {
        unsigned armed = starts(slot, Doing::kShuttingDown, [&](uring::Sqe &sqe) {
            sqe.prep_shutdown(static_cast<int>(slot), SHUT_WR);
            sqe.fixed_file();
        });
        armed += starts(slot, Doing::kLingering, [&](uring::Sqe &sqe) { sqe.prep_timeout(linger_); });
        waits_for(slot, Waiting::nothing);
        if (!owed_[slot].runs(Doing::kReceiving))
            armed += receives(slot);
        return armed;
    }

    unsigned ends(const uint32_t slot)
    {
        const Owed &owed = owed_[slot];
        if (owed.running == 0)
            return closes_the_slot(slot);
        unsigned armed = 0;
        if (owed.runs(Doing::kLingering)) {
            const uint64_t timer = marked(Doing::kLingering, slot, owed.connection.generation);
            armed += starts(slot, Doing::kRemovingTimer,
                            [&](uring::Sqe &sqe) { sqe.prep_timeout_remove(timer); });
        }
        if (owed.runs(Doing::kReceiving) || owed.runs(Doing::kSending) || owed.runs(Doing::kShuttingDown)) {
            armed += starts(slot, Doing::kCancelling, [&](uring::Sqe &sqe) {
                sqe.prep_cancel_fd(static_cast<int>(slot), IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_FD_FIXED);
            });
        }
        return armed;
    }

    unsigned settles(const uint32_t slot)
    {
        const Owed &owed = owed_[slot];
        if (owed.running != 0)
            return 0;
        if (owed.connection.tcp != tcp::State::last_ack && owed.connection.tcp != tcp::State::closed)
            return 0;
        return closes_the_slot(slot);
    }

    struct Taken {
        size_t bytes;
        bool needs_more;
    };

    template <class Answering>
    Taken answers_from(const uint32_t slot, const std::string_view asked, Answering &answering)
    {
        Owed &owed = owed_[slot];
        if (owed.room == nullptr)
            owed.room = room_taken(slot);
        uint8_t *const into = owed.room;
        const size_t area = kAnswerBytes;
        std::string_view left = asked;
        bool answered = false;
        bool closing = false;
        bool refused = false;
        bool needs_more = false;
        while (!left.empty() && owed.filled < area && owed.pieces + 2 <= kPiecesMost && owed.helds < kHeldMost) {
            const std::span<char> room(reinterpret_cast<char *>(into + owed.filled), area - owed.filled);
            const Answered said = answering(left, room);
            if (said.held != nullptr)
                owed.held[owed.helds++] = said.held;
            if (said.taken == 0) {
                needs_more = true;
                break;
            }
            if (said.head == 0 || said.head > room.size())
                break;
            owes_a_piece(owed, into + owed.filled, said.head);
            owed.filled += static_cast<uint32_t>(said.head);
            owes_a_piece(owed, reinterpret_cast<uint8_t *>(const_cast<char *>(said.body.data())), said.body.size());
            left.remove_prefix(said.taken);
            answered = true;
            if (said.closes) {
                closing = true;
                refused = said.refused;
                break;
            }
        }
        if (answered && owed.connection.http != http1::State::response) {
            if (owed.connection.http == http1::State::persistent)
                owed.connection.moves_to(http1::State::header_section);
            owed.connection.moves_to(refused ? http1::State::client_error : http1::State::dispatch);
            if (!refused)
                owed.connection.moves_to(http1::State::response);
        }
        if (closing) {
            owed.connection.moves_to(http1::State::tear_down);
            return Taken{asked.size(), false};
        }
        return Taken{asked.size() - left.size(), needs_more};
    }

    bool keeps_the_rest(const uint32_t slot, const std::string_view left, const bool needs_more)
    {
        Rest &rest = rests_.at(slot);
        if (left.empty()) {
            rest = Rest{};
            return true;
        }
        if (!rest.bytes)
            rest.bytes = std::make_shared_for_overwrite<char[]>(kRestBytes + http::kWidePadding);
        const size_t kept = std::min(left.size(), kRestBytes);
        if (left.data() != rest.bytes.get())
            std::ranges::copy(left.substr(0, kept), rest.bytes.get());
        rest.size = static_cast<uint32_t>(kept);
        rest.needs_more = needs_more;
        return kept == left.size();
    }

    template <class Releasing> unsigned closes(const uint32_t slot, Releasing &released, const int result)
    {
        Owed &owed = owed_[slot];
        if (!owed.connection.is_open())
            return 0;
        const tcp::State to = tcp::state_after_close(owed.connection.tcp, result);
        owed.connection.moves_to(to);
        if (owed.runs(Doing::kSending))
            return to == tcp::State::fin_wait_1 ? 0 : ends(slot);
        gives_back_everything(slot, released);
        return to == tcp::State::fin_wait_1 ? lingers(slot) : ends(slot);
    }

    unsigned ends_the_linger(const uint32_t slot)
    {
        owed_[slot].connection.moves_to(tcp::State::closed);
        return ends(slot);
    }

    template <class Answering, class Releasing>
    unsigned took(const uring::Cqe &cqe, Answering &answering, Releasing &released)
    {
        unsigned armed = 0;
        const uint64_t mark = cqe.data;
        const uint32_t slot = slot_of(mark);
        const Doing doing = doing_of(mark);
        const bool stale = doing != Doing::kAccepting &&
                           (slot >= connections_ || generation_of(mark) != owed_[slot].connection.generation);
        if (stale) [[unlikely]] {
            if (doing == Doing::kReceiving && (cqe.flags & IORING_CQE_F_BUFFER) != 0 && cqe.res > 0)
                consumed_ += buffers_of(static_cast<size_t>(cqe.res));
            return armed;
        }
        switch (doing) {
        case Doing::kAccepting:
            if (cqe.res >= 0) {
                Owed &fresh = owed_[cqe.res];
                const Connection next = accepted_after(fresh.connection);
                fresh = Owed{};
                fresh.connection = next;
                rests_.at(static_cast<size_t>(cqe.res)) = Rest{};
                fresh.waiting = Waiting::nothing;
                waits_for(static_cast<uint32_t>(cqe.res), Waiting::header);
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
            Owed &owed = owed_[slot];
            const bool last = (cqe.flags & IORING_CQE_F_MORE) == 0;
            if (last)
                owed.finishes(Doing::kReceiving);
            if (cqe.res <= 0) {
                if (cqe.res == -ENOBUFS) {
                    if (last && (owed.connection.reads() || owed.connection.drains()))
                        return receives(slot);
                    return settles(slot);
                }
                if (owed.connection.drains())
                    return ends_the_linger(slot);
                if (!owed.connection.is_open())
                    return settles(slot);
                if (cqe.res == 0 && owed.connection.reads()) {
                    owed.connection.moves_to(tcp::State::close_wait);
                    if (owed.runs(Doing::kSending) || owed.first != owed.pieces)
                        return armed;
                }
                return closes(slot, released, cqe.res);
            }
            const uint32_t which = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
            const size_t took_bytes = static_cast<size_t>(cqe.res);
            const size_t from = static_cast<size_t>(which) * kBufferBytes;
            const size_t pool = static_cast<size_t>(kBufferCount) * kBufferBytes;
            consumed_ += buffers_of(took_bytes);
            if (!owed.connection.reads() || owed.connection.http == http1::State::tear_down) {
                if (last && (owed.connection.reads() || owed.connection.drains()))
                    return receives(slot);
                return settles(slot);
            }
            if (which >= kBufferCount || took_bytes > pool) [[unlikely]]
                return closes(slot, released, -EIO);
            const std::span<std::byte> bundle = room_map_->all_and_its_start_again().subspan(from, took_bytes);
            const std::string_view received(reinterpret_cast<const char *>(bundle.data()), bundle.size());
            Rest &rest = rests_.at(slot);
            std::string_view asked = received;
            bool fits = true;
            if (rest.size > 0) {
                const size_t before = rest.size;
                const size_t appended = std::min(kRestBytes - rest.size, received.size());
                fits = appended == received.size();
                std::ranges::copy(received.substr(0, appended), std::next(rest.bytes.get(), static_cast<std::ptrdiff_t>(rest.size)));
                rest.size += appended;
                asked = std::string_view(rest.bytes.get(), rest.size);
                const size_t join = before >= kHeadEndBytes - 1 ? before - (kHeadEndBytes - 1) : 0;
                if (rest.needs_more && fits && rest.size <= http1::kMostRequestLineBytes + http1::kMostHeaderSectionBytes &&
                    http1::bytes_before_the_body(asked.substr(join)) == 0) {
                    if (last)
                        armed += receives(slot);
                    return armed;
                }
            }
            const Taken taken = answers_from(slot, asked, answering);
            fits = keeps_the_rest(slot, asked.substr(taken.bytes), taken.needs_more) && fits;
            if (!fits) [[unlikely]] {
                if (owed.first == owed.pieces)
                    return closes(slot, released, 0);
                if (owed.connection.http != http1::State::tear_down)
                    owed.connection.moves_to(http1::State::tear_down);
            }
            armed += sends(slot);
            waits_after_the_answers(slot);
            if (last)
                armed += receives(slot);
            return armed;
        }
        case Doing::kSending: {
            Owed &owed = owed_[slot];
            owed.finishes(Doing::kSending);
            if (!owed.connection.is_open()) {
                gives_back_everything(slot, released);
                if (!owed.connection.drains())
                    return settles(slot);
                if (cqe.res == -ECONNRESET || cqe.res == -EPIPE)
                    return ends_the_linger(slot);
                return lingers(slot);
            }
            if (cqe.res <= 0)
                return closes(slot, released, cqe.res);
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
                room_given_back(slot);
                owed.first = owed.pieces = 0;
                owed.filled = 0;
                if (owed.connection.http == http1::State::tear_down)
                    return armed + closes(slot, released, 0);
                owed.connection.moves_to(http1::State::persistent);
                const Rest &rest = rests_.at(slot);
                if (rest.size > 0 && !rest.needs_more) {
                    const std::string_view asked(rest.bytes.get(), rest.size);
                    const Taken taken = answers_from(slot, asked, answering);
                    keeps_the_rest(slot, asked.substr(taken.bytes), taken.needs_more);
                    armed += sends(slot);
                }
                if (owed.connection.tcp == tcp::State::close_wait && owed.first == owed.pieces)
                    return armed + closes(slot, released, 0);
                waits_after_the_answers(slot);
            } else {
                armed += sends(slot);
            }
            return armed;
        }
        case Doing::kShuttingDown: {
            Owed &owed = owed_[slot];
            owed.finishes(Doing::kShuttingDown);
            if (cqe.res < 0 && owed.connection.drains())
                return ends_the_linger(slot);
            return settles(slot);
        }
        case Doing::kLingering: {
            Owed &owed = owed_[slot];
            owed.finishes(Doing::kLingering);
            if (cqe.res == -ETIME && owed.connection.drains())
                return ends_the_linger(slot);
            return settles(slot);
        }
        case Doing::kCancelling:
        case Doing::kRemovingTimer:
            owed_[slot].finishes(doing);
            return settles(slot);
        case Doing::kClosing: {
            Owed &owed = owed_[slot];
            owed.finishes(Doing::kClosing);
            if (owed.connection.tcp == tcp::State::last_ack)
                owed.connection.moves_to(tcp::State::closed);
            return armed;
        }
        default:
            return armed;
        }
    }

    uring::Ring ring_;
    std::optional<uring::BufferRing> buffers_;
    std::optional<os::Mapping> room_map_;
    std::optional<os::Mapping> owed_map_;
    std::vector<std::shared_ptr<uint8_t[]>> heads_;
    struct Rest {
        std::shared_ptr<char[]> bytes;
        uint32_t size;
        bool needs_more;
    };
    std::vector<Rest> rests_;
    struct Deadline {
        uint32_t at;
        uint32_t slot;
        uint32_t generation;
    };
    std::array<std::deque<Deadline>, kWaitingKinds> deadlines_;
    Timeouts timeouts_ = kTimeoutsByDefault;
    __kernel_timespec linger_ = {};
    std::chrono::steady_clock::time_point started_;
    uint32_t now_ = 0;
    uint32_t consumed_ = 0;
    uint32_t given_back_ = 0;
    bool bundles_ = false;
    Owed *owed_ = nullptr;
    unsigned entries_ = 0;
    uint32_t connections_ = 0;
    uint32_t listeners_ = 0;
    uint16_t port_[kListeners] = {};
};

} // namespace wm
