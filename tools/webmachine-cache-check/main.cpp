#include <assert.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../src/cache.h"
#include "../../src/cache_datagram.h"
#include "../../src/cache_forget.h"

extern char **environ;

enum { kThreads = 3, kFirstFd = 3 };

static int mine[kThreads];
static size_t inline_limit = 0;

static void hand_over_inline(const int thread, const uint64_t of_route, const uint8_t field,
                             const uint32_t freshness_lifetime, const uint8_t *const value,
                             const size_t value_length)
{
    cache_datagram_header header = {};
    header.route = of_route;
    header.field = field;
    header.freshness_lifetime = freshness_lifetime;
    header.body = kCacheBodyIsInline;

    const size_t length = sizeof header + value_length;
    uint8_t *const room = static_cast<uint8_t *>(malloc(length));
    memcpy(room, &header, sizeof header);
    memcpy(room + sizeof header, value, value_length);
    assert(send(mine[thread], room, length, 0) == (ssize_t) length);
    free(room);
}

static void hand_over_in_a_file(const int thread, const uint64_t of_route, const uint8_t field,
                                const uint32_t freshness_lifetime, const uint8_t *const value,
                                const size_t value_length)
{
    cache_datagram_header header = {};
    header.route = of_route;
    header.field = field;
    header.freshness_lifetime = freshness_lifetime;
    header.body = kCacheBodyIsInAFile;

    const int file = memfd_create("cache-entry", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    assert(file >= 0);
    assert(ftruncate(file, (off_t) value_length) == 0);
    uint8_t *const mapping = static_cast<uint8_t *>(
        mmap(nullptr, value_length, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0));
    assert(mapping != MAP_FAILED);
    memcpy(mapping, value, value_length);
    assert(munmap(mapping, value_length) == 0);
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

static cache_forgetting *through_the_writer = nullptr;

static uint64_t now_is()
{
    struct timespec now = {};
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t) now.tv_sec;
}

static const char *const kFile = "/tmp/wm-whole.mdb";

static pid_t the_writer_stands(char *const writer, char *const arm, int32_t *const taken)
{
    int theirs[kThreads];
    for (int at = 0; at < kThreads; at++) {
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        mine[at] = pair[0];
        theirs[at] = fcntl(pair[1], F_DUPFD_CLOEXEC, kFirstFd + kThreads);
        assert(theirs[at] >= 0);
        close(pair[1]);
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int at = 0; at < kThreads; at++)
        posix_spawn_file_actions_adddup2(&actions, theirs[at], kFirstFd + at);
    posix_spawn_file_actions_addclosefrom_np(&actions, kFirstFd + kThreads);

    static char threads[8], map[32], readers[8], batch[8], budget[32];
    snprintf(threads, sizeof threads, "%d", kThreads);
    snprintf(map, sizeof map, "%llu", (unsigned long long) (1024ull << 20));
    snprintf(readers, sizeof readers, "%d", 64);
    snprintf(batch, sizeof batch, "%d", 2);
    snprintf(budget, sizeof budget, "%llu", (unsigned long long) (512ull << 20));
    char *child[] = {writer, const_cast<char *>(kFile), threads, map, readers, batch, budget,
                     arm, nullptr};
    pid_t spawned = 0;
    assert(posix_spawn(&spawned, writer, &actions, nullptr, child, environ) == 0);
    posix_spawn_file_actions_destroy(&actions);
    for (int at = 0; at < kThreads; at++)
        close(theirs[at]);

    int32_t standing = 0;
    assert(recv(mine[0], &standing, sizeof standing, 0) == (ssize_t) sizeof standing);
    *taken = standing;
    cache_forgetting_closed(through_the_writer);
    through_the_writer = cache_forgetting_through(mine[0]);
    assert(through_the_writer != nullptr);
    return spawned;
}

static void the_writer_left(const pid_t spawned)
{
    for (int at = 0; at < kThreads; at++)
        close(mine[at]);
    int left = 0;
    waitpid(spawned, &left, 0);
    if (!WIFEXITED(left)) {
        fprintf(stderr, "the writer died of signal %d\n", WTERMSIG(left));
        exit(1);
    }
    assert(WEXITSTATUS(left) == 0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    remove(kFile);
    remove("/tmp/wm-whole.mdb-lock");

    char *const writer = argc > 1 ? argv[1] : const_cast<char *>("./webmachine-cache");
    char *const arm = argc > 2 ? argv[2] : nullptr;
    int32_t standing = 0;
    pid_t spawned = the_writer_stands(writer, arm, &standing);
    printf("the server spawned the writer and kept one socket per thread\n");
    if (standing < 0) {
        fprintf(stderr, "the writer could not start: %s\n", strerror(-standing));
        return 1;
    }
    inline_limit = (size_t) standing - sizeof(cache_datagram_header) - 256;
    printf("the writer stands and takes %d bytes, so %zu of body goes inline\n", standing,
           inline_limit);

    static uint8_t tag[32];
    memset(tag, 'a', sizeof tag);
    static uint8_t large[3u << 20];
    for (size_t at = 0; at < sizeof large; at++)
        large[at] = (uint8_t) ('0' + at % 10);

    const char *const declared = "/articles/42?param=xyz&foo=bar";
    const uint64_t one =
        cache_key_of(reinterpret_cast<const uint8_t *>(declared), strlen(declared));
    assert(sizeof tag <= inline_limit);
    assert(sizeof large > inline_limit);

    hand_over_inline(0, one, kCacheFieldEntityTag, 900, tag, sizeof tag);
    hand_over_inline(0, one, kCacheFieldStatus, 900,
                     reinterpret_cast<const uint8_t *>("200"), 3);
    hand_over_inline(1, one, kCacheFieldContentType, 0,
                     reinterpret_cast<const uint8_t *>("text/html"), 9);
    hand_over_inline(1, one, kCacheFieldVary, 900,
                     reinterpret_cast<const uint8_t *>("accept"), 6);
    hand_over_in_a_file(2, one, kCacheFieldBody, 900, large, sizeof large);
    assert(cache_forget_value(through_the_writer, one, kCacheFieldVary));

    const char *const other = "/articles/7?param=abc&foo=bar";
    const uint64_t two =
        cache_key_of(reinterpret_cast<const uint8_t *>(other), strlen(other));
    hand_over_inline(0, two, kCacheFieldEntityTag, 900, tag, sizeof tag);
    hand_over_in_a_file(0, two, kCacheFieldBody, 900, large, sizeof large);
    assert(cache_forget_route(through_the_writer, two));
    printf("one route, three fields inline and a body of %zu bytes in a sealed memfd,\n"
           "and a second route stored whole and then forgotten\n",
           sizeof large);

    struct timespec long_enough = {2, 0};
    nanosleep(&long_enough, nullptr);
    printf("the writer's sweep ran at least once while the sockets stood open\n");

    bool heard_the_invalidation = false;
    bool heard_an_expiry = false;
    for (int at = 0; at < kThreads; at++) {
        cache_gone_datagram gone = {};
        while (recv(mine[at], &gone, sizeof gone, MSG_DONTWAIT) == (ssize_t) sizeof gone) {
            if (gone.route == two && gone.field == kCacheFieldCount &&
                gone.why == kCacheInvalidated)
                heard_the_invalidation = true;
            if (gone.route == one && gone.field == kCacheFieldContentType &&
                gone.why == kCacheExpired)
                heard_an_expiry = true;
        }
    }
    assert(heard_the_invalidation);
    assert(heard_an_expiry);
    printf("every thread heard what went away, both the forgetting and the expiry\n");

    the_writer_left(spawned);
    printf("the writer drained its sockets and left with 0\n");

    cache *const c = cache_open("wm-whole", "/tmp", 64);
    assert(c != nullptr);
    cache_reader *const r = cache_reader_opened(c);
    assert(r != nullptr);
    const uint64_t now = now_is();

    cache_answer a = cache_asked(r, one, kCacheFieldEntityTag, now);
    assert(a.value != nullptr);
    assert(a.length == sizeof tag);
    assert(memcmp(a.value, tag, sizeof tag) == 0);
    printf("the reader finds a field out of the index\n");

    cache_answer s = cache_asked(r, one, kCacheFieldStatus, now);
    assert(s.value != nullptr && s.length == 3 && memcmp(s.value, "200", 3) == 0);
    printf("and its neighbour on the same route, without descending again\n");

    assert(cache_asked(r, one, kCacheFieldContentType, now).value == nullptr);
    printf("a field whose lifetime has run out is a miss, not its neighbour\n");

    assert(cache_asked(r, one, kCacheFieldLastModified, now).value == nullptr);
    printf("a field that was never stored is a miss, not the next one along\n");

    assert(cache_asked(r, one, kCacheFieldVary, now).value == nullptr);
    printf("one value can be dropped on its own, and only it\n");

    cache_answer b = cache_body_asked(r, one, now);
    assert(b.value != nullptr);
    assert(b.length == sizeof large);
    assert(memcmp(b.value, large, sizeof large) == 0);
    printf("the body that came as a descriptor is whole, %zu bytes\n", b.length);

    assert(cache_asked(r, two, kCacheFieldEntityTag, now).value == nullptr);
    assert(cache_body_asked(r, two, now).value == nullptr);
    printf("the forgotten route has neither fields nor a body left\n");

    const char *const missing = "/articles/42?utm_source=mail";
    assert(cache_asked(r, cache_key_of(reinterpret_cast<const uint8_t *>(missing),
                                       strlen(missing)),
                       kCacheFieldEntityTag, now)
               .value == nullptr);
    printf("a route the dev did not declare is a miss\n");

    cache_sent(r, a.snapshot);
    cache_sent(r, s.snapshot);
    cache_sent(r, b.snapshot);
    cache_reader_closed(r);
    cache_close(c);

    spawned = the_writer_stands(writer, arm, &standing);
    assert(standing > 0);
    const char *const third = "/articles/99?param=q&foo=bar";
    const uint64_t three =
        cache_key_of(reinterpret_cast<const uint8_t *>(third), strlen(third));
    hand_over_inline(0, three, kCacheFieldEntityTag, 900, tag, sizeof tag);
    assert(cache_forget_everything(through_the_writer));
    printf("a second writer stored one more route and was told to empty everything\n");

    bool heard_the_emptying = false;
    struct timespec a_moment = {0, 200000000};
    nanosleep(&a_moment, nullptr);
    for (int at = 0; at < kThreads; at++) {
        cache_gone_datagram gone = {};
        while (recv(mine[at], &gone, sizeof gone, MSG_DONTWAIT) == (ssize_t) sizeof gone)
            if (gone.why == kCacheEmptied)
                heard_the_emptying = true;
    }
    assert(heard_the_emptying);
    the_writer_left(spawned);

    cache *const after = cache_open("wm-whole", "/tmp", 64);
    assert(after != nullptr);
    cache_reader *const then = cache_reader_opened(after);
    assert(then != nullptr);
    const uint64_t later = now_is();
    assert(cache_asked(then, one, kCacheFieldEntityTag, later).value == nullptr);
    assert(cache_asked(then, one, kCacheFieldStatus, later).value == nullptr);
    assert(cache_body_asked(then, one, later).value == nullptr);
    assert(cache_asked(then, three, kCacheFieldEntityTag, later).value == nullptr);
    printf("emptying takes everything, the route it never heard of as much as the new one\n");
    cache_reader_closed(then);
    cache_close(after);

    spawned = the_writer_stands(writer, arm, &standing);
    assert(standing > 0);
    hand_over_inline(0, three, kCacheFieldEntityTag, 900, tag, sizeof tag);
    hand_over_inline(0, three, kCacheFieldStatus, 900,
                     reinterpret_cast<const uint8_t *>("200"), 3);
    hand_over_in_a_file(1, three, kCacheFieldBody, 900, large, sizeof large);
    the_writer_left(spawned);
    cache_forgetting_closed(through_the_writer);
    through_the_writer = nullptr;
    printf("a third writer filled one route and left, so nothing is running now\n");

    cache_forgetting *const alone = cache_forgetting_on("wm-whole", "/tmp");
    assert(alone != nullptr);
    assert(cache_forget_value(alone, three, kCacheFieldStatus));
    assert(cache_forget_value(alone, three, kCacheFieldBody));

    cache *const without = cache_open("wm-whole", "/tmp", 64);
    assert(without != nullptr);
    cache_reader *const only = cache_reader_opened(without);
    assert(only != nullptr);
    const uint64_t by_now = now_is();
    assert(cache_asked(only, three, kCacheFieldStatus, by_now).value == nullptr);
    assert(cache_body_asked(only, three, by_now).value == nullptr);
    cache_answer kept = cache_asked(only, three, kCacheFieldEntityTag, by_now);
    assert(kept.value != nullptr && kept.length == sizeof tag);
    printf("with no writer anywhere, the library drops a value and a body itself\n");
    cache_sent(only, kept.snapshot);

    assert(cache_forget_everything(alone));
    assert(cache_changed(only));
    assert(cache_asked(only, three, kCacheFieldEntityTag, by_now).value == nullptr);
    printf("and empties the whole of it, which the reader sees on its next snapshot\n");
    cache_reader_closed(only);
    cache_close(without);
    cache_forgetting_closed(alone);

    printf("ok\n");
    return 0;
}
