#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lmdb.h>

#include "../../src/cache_datagram.h"

enum { kFirstFd = 3 };

struct writing {
    MDB_env *environment;
    MDB_dbi database;
    MDB_txn *putting;
    unsigned put;
    unsigned batch;
};

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
    const int opening = mdb_dbi_open(of_file->putting, nullptr, MDB_INTEGERKEY,
                                     &of_file->database);
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

static int file_beside(struct msghdr *const carrying)
{
    for (struct cmsghdr *one = CMSG_FIRSTHDR(carrying); one != nullptr;
         one = CMSG_NXTHDR(carrying, one)) {
        if (one->cmsg_level == SOL_SOCKET && one->cmsg_type == SCM_RIGHTS) {
            int found = -1;
            memcpy(&found, CMSG_DATA(one), sizeof found);
            return found;
        }
    }
    return -1;
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

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr,
                "usage: webmachine-cache <file> <connections> <map bytes> <readers> <batch>\n");
        return 2;
    }
    const char *const file = argv[1];
    const int connections = atoi(argv[2]);
    const size_t map_bytes = strtoull(argv[3], nullptr, 10);
    const unsigned readers = (unsigned) strtoul(argv[4], nullptr, 10);
    const unsigned batch = (unsigned) strtoul(argv[5], nullptr, 10);
    if (connections <= 0 || map_bytes == 0 || readers == 0 || batch == 0) {
        fprintf(stderr, "webmachine-cache: every argument counts, and none may be zero\n");
        return 2;
    }

    struct writing of_file = {};
    if (!opened(&of_file, file, map_bytes, readers, batch))
        return 1;

    struct pollfd *const watching =
        static_cast<struct pollfd *>(calloc((size_t) connections, sizeof *watching));
    int largest = 0;
    socklen_t asked = sizeof largest;
    getsockopt(kFirstFd, SOL_SOCKET, SO_RCVBUF, &largest, &asked);
    const size_t room_bytes = largest > 0 ? (size_t) largest : (size_t) 1 << 18;
    uint8_t *const room = static_cast<uint8_t *>(malloc(room_bytes));
    if (watching == nullptr || room == nullptr)
        return 1;
    for (int at = 0; at < connections; at++) {
        watching[at].fd = kFirstFd + at;
        watching[at].events = POLLIN;
    }

    int open_connections = connections;
    while (open_connections > 0) {
        if (poll(watching, (nfds_t) connections, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("webmachine-cache: poll");
            break;
        }
        for (int at = 0; at < connections; at++) {
            if (watching[at].fd < 0 || (watching[at].revents & (POLLIN | POLLHUP)) == 0)
                continue;
            struct iovec one = {room, room_bytes};
            union {
                char room[CMSG_SPACE(sizeof(int))];
                struct cmsghdr align;
            } beside = {};
            struct msghdr carrying = {};
            carrying.msg_iov = &one;
            carrying.msg_iovlen = 1;
            carrying.msg_control = beside.room;
            carrying.msg_controllen = sizeof beside.room;
            const ssize_t got = recvmsg(watching[at].fd, &carrying, 0);
            if (got > 0) {
                took(&of_file, room, (size_t) got, file_beside(&carrying));
                continue;
            }
            if (got < 0 && errno == EINTR)
                continue;
            close(watching[at].fd);
            watching[at].fd = -1;
            open_connections--;
        }
    }

    const bool ended = committed(&of_file);
    mdb_env_close(of_file.environment);
    free(watching);
    free(room);
    return ended ? 0 : 1;
}
