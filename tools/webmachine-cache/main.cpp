#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <liburing.h>
#include <lmdb.h>
#include <slipstream_syscall.h>

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
    MDB_dbi database;
    MDB_txn *putting;
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
    const int opening =
        mdb_dbi_open(of_file->putting, nullptr, MDB_INTEGERKEY, &of_file->database);
    if (opening != 0)
        return complain("mdb_dbi_open", opening), false;
    return true;
}

static bool committed(struct writing *const of_file)
{
    if (of_file->putting == nullptr)
        return true;
    const int status = mdb_txn_commit(of_file->putting);
    of_file->putting = nullptr;
    of_file->put = 0;
    if (status != 0)
        return complain("mdb_txn_commit", status), false;
    return true;
}

static void stored(struct writing *const of_file, const cache_datagram_header header,
                   const uint8_t *const value, const size_t value_length)
{
    if (!putting(of_file))
        return;
    MDB_val key = {sizeof header.key, const_cast<uint64_t *>(&header.key)};
    MDB_val data = {value_length, const_cast<uint8_t *>(value)};
    const int status = mdb_put(of_file->putting, of_file->database, &key, &data, 0);
    if (status != 0) {
        complain("mdb_put", status);
        return;
    }
    if (++of_file->put >= of_file->batch)
        committed(of_file);
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
    const uint8_t *const after = datagram + sizeof header;
    const size_t after_length = datagram_length - sizeof header;

    if (header.body == kCacheBodyIsInline) {
        if (file >= 0)
            close(file);
        if (after_length < sizeof(uint32_t))
            return;
        uint32_t body_length = 0;
        memcpy(&body_length, after, sizeof body_length);
        if (after_length != sizeof body_length + header.key_length + body_length)
            return;
        stored(of_file, header, after + sizeof body_length,
               (size_t) header.key_length + body_length);
        return;
    }

    if (file < 0)
        return;
    const off_t length = lseek(file, 0, SEEK_END);
    if (length <= 0 || (size_t) length < header.key_length) {
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

static void armed(struct io_uring *const ring, const int fd, struct msghdr *const shape)
{
    struct io_uring_sqe *const sqe = io_uring_get_sqe(ring);
    io_uring_prep_recvmsg_multishot(sqe, fd, shape, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = kBufferGroup;
    io_uring_sqe_set_data64(sqe, (uint64_t) fd);
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
        armed(&ring, kFirstFd + at, &shape);
    io_uring_submit(&ring);
    said_to_the_parent((int32_t) given);

    int open_connections = connections;
    while (open_connections > 0) {
        struct io_uring_cqe *cqe = nullptr;
        const int waited = io_uring_wait_cqe(&ring, &cqe);
        if (waited < 0) {
            if (waited == -EINTR)
                continue;
            fprintf(stderr, "webmachine-cache: io_uring_wait_cqe: %s\n", strerror(-waited));
            break;
        }
        const int fd = (int) io_uring_cqe_get_data64(cqe);
        const bool more = (cqe->flags & IORING_CQE_F_MORE) != 0;
        if ((cqe->flags & IORING_CQE_F_BUFFER) == 0) {
            if (cqe->res == -ENOBUFS || !more)
                armed(&ring, fd, &shape), io_uring_submit(&ring);
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
            close(fd);
            open_connections--;
        } else if (!more) {
            armed(&ring, fd, &shape);
            io_uring_submit(&ring);
        }
        io_uring_cqe_seen(&ring, cqe);
    }

    const bool ended = committed(&of_file);
    mdb_env_close(of_file.environment);
    io_uring_free_buf_ring(&ring, buffers, buffer_count, kBufferGroup);
    io_uring_queue_exit(&ring);
    free(room);
    return ended ? 0 : 1;
}
