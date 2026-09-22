#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <liburing.h>
#include <lmdb.h>
#include <slipstream_syscall.h>

#include "../../src/cache.h"
#include "../../src/cache_datagram.h"

enum { kFirstFd = 3, kBufferGroup = 1 };

enum : unsigned { kMostRingEntries = 32768 };

static unsigned no_more_than_a_power_of_two(const unsigned wanted, const unsigned most)
{
    unsigned taken = 1;
    while (taken * 2 <= wanted && taken * 2 <= most)
        taken *= 2;
    return taken;
}

struct writing {
    MDB_env *environment;
    MDB_dbi fields;
    MDB_dbi bodies;
    MDB_dbi due;
    struct io_uring *ring;
    int connections;
    unsigned long dropped;
    MDB_txn *putting;
    MDB_cursor *walking;
    unsigned put;
    unsigned batch;
};

static void said_to_the_parent(const int32_t answer)
{
    send(kFirstFd, &answer, sizeof answer, 0);
}

static int left_with(const int trouble)
{
    said_to_the_parent(trouble < 0 ? trouble : -trouble);
    return 1;
}

static void complain(const char *const what, const int status)
{
    fprintf(stderr, "webmachine-cache: %s: %s\n", what, mdb_strerror(status));
}

static bool opened(struct writing *const of_file, const char *const file, const size_t map_bytes,
                   const unsigned readers, const unsigned batch)
{
    const int made = mdb_env_create(&of_file->environment);
    if (made != 0)
        return complain("mdb_env_create", made), false;
    mdb_env_set_mapsize(of_file->environment, map_bytes);
    mdb_env_set_maxdbs(of_file->environment, 3);
    mdb_env_set_maxreaders(of_file->environment, readers);
    const int status = mdb_env_open(of_file->environment, file, MDB_NOSUBDIR, 0600);
    if (status != 0)
        return complain(file, status), false;
    of_file->batch = batch;
    return true;
}

static bool putting(struct writing *const of_file)
{
    if (of_file->putting != nullptr)
        return true;
    const int begun = mdb_txn_begin(of_file->environment, nullptr, 0, &of_file->putting);
    if (begun != 0)
        return complain("mdb_txn_begin", begun), false;
    const int opening = mdb_dbi_open(of_file->putting, "fields",
                                     MDB_INTEGERKEY | MDB_DUPSORT | MDB_CREATE, &of_file->fields);
    if (opening != 0)
        return complain("mdb_dbi_open fields", opening), false;
    const int too = mdb_dbi_open(of_file->putting, "bodies", MDB_INTEGERKEY | MDB_CREATE,
                                 &of_file->bodies);
    if (too != 0)
        return complain("mdb_dbi_open bodies", too), false;
    const int owed = mdb_dbi_open(of_file->putting, "due", MDB_INTEGERKEY | MDB_DUPSORT | MDB_CREATE,
                                  &of_file->due);
    if (owed != 0)
        return complain("mdb_dbi_open due", owed), false;
    const int walking = mdb_cursor_open(of_file->putting, of_file->fields, &of_file->walking);
    if (walking != 0)
        return complain("mdb_cursor_open", walking), false;
    return true;
}

static bool committed(struct writing *const of_file)
{
    if (of_file->putting == nullptr)
        return true;
    mdb_cursor_close(of_file->walking);
    of_file->walking = nullptr;
    const int status = mdb_txn_commit(of_file->putting);
    of_file->putting = nullptr;
    of_file->put = 0;
    if (status != 0)
        return complain("mdb_txn_commit", status), false;
    return true;
}

static uint64_t until_of(const cache_datagram_header header)
{
    struct timespec now = {};
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t) now.tv_sec + header.freshness_lifetime;
}

struct going {
    cache_gone_datagram datagram;
};

