#include "cache.h"
#include <lmdb.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void write_one(const char *file, const char *key, const char *value)
{
    MDB_env *e = NULL;
    MDB_txn *t = NULL;
    MDB_dbi d = 0;
    assert(mdb_env_create(&e) == 0);
    assert(mdb_env_open(e, file, MDB_NOSUBDIR, 0600) == 0);
    assert(mdb_txn_begin(e, NULL, 0, &t) == 0);
    assert(mdb_dbi_open(t, NULL, 0, &d) == 0);
    MDB_val k = {strlen(key), (void *) key};
    MDB_val v = {strlen(value), (void *) value};
    assert(mdb_put(t, d, &k, &v, 0) == 0);
    assert(mdb_txn_commit(t) == 0);
    mdb_env_close(e);
}

int main(void)
{
    const char *const response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
    const char *const declared = "GET /articles/42?param=xyz&foo=bar";
    write_one("/home/user/cachetest.mdb", declared, response);

    assert(cache_open("has/slash", "/home/user") == NULL);
    assert(cache_open("..", "/home/user") == NULL);
    assert(cache_open("", "/home/user") == NULL);
    printf("a name that is not a token is refused\n");

    cache *const c = cache_open("cachetest", "/home/user");
    assert(c != NULL);
    printf("the app name names the file\n");

    assert(cache_asked(c, declared, strlen(declared)).value == NULL);
    printf("no reading, no answer\n");

    assert(cache_reading_began(c));
    cache_answer a = cache_asked(c, declared, strlen(declared));
    assert(a.value != NULL && a.length == strlen(response));
    assert(memcmp(a.value, response, a.length) == 0);
    printf("the value comes back byte for byte\n");

    const char *const utm = "GET /articles/42?utm_source=mail";
    assert(cache_asked(c, utm, strlen(utm)).value == NULL);
    printf("a key the dev did not declare is a miss\n");

    cache_reading_ended(c);
    assert(cache_reading_began(c));
    a = cache_asked(c, declared, strlen(declared));
    assert(a.value != NULL);
    printf("a reset transaction renews and reads again\n");
    cache_reading_ended(c);
    cache_close(c);
    printf("ok\n");
    return 0;
}
