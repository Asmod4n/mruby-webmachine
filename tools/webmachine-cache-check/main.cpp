
#include <assert.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../src/cache.h"
#include "../../src/cache_datagram.h"

extern char **environ;

#define THREADS 3
#define FIRST_FD 3
#define PIECE 4096

static int mine[THREADS];

static void hand_over(const int thread, const char *const declared, const uint8_t *const body,
                      const size_t body_length, const uint32_t freshness_lifetime)
{
    const uint64_t key = cache_key_of((const uint8_t *) declared, strlen(declared));
    const uint32_t key_length = (uint32_t) strlen(declared);
    uint8_t room[sizeof(cache_datagram_header) + PIECE + 256];
    cache_datagram_header header = {0};
    header.group = (uint64_t) thread + 1;
    header.key = key;
    header.message_length = (uint32_t) body_length;
    header.key_length = key_length;
    header.freshness_lifetime = freshness_lifetime;

    size_t sent = 0;
    uint32_t at = 0;
    while (at == 0 || sent < body_length) {
        header.at = at;
        const size_t room_for_body = at == 0 ? PIECE - key_length : PIECE;
        size_t take = body_length - sent;
        if (take > room_for_body)
            take = room_for_body;
        size_t length = sizeof header;
        memcpy(room, &header, sizeof header);
        if (at == 0) {
            memcpy(room + length, declared, key_length);
            length += key_length;
        }
        memcpy(room + length, body + sent, take);
        length += take;
        assert(send(mine[thread], room, length, 0) == (ssize_t) length);
        sent += take;
        at++;
    }
}

int main(int argc, char **argv)
{
    const char *const file = "/tmp/wm-whole.mdb";
    remove(file);
    remove("/tmp/wm-whole.mdb-lock");

    int theirs[THREADS];
    for (int at = 0; at < THREADS; at++) {
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
        mine[at] = pair[0];
        theirs[at] = fcntl(pair[1], F_DUPFD_CLOEXEC, FIRST_FD + THREADS);
        assert(theirs[at] >= 0);
        close(pair[1]);
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (int at = 0; at < THREADS; at++)
        posix_spawn_file_actions_adddup2(&actions, theirs[at], FIRST_FD + at);
    posix_spawn_file_actions_addclosefrom_np(&actions, FIRST_FD + THREADS);

    char threads[8], map[32], readers[8], batch[8];
    snprintf(threads, sizeof threads, "%d", THREADS);
    snprintf(map, sizeof map, "%llu", (unsigned long long) (256ull << 20));
    snprintf(readers, sizeof readers, "%d", 64);
    snprintf(batch, sizeof batch, "%d", 2);
    char *const writer = argc > 1 ? argv[1] : (char *) "./webmachine-cache";
    char *child[] = {writer, (char *) file, threads, map, readers, batch, NULL};
    pid_t spawned = 0;
    const int status = posix_spawn(&spawned, writer, &actions, NULL, child, environ);
    posix_spawn_file_actions_destroy(&actions);
    assert(status == 0);
    for (int at = 0; at < THREADS; at++)
        close(theirs[at]);
    printf("the server spawned the writer and kept one socket per thread\n");

    static uint8_t small[64];
    memset(small, 'a', sizeof small);
    static uint8_t large[PIECE * 3 + 77];
    for (size_t at = 0; at < sizeof large; at++)
        large[at] = (uint8_t) ('0' + at % 10);

    hand_over(0, "GET /articles/42?param=xyz&foo=bar", small, sizeof small, 900);
    hand_over(1, "GET /articles/7?param=abc&foo=bar", large, sizeof large, 900);
    hand_over(2, "GET /articles/9?param=q&foo=bar", small, sizeof small, 900);
    printf("three entries handed over, one of them across %zu datagrams\n",
           (size_t) (sizeof large / PIECE + 2));

    for (int at = 0; at < THREADS; at++)
        close(mine[at]);
    int left = 0;
    waitpid(spawned, &left, 0);
    printf("the writer drained its sockets and left with %d\n", WEXITSTATUS(left));
    assert(WEXITSTATUS(left) == 0);

    cache *const c = cache_open("wm-whole", "/tmp", 64);
    assert(c != NULL);
    cache_reader *const r = cache_reader_opened(c);
    assert(r != NULL);

    const char *const one = "GET /articles/42?param=xyz&foo=bar";
    cache_answer a = cache_asked(r, cache_key_of((const uint8_t *) one, strlen(one)));
    assert(a.value != NULL);
    assert(a.length == strlen(one) + sizeof small);
    assert(memcmp(a.value, one, strlen(one)) == 0);
    assert(memcmp(a.value + strlen(one), small, sizeof small) == 0);
    printf("the reader finds the small entry, key first, then the answer\n");

    const char *const two = "GET /articles/7?param=abc&foo=bar";
    cache_answer b = cache_asked(r, cache_key_of((const uint8_t *) two, strlen(two)));
    assert(b.value != NULL);
    assert(b.length == strlen(two) + sizeof large);
    assert(memcmp(b.value + strlen(two), large, sizeof large) == 0);
    printf("the entry that crossed several datagrams came back whole, %zu bytes\n", b.length);

    const char *const missing = "GET /articles/42?utm_source=mail";
    assert(cache_asked(r, cache_key_of((const uint8_t *) missing, strlen(missing))).value == NULL);
    printf("a key the dev did not declare is a miss\n");

    cache_sent(r, a.snapshot);
    cache_sent(r, b.snapshot);
    cache_reader_closed(r);
    cache_close(c);
    printf("ok\n");
    return 0;
}