static void said_it_is_gone(struct writing *const of_file, const uint64_t route,
                            const uint8_t field, const uint8_t why)
{
    for (int at = 0; at < of_file->connections; at++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(of_file->ring);
        if (sqe == nullptr) {
            io_uring_submit(of_file->ring);
            sqe = io_uring_get_sqe(of_file->ring);
        }
        if (sqe == nullptr) {
            of_file->dropped++;
            continue;
        }
        struct going *const one = static_cast<struct going *>(calloc(1, sizeof *one));
        if (one == nullptr) {
            of_file->dropped++;
            continue;
        }
        one->datagram.route = route;
        one->datagram.field = field;
        one->datagram.why = why;
        io_uring_prep_send(sqe, at, &one->datagram, sizeof one->datagram, MSG_NOSIGNAL);
        sqe->flags |= IOSQE_FIXED_FILE;
        io_uring_sqe_set_data(sqe, one);
    }
    io_uring_submit(of_file->ring);
}

static void owed_at(struct writing *const of_file, const uint64_t until, const uint64_t route,
                    const uint8_t field)
{
    uint8_t room[sizeof route + 1];
    memcpy(room, &route, sizeof route);
    room[sizeof route] = field;
    MDB_val key = {sizeof until, const_cast<uint64_t *>(&until)};
    MDB_val data = {sizeof room, room};
    const int status = mdb_put(of_file->putting, of_file->due, &key, &data, 0);
    if (status != 0 && status != MDB_KEYEXIST)
        complain("mdb_put due", status);
}

static void stored_as_a_field(struct writing *const of_file, const cache_datagram_header header,
                              const uint8_t *const value, const size_t value_length)
{
    const uint64_t until = until_of(header);
    MDB_val key = {sizeof header.route, const_cast<uint64_t *>(&header.route)};
    uint8_t wanted = header.field;
    MDB_val standing = {sizeof wanted, &wanted};
    if (mdb_cursor_get(of_file->walking, &key, &standing, MDB_GET_BOTH_RANGE) == 0 &&
        standing.mv_size >= 1 && static_cast<const uint8_t *>(standing.mv_data)[0] == header.field)
        mdb_cursor_del(of_file->walking, 0);

    uint8_t room[1 + sizeof until + kCacheFieldMost];
    room[0] = header.field;
    memcpy(room + 1, &until, sizeof until);
    memcpy(room + 1 + sizeof until, value, value_length);
    MDB_val data = {1 + sizeof until + value_length, room};
    const int status = mdb_cursor_put(of_file->walking, &key, &data, 0);
    if (status != 0) {
        complain("mdb_cursor_put", status);
        return;
    }
    owed_at(of_file, until, header.route, header.field);
    if (++of_file->put >= of_file->batch)
        committed(of_file);
}

static void stored_as_a_body(struct writing *const of_file, const cache_datagram_header header,
                             const uint8_t *const value, const size_t value_length)
{
    const uint64_t until = until_of(header);
    MDB_val key = {sizeof header.route, const_cast<uint64_t *>(&header.route)};
    MDB_val data = {sizeof until + value_length, nullptr};
    const int status = mdb_put(of_file->putting, of_file->bodies, &key, &data, MDB_RESERVE);
    if (status != 0) {
        complain("mdb_put", status);
        return;
    }
    uint8_t *const room = static_cast<uint8_t *>(data.mv_data);
    memcpy(room, &until, sizeof until);
    memcpy(room + sizeof until, value, value_length);
    owed_at(of_file, until, header.route, kCacheFieldBody);
    if (++of_file->put >= of_file->batch)
        committed(of_file);
}

static void forgotten(struct writing *const of_file, const uint64_t route)
{
    if (!putting(of_file))
        return;
    MDB_val key = {sizeof route, const_cast<uint64_t *>(&route)};
    const int fields = mdb_del(of_file->putting, of_file->fields, &key, nullptr);
    if (fields != 0 && fields != MDB_NOTFOUND)
        complain("mdb_del fields", fields);
    MDB_val too = {sizeof route, const_cast<uint64_t *>(&route)};
    const int bodies = mdb_del(of_file->putting, of_file->bodies, &too, nullptr);
    if (bodies != 0 && bodies != MDB_NOTFOUND)
        complain("mdb_del bodies", bodies);
    said_it_is_gone(of_file, route, kCacheFieldCount, kCacheInvalidated);
    if (++of_file->put >= of_file->batch)
        committed(of_file);
}

