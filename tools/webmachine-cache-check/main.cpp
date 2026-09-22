#include <assert.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/cache.h"
#include "../../src/cache_datagram.h"

extern char **environ;

enum { kThreads = 3, kFirstFd = 3 };

static int mine[kThreads];
static size_t inline_limit = 0;

static void hand_over_inline(const int thread, const char *const declared,
                             const uint8_t *const body, const uint32_t body_length)
{
    cache_datagram_header header = {};
    header.key = cache_key_of(reinterpret_cast<const uint8_t *>(declared), strlen(declared));
    header.key_length = (uint32_t) strlen(declared);
    header.freshness_lifetime = 900;
    header.body = kCacheBodyIsInline;

    const size_t length = sizeof header + sizeof body_length + header.key_length + body_length;
    uint8_t *const room = static_cast<uint8_t *>(malloc(length));
    size_t at = 0;
    memcpy(room + at, &header, sizeof header);
    at += sizeof header;
    memcpy(room + at, &body_length, sizeof body_length);
    at += sizeof body_length;
    memcpy(room + at, declared, header.key_length);
    at += header.key_length;
    memcpy(room + at, body, body_length);
    assert(send(mine[thread], room, length, 0) == (ssize_t) length);
    free(room);
}

static void hand_over_in_a_file(const int thread, const char *const declared,
                                const uint8_t *const body, const uint32_t body_length)
{
    cache_datagram_header header = {};
    header.key = cache_key_of(reinterpret_cast<const uint8_t *>(declared), strlen(declared));
    header.key_length = (uint32_t) strlen(declared);
    header.freshness_lifetime = 900;
    header.body = kCacheBodyIsInAFile;

    const size_t length = header.key_length + body_length;
    const int file = memfd_create("cache-entry", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    assert(file >= 0);
    assert(ftruncate(file, (off_t) length) == 0);
    uint8_t *const mapping =
        static_cast<uint8_t *>(mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0));
    assert(mapping != MAP_FAILED);
    memcpy(mapping, declared, header.key_length);
    memcpy(mapping + header.key_length, body, body_length);
    assert(munmap(mapping, length) == 0);
    assert(fcntl(file, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) == 0);

    struct iovec one = {&header, sizeof header};
    union {
        char room[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } beside = {};
    struct msghdr carrying = {};
    carrying.msg_iov = &one;
    carrying.msg_iovlen = 1;
    carrying.msg_control = beside.room;
    carrying.msg_controllen = sizeof beside.room;
    struct cmsghdr *const rights = CMSG_FIRSTHDR(&carrying);
    rights->cmsg_level = SOL_SOCKET;
    rights->cmsg_type = SCM_RIGHTS;
    rights->cmsg_len = CMSG_LEN(sizeof file);
    memcpy(CMSG_DATA(rights), &file, sizeof file);
    assert(sendmsg(mine[thread], &carrying, 0) == (ssize_t) sizeof header);
    close(file);
}

int main(int argc, char **argv)
{
    const char *const file = "/tmp/wm-whole.mdb";
    remove(file);
    remove("/tmp/wm-whole.mdb-lock");

    int theirs[kThreads];
    for (int at = 0; at < kThreads; at++) {
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        mine[at] = pair[0];
        theirs[at] = fcntl(pair[1], F_DUPFD_CLOEXEC, kFirstFd + kThreads);
        assert(theirs[at] >= 0);
        close(pair[1]);
    }

    int given = 0;
    socklen_t asked = sizeof given;
    assert(getsockopt(mine[0], SOL_SOCKET, SO_SNDBUF, &given, &asked) == 0);
    inline_limit = (size_t) given - sizeof(cache_datagram_header) - sizeof(uint32_t) - 256;
    printf("the socket gave %d bytes, so %zu of body goes inline\n", given, inline_limit);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int at = 0; at < kThreads; at++)
        posix_spawn_file_actions_adddup2(&actions, theirs[at], kFirstFd + at);
    posix_spawn_file_actions_addclosefrom_np(&actions, kFirstFd + kThreads);

    char threads[8], map[32], readers[8], batch[8];
    snprintf(threads, sizeof threads, "%d", kThreads);
    snprintf(map, sizeof map, "%llu", (unsigned long long) (1024ull << 20));
    snprintf(readers, sizeof readers, "%d", 64);
    snprintf(batch, sizeof batch, "%d", 2);
    char *const writer = argc > 1 ? argv[1] : const_cast<char *>("./webmachine-cache");
    char *child[] = {writer, const_cast<char *>(file), threads, map, readers, batch, nullptr};
    pid_t spawned = 0;
    assert(posix_spawn(&spawned, writer, &actions, nullptr, child, environ) == 0);
    posix_spawn_file_actions_destroy(&actions);
    for (int at = 0; at < kThreads; at++)
        close(theirs[at]);
    printf("the server spawned the writer and kept one socket per thread\n");

    static uint8_t small[64];
    memset(small, 'a', sizeof small);
    static uint8_t large[3u << 20];
    for (size_t at = 0; at < sizeof large; at++)
        large[at] = (uint8_t) ('0' + at % 10);

    const char *const one = "GET /articles/42?param=xyz&foo=bar";
    const char *const two = "GET /articles/7?param=abc&foo=bar";
    assert(sizeof small <= inline_limit);
    assert(sizeof large > inline_limit);
    hand_over_inline(0, one, small, sizeof small);
    hand_over_in_a_file(1, two, large, sizeof large);
    printf("one entry inline, one in a sealed memfd of %zu bytes\n", sizeof large);

    for (int at = 0; at < kThreads; at++)
        close(mine[at]);
    int left = 0;
    waitpid(spawned, &left, 0);
    assert(WEXITSTATUS(left) == 0);
    printf("the writer drained its sockets and left with 0\n");

    cache *const c = cache_open("wm-whole", "/tmp", 64);
    assert(c != nullptr);
    cache_reader *const r = cache_reader_opened(c);
    assert(r != nullptr);

    cache_answer a =
        cache_asked(r, cache_key_of(reinterpret_cast<const uint8_t *>(one), strlen(one)));
    assert(a.value != nullptr);
    assert(a.length == strlen(one) + sizeof small);
    assert(memcmp(a.value, one, strlen(one)) == 0);
    assert(memcmp(a.value + strlen(one), small, sizeof small) == 0);
    printf("the reader finds the inline entry, key first, then the answer\n");

    cache_answer b =
        cache_asked(r, cache_key_of(reinterpret_cast<const uint8_t *>(two), strlen(two)));
    assert(b.value != nullptr);
    assert(b.length == strlen(two) + sizeof large);
    assert(memcmp(b.value, two, strlen(two)) == 0);
    assert(memcmp(b.value + strlen(two), large, sizeof large) == 0);
    printf("the entry that came as a descriptor is whole, %zu bytes\n", b.length);

    const char *const missing = "GET /articles/42?utm_source=mail";
    assert(cache_asked(r, cache_key_of(reinterpret_cast<const uint8_t *>(missing),
                                       strlen(missing)))
               .value == nullptr);
    printf("a key the dev did not declare is a miss\n");

    cache_sent(r, a.snapshot);
    cache_sent(r, b.snapshot);
    cache_reader_closed(r);
    cache_close(c);
    printf("ok\n");
    return 0;
}
