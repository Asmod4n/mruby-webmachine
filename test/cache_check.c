#include "cache.h"
#include <assert.h>
#include <lmdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t hash_of(const char *text, size_t length)
{
    uint64_t low = ~(uint64_t) 0;
    uint64_t high = 0x9e3779b97f4a7c15ULL;
    size_t at = 0;
    for (; at + 16 <= length; at += 16) {
        uint64_t one = 0, two = 0;
        memcpy(&one, text + at, 8);
        memcpy(&two, text + at + 8, 8);
        low = __builtin_ia32_crc32di(low, one);
        high = __builtin_ia32_crc32di(high, two);
    }
    for (; at + 8 <= length; at += 8) {
        uint64_t one = 0;
        memcpy(&one, text + at, 8);
        low = __builtin_ia32_crc32di(low, one);
    }
    for (; at < length; at++)
        low = __builtin_ia32_crc32qi((uint32_t) low, (unsigned char) text[at]);
    return (low * 0x9e3779b97f4a7c15ULL) ^ (high << 32) ^ high;
}

static void store(const char *file, const char *key, const char *value)
{
    MDB_env *e = NULL;
    MDB_txn *t = NULL;
    MDB_dbi d = 0;
    assert(mdb_env_create(&e) == 0);
    assert(mdb_env_open(e, file, MDB_NOSUBDIR, 0600) == 0);
    assert(mdb_txn_begin(e, NULL, 0, &t) == 0);
    assert(mdb_dbi_open(t, NULL, MDB_INTEGERKEY, &d) == 0);
    const uint64_t number = hash_of(key, strlen(key));
    MDB_val k = {sizeof number, (void *) &number};
    MDB_val v = {strlen(value), (void *) value};
    assert(mdb_put(t, d, &k, &v, 0) == 0);
    assert(mdb_txn_commit(t) == 0);
    mdb_env_close(e);
}

static cache_answer ask(cache_reader *r, const char *key)
{
    const uint64_t number = hash_of(key, strlen(key));
    return cache_asked(r, (const char *) &number, sizeof number);
}

int main(void)
{
    const char *const file = "/tmp/wm-cachecheck.mdb";
    const char *const declared = "GET /articles/42?param=xyz&foo=bar";
    const char *const first = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    const char *const second = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nho";
    remove(file);
    remove("/tmp/wm-cachecheck.mdb-lock");
    store(file, declared, first);

    assert(cache_open("has/slash", "/tmp", 64) == NULL);
    assert(cache_open("..", "/tmp", 64) == NULL);
    assert(cache_open("", "/tmp", 64) == NULL);
    assert(cache_open("wm-cachecheck", "/tmp", 0) == NULL);
    printf("a name that is not a token, and no readers, are refused\n");

    cache *const c = cache_open("wm-cachecheck", "/tmp", 64);
    assert(c != NULL);
    cache_reader *const r = cache_reader_opened(c);
    assert(r != NULL);
    printf("the app name names the file, and a reader stands on its own heap\n");

    cache_answer a = ask(r, declared);
    assert(a.value != NULL && a.length == strlen(first));
    assert(memcmp(a.value, first, a.length) == 0);
    printf("the value comes back byte for byte\n");

    assert(ask(r, "GET /articles/42?utm_source=mail").value == NULL);
    printf("a key the dev did not declare is a miss\n");

    store(file, declared, second);
    cache_answer stale = ask(r, declared);
    assert(stale.value != NULL && memcmp(stale.value, first, stale.length) == 0);
    printf("a snapshot does not see a commit that came after it\n");

    assert(cache_changed(r));
    assert(memcmp(a.value, first, a.length) == 0);
    assert(memcmp(stale.value, first, stale.length) == 0);
    printf("a send in flight keeps reading the snapshot it was given\n");

    cache_answer fresh = ask(r, declared);
    assert(fresh.value != NULL && fresh.length == strlen(second));
    assert(memcmp(fresh.value, second, fresh.length) == 0);
    assert(fresh.snapshot != a.snapshot);
    printf("a request that arrives after the commit sees it\n");

    cache_sent(r, a.snapshot);
    cache_sent(r, stale.snapshot);
    cache_sent(r, fresh.snapshot);
    printf("the drained snapshot is let go when its last send ends\n");

    cache_reader_closed(r);
    cache_close(c);
    printf("ok\n");
    return 0;
}