static bool still_due(struct writing *const of_file, const uint64_t route, const uint8_t field,
                      const uint64_t now)
{
    MDB_val key = {sizeof route, const_cast<uint64_t *>(&route)};
    MDB_val found = {0, nullptr};
    uint64_t until = 0;
    if (field == kCacheFieldBody) {
        if (mdb_get(of_file->putting, of_file->bodies, &key, &found) != 0)
            return false;
        if (found.mv_size < sizeof until)
            return true;
        memcpy(&until, found.mv_data, sizeof until);
        return until <= now;
    }
    uint8_t wanted = field;
    MDB_val standing = {sizeof wanted, &wanted};
    MDB_cursor *reading = nullptr;
    if (mdb_cursor_open(of_file->putting, of_file->fields, &reading) != 0)
        return false;
    bool due = false;
    if (mdb_cursor_get(reading, &key, &standing, MDB_GET_BOTH_RANGE) == 0 &&
        standing.mv_size >= 1 + sizeof until &&
        static_cast<const uint8_t *>(standing.mv_data)[0] == field) {
        memcpy(&until, static_cast<const uint8_t *>(standing.mv_data) + 1, sizeof until);
        due = until <= now;
        if (due)
            mdb_cursor_del(reading, 0);
    }
    mdb_cursor_close(reading);
    return due;
}

static void swept(struct writing *const of_file, const uint64_t now)
{
    if (!putting(of_file))
        return;
    MDB_cursor *owed = nullptr;
    if (mdb_cursor_open(of_file->putting, of_file->due, &owed) != 0)
        return;
    MDB_val key = {0, nullptr};
    MDB_val data = {0, nullptr};
    int standing = mdb_cursor_get(owed, &key, &data, MDB_FIRST);
    while (standing == 0) {
        uint64_t until = 0;
        memcpy(&until, key.mv_data, sizeof until);
        if (until > now)
            break;
        uint64_t route = 0;
        uint8_t field = 0;
        memcpy(&route, data.mv_data, sizeof route);
        memcpy(&field, static_cast<const uint8_t *>(data.mv_data) + sizeof route, 1);
        if (field == kCacheFieldBody && still_due(of_file, route, field, now)) {
            MDB_val one = {sizeof route, &route};
            mdb_del(of_file->putting, of_file->bodies, &one, nullptr);
            said_it_is_gone(of_file, route, field, kCacheExpired);
        } else if (field != kCacheFieldBody && still_due(of_file, route, field, now)) {
            said_it_is_gone(of_file, route, field, kCacheExpired);
        }
        mdb_cursor_del(owed, 0);
        standing = mdb_cursor_get(owed, &key, &data, MDB_GET_CURRENT);
        if (standing != 0)
            standing = mdb_cursor_get(owed, &key, &data, MDB_FIRST);
    }
    mdb_cursor_close(owed);
    committed(of_file);
}

static void stored(struct writing *const of_file, const cache_datagram_header header,
                   const uint8_t *const value, const size_t value_length)
{
    if (!putting(of_file))
        return;
    if (header.field == kCacheFieldBody || value_length > kCacheFieldMost)
        stored_as_a_body(of_file, header, value, value_length);
    else
        stored_as_a_field(of_file, header, value, value_length);
}

static void took(struct writing *const of_file, const uint8_t *const datagram,
                 const size_t datagram_length, const int file)
{
    if (datagram_length < sizeof(cache_datagram_header)) {
        if (file >= 0)
            close(file);
        return;
    }
    cache_datagram_header header;
    memcpy(&header, datagram, sizeof header);
    if (header.field >= kCacheFieldCount) {
        if (file >= 0)
            close(file);
        return;
    }

    if (header.forget == kCacheForgets) {
        if (file >= 0)
            close(file);
        forgotten(of_file, header.route);
        return;
    }

    if (header.body == kCacheBodyIsInline) {
        if (file >= 0)
            close(file);
        stored(of_file, header, datagram + sizeof header, datagram_length - sizeof header);
        return;
    }

    if (file < 0)
        return;
    const off_t length = lseek(file, 0, SEEK_END);
    if (length <= 0) {
        close(file);
        return;
    }
    void *const mapping = mmap(nullptr, (size_t) length, PROT_READ, MAP_SHARED, file, 0);
    close(file);
    if (mapping == MAP_FAILED) {
        perror("webmachine-cache: mmap");
        return;
    }
    stored(of_file, header, static_cast<const uint8_t *>(mapping), (size_t) length);
    munmap(mapping, (size_t) length);
}

