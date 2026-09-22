#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lmdb.h>

#include "../../src/cache_datagram.h"
#include "../../src/cache_gathering.h"

enum { kFirstFd = 3 };

struct writing {
    MDB_env *environment;
    MDB_dbi database;
    MDB_txn *putting;
    unsigned gathered;
    unsigned gathering_now;
    unsigned batch;
};

static void complain(const char *what, const int status)
{
    fprintf(stderr, "webmachine-cache: %s: %s\n", what, mdb_strerror(status));
}

static bool opened(struct writing *const of_file, const char *const file, const size_t map_bytes,
                   const unsigned readers, const unsigned batch)
{
    int status = mdb_env_create(&of_file->environment);
    if (status != 0)
        return complain("mdb_env_create", status), false;
    mdb_env_set_mapsize(of_file->environment, map_bytes);
    mdb_env_set_maxreaders(of_file->environment, readers);
    status = mdb_env_open(of_file->environment, file, MDB_NOSUBDIR, 0600);
    if (status != 0)
        return complain(file, status), false;
    of_file->batch = batch;
    return true;
}

static bool putting(struct writing *const of_file)
{
    if (of_file->putting != NULL)
        return true;
    const int status = mdb_txn_begin(of_file->environment, NULL, 0, &of_file->putting);
    if (status != 0)
        return complain("mdb_txn_begin", status), false;
    const int opening = mdb_dbi_open(of_file->putting, NULL, MDB_INTEGERKEY, &of_file->database);
    if (opening != 0)
        return complain("mdb_dbi_open", opening), false;
    return true;
}

static bool committed(struct writing *const of_file)
{
    if (of_file->putting == NULL)
        return true;
    const int status = mdb_txn_commit(of_file->putting);
    of_file->putting = NULL;
    of_file->gathered = 0;
    if (status != 0)
        return complain("mdb_txn_commit", status), false;
    return true;
}

static void forgotten(struct writing *const of_file, cache_gathering *const of_connection)
{
    if (of_connection->into != NULL) {
        if (of_file->gathering_now > 0)
            of_file->gathering_now--;
    }
    if (of_connection->into != NULL && of_file->putting != NULL) {
        MDB_val key = {sizeof of_connection->key, &of_connection->key};
        mdb_del(of_file->putting, of_file->database, &key, NULL);
    }
    cache_gathering_dropped(of_connection);
}

static void took(struct writing *const of_file, cache_gathering *const of_connection,
                 const uint8_t *const datagram, const size_t datagram_length)
{
    if (datagram_length < sizeof(cache_datagram_header)) {
        forgotten(of_file, of_connection);
        return;
    }
    cache_datagram_header header;
    memcpy(&header, datagram, sizeof header);
    const uint8_t *const payload = datagram + sizeof header;
    const size_t payload_length = datagram_length - sizeof header;

    if (header.at == 0) {
        forgotten(of_file, of_connection);
        const uint32_t room = cache_gathering_reserves(header, payload_length);
        if (room == 0 || !putting(of_file))
            return;
        MDB_val key = {sizeof header.key, &header.key};
        MDB_val value = {room, NULL};
        const int status =
            mdb_put(of_file->putting, of_file->database, &key, &value, MDB_RESERVE);
        if (status != 0) {
            complain("mdb_put", status);
            return;
        }
        if (!cache_gathering_began(of_connection, header, payload, payload_length, static_cast<uint8_t *>(value.mv_data)))
            forgotten(of_file, of_connection);
        else
            of_file->gathering_now++;
    } else if (!cache_gathering_took(of_connection, header, payload, payload_length)) {
        forgotten(of_file, of_connection);
        return;
    }

    if (cache_gathering_whole(of_connection)) {
        cache_gathering_dropped(of_connection);
        of_file->gathering_now--;
        if (++of_file->gathered >= of_file->batch && of_file->gathering_now == 0)
            committed(of_file);
    }
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
    const size_t map_bytes = strtoull(argv[3], NULL, 10);
    const unsigned readers = (unsigned) strtoul(argv[4], NULL, 10);
    const unsigned batch = (unsigned) strtoul(argv[5], NULL, 10);
    if (connections <= 0 || map_bytes == 0 || readers == 0 || batch == 0) {
        fprintf(stderr, "webmachine-cache: every argument counts, and none may be zero\n");
        return 2;
    }

    struct writing of_file = {0};
    if (!opened(&of_file, file, map_bytes, readers, batch))
        return 1;

    struct pollfd *const watching =
        static_cast<struct pollfd *>(calloc((size_t) connections, sizeof *watching));
    cache_gathering *const gathering =
        static_cast<cache_gathering *>(calloc((size_t) connections, sizeof *gathering));
    const size_t room_bytes = map_bytes < (1u << 20) ? map_bytes : (1u << 20);
    uint8_t *const room = static_cast<uint8_t *>(malloc(room_bytes));
    if (watching == NULL || gathering == NULL || room == NULL)
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
            const ssize_t got = recv(watching[at].fd, room, room_bytes, 0);
            if (got > 0) {
                took(&of_file, &gathering[at], room, (size_t) got);
                continue;
            }
            if (got < 0 && errno == EINTR)
                continue;
            forgotten(&of_file, &gathering[at]);
            close(watching[at].fd);
            watching[at].fd = -1;
            open_connections--;
        }
    }

    const bool ended = committed(&of_file);
    mdb_env_close(of_file.environment);
    free(watching);
    free(gathering);
    free(room);
    return ended ? 0 : 1;
}