static int file_beside(struct io_uring_recvmsg_out *const said, struct msghdr *const shape)
{
    for (struct cmsghdr *one = io_uring_recvmsg_cmsg_firsthdr(said, shape); one != nullptr;
         one = io_uring_recvmsg_cmsg_nexthdr(said, shape, one)) {
        if (one->cmsg_level == SOL_SOCKET && one->cmsg_type == SCM_RIGHTS) {
            int found = -1;
            memcpy(&found, CMSG_DATA(one), sizeof found);
            return found;
        }
    }
    return -1;
}

static uint64_t seconds_now()
{
    struct timespec now = {};
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t) now.tv_sec;
}

static void armed(struct io_uring *const ring, const int which, struct msghdr *const shape)
{
    struct io_uring_sqe *const sqe = io_uring_get_sqe(ring);
    io_uring_prep_recvmsg_multishot(sqe, which, shape, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE;
    sqe->buf_group = kBufferGroup;
    io_uring_sqe_set_data64(sqe, (uint64_t) which);
}

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) {
        fprintf(stderr, "usage: webmachine-cache <file> <connections> <map bytes> <readers> "
                        "<batch> <buffer bytes> [engine]\n");
        return 2;
    }
    const char *const file = argv[1];
    const int connections = atoi(argv[2]);
    const size_t map_bytes = strtoull(argv[3], nullptr, 10);
    const unsigned readers = (unsigned) strtoul(argv[4], nullptr, 10);
    const unsigned batch = (unsigned) strtoul(argv[5], nullptr, 10);
    const size_t buffer_budget = strtoull(argv[6], nullptr, 10);
    if (argc == 8 && strcmp(argv[7], "engine") == 0)
        slipstream_syscall_set_engine(1);
    if (connections <= 0 || map_bytes == 0 || readers == 0 || batch == 0 || buffer_budget == 0) {
        fprintf(stderr, "webmachine-cache: every argument counts, and none may be zero\n");
        return 2;
    }

    struct writing of_file = {};
    of_file.connections = connections;
    if (!opened(&of_file, file, map_bytes, readers, batch))
        return left_with(EIO);

    int given = 0;
    socklen_t asked = sizeof given;
    getsockopt(kFirstFd, SOL_SOCKET, SO_RCVBUF, &given, &asked);
    const size_t buffer_bytes =
        (given > 0 ? (size_t) given : (size_t) 1 << 18) + sizeof(struct io_uring_recvmsg_out) +
        CMSG_SPACE(sizeof(int)) + 64;

    const unsigned buffers_wanted = (unsigned) (buffer_budget / buffer_bytes);
    if (buffers_wanted < 2) {
        fprintf(stderr, "webmachine-cache: %zu bytes of buffer holds fewer than two of %zu\n",
                buffer_budget, buffer_bytes);
        return left_with(ERANGE);
    }
    const unsigned buffer_count = no_more_than_a_power_of_two(buffers_wanted, kMostRingEntries);
    const unsigned buffer_mask = buffer_count - 1;

    struct io_uring ring;
    const int begun = io_uring_queue_init(buffer_count, &ring, 0);
    if (begun < 0) {
        fprintf(stderr, "webmachine-cache: io_uring_queue_init: %s\n", strerror(-begun));
        return left_with(begun);
    }
    of_file.ring = &ring;
    int *const theirs = static_cast<int *>(malloc(sizeof(int) * (size_t) connections));
    if (theirs == nullptr)
        return left_with(ENOMEM);
    for (int at = 0; at < connections; at++)
        theirs[at] = kFirstFd + at;
    const int registered = io_uring_register_files(&ring, theirs, (unsigned) connections);
    free(theirs);
    if (registered < 0) {
        fprintf(stderr, "webmachine-cache: io_uring_register_files: %s\n", strerror(-registered));
        return left_with(registered);
    }
    fprintf(stderr, "webmachine-cache: %u buffers of %zu bytes, %u completions, io through %s\n",
            buffer_count, buffer_bytes, buffer_count * 2,
            slipstream_syscall_uses_engine() ? "the engine" : "the kernel");

    int trouble = 0;
    struct io_uring_buf_ring *const buffers =
        io_uring_setup_buf_ring(&ring, buffer_count, kBufferGroup, 0, &trouble);
    if (buffers == nullptr) {
        fprintf(stderr, "webmachine-cache: io_uring_setup_buf_ring: %s\n", strerror(-trouble));
        return left_with(trouble);
    }
    uint8_t *const room = static_cast<uint8_t *>(malloc(buffer_bytes * buffer_count));
    if (room == nullptr)
        return left_with(ENOMEM);
    for (unsigned at = 0; at < buffer_count; at++)
        io_uring_buf_ring_add(buffers, room + at * buffer_bytes, (unsigned) buffer_bytes, at,
                              (int) buffer_mask, (int) at);
    io_uring_buf_ring_advance(buffers, buffer_count);

    struct msghdr shape = {};
    shape.msg_namelen = 0;
    shape.msg_controllen = CMSG_SPACE(sizeof(int));
    for (int at = 0; at < connections; at++)
        armed(&ring, at, &shape);
    io_uring_submit(&ring);
    said_to_the_parent((int32_t) given);

    int open_connections = connections;
    while (open_connections > 0) {
        struct io_uring_cqe *cqe = nullptr;
        struct __kernel_timespec a_second = {1, 0};
        const int waited = io_uring_submit_and_wait_timeout(&ring, &cqe, 1, &a_second, nullptr);
        if (waited < 0 && waited != -ETIME) {
            if (waited == -EINTR)
                continue;
            fprintf(stderr, "webmachine-cache: io_uring_submit_and_wait_timeout: %s\n",
                    strerror(-waited));
            break;
        }
        if (cqe == nullptr) {
            swept(&of_file, seconds_now());
            continue;
        }
        if (io_uring_cqe_get_data64(cqe) >= (uint64_t) connections) {
            struct going *const one = static_cast<struct going *>(io_uring_cqe_get_data(cqe));
            if (cqe->res != (int) sizeof one->datagram)
                of_file.dropped++;
            free(one);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }
        const int connection = (int) io_uring_cqe_get_data64(cqe);
        const bool more = (cqe->flags & IORING_CQE_F_MORE) != 0;
        if ((cqe->flags & IORING_CQE_F_BUFFER) == 0) {
            if (cqe->res == -ENOBUFS || !more)
                armed(&ring, connection, &shape), io_uring_submit(&ring);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }
        const unsigned which = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
        uint8_t *const one = room + which * buffer_bytes;
        bool ended_here = false;
        if (cqe->res > 0) {
            struct io_uring_recvmsg_out *const said =
                io_uring_recvmsg_validate(one, cqe->res, &shape);
            if (said == nullptr) {
                ended_here = true;
            } else {
                const unsigned length =
                    io_uring_recvmsg_payload_length(said, cqe->res, &shape);
                if (length == 0) {
                    ended_here = true;
                } else {
                    const void *const payload = io_uring_recvmsg_payload(said, &shape);
                    took(&of_file, static_cast<const uint8_t *>(payload), length,
                         file_beside(said, &shape));
                }
            }
        } else {
            ended_here = cqe->res != -ENOBUFS;
        }
        io_uring_buf_ring_add(buffers, one, (unsigned) buffer_bytes, which,
                              (int) buffer_mask, 0);
        io_uring_buf_ring_advance(buffers, 1);
        if (ended_here) {
            close(kFirstFd + connection);
            open_connections--;
        } else if (!more) {
            armed(&ring, connection, &shape);
            io_uring_submit(&ring);
        }
        io_uring_cqe_seen(&ring, cqe);
    }

    if (of_file.dropped != 0)
        fprintf(stderr, "webmachine-cache: %lu gone datagrams were dropped\n", of_file.dropped);
    const bool ended = committed(&of_file);
    mdb_env_close(of_file.environment);
    io_uring_free_buf_ring(&ring, buffers, buffer_count, kBufferGroup);
    io_uring_queue_exit(&ring);
    free(room);
    return ended ? 0 : 1;
}
