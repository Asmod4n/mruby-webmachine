#include "cache.h"
#include "cache_file.h"

#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#include <nmmintrin.h>
#elif defined(__aarch64__)
#include <arm_acle.h>
#else
#error "cache_key_of has no CRC-32C for this architecture yet"
#endif

#include <lmdb.h>

enum { kFieldPrefix = 1 + sizeof(uint64_t), kBodyPrefix = sizeof(uint64_t) };

struct cache {
    MDB_env *environment;
    MDB_dbi fields;
    MDB_dbi bodies;
};

struct cache_reader {
    MDB_env *environment;
    MDB_dbi fields;
    MDB_dbi bodies;
    MDB_txn *reading;
    MDB_cursor *walking;
    unsigned in_flight;
};

#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("sse4.2"))) static uint64_t crc_of_word(const uint64_t taken,
                                                              const uint64_t word)
{
    return _mm_crc32_u64(taken, word);
}

__attribute__((target("sse4.2"))) static uint64_t crc_of_byte(const uint64_t taken,
                                                              const uint8_t byte)
{
    return _mm_crc32_u8((uint32_t) taken, byte);
}
#elif defined(__aarch64__)
__attribute__((target("+crc"))) static uint64_t crc_of_word(const uint64_t taken,
                                                            const uint64_t word)
{
    return __crc32cd((uint32_t) taken, word);
}

__attribute__((target("+crc"))) static uint64_t crc_of_byte(const uint64_t taken,
                                                            const uint8_t byte)
{
    return __crc32cb((uint32_t) taken, byte);
}
#endif

uint64_t cache_key_of(const uint8_t *const route, const size_t route_length)
{
    uint64_t low = ~(uint64_t) 0;
    uint64_t high = 0x9e3779b97f4a7c15ULL;
    size_t at = 0;
    for (; at + 16 <= route_length; at += 16) {
        uint64_t one = 0;
        uint64_t two = 0;
        memcpy(&one, route + at, sizeof one);
        memcpy(&two, route + at + 8, sizeof two);
        low = crc_of_word(low, one);
        high = crc_of_word(high, two);
    }
    for (; at + 8 <= route_length; at += 8) {
        uint64_t one = 0;
        memcpy(&one, route + at, sizeof one);
        low = crc_of_word(low, one);
    }
    for (; at < route_length; at++)
        low = crc_of_byte(low, route[at]);
    return (low * 0x9e3779b97f4a7c15ULL) ^ (high << 32) ^ high;
}

static bool app_name_is_a_token(const char *name)
{
    static const char marks[] = "!#$%&'*+-.^_`|~";
    if (name == NULL || *name == '\0')
        return false;
    for (const char *at = name; *at != '\0'; at++) {
        const unsigned char byte = (unsigned char) *at;
        if (byte >= 'a' && byte <= 'z')
            continue;
        if (byte >= 'A' && byte <= 'Z')
            continue;
        if (byte >= '0' && byte <= '9')
            continue;
        if (strchr(marks, (char) byte) != NULL)
            continue;
        return false;
    }
    return true;
}

char *cache_file_of(const char *app_name, const char *directory)
{
    if (!app_name_is_a_token(app_name) || directory == NULL)
        return NULL;
    const size_t directory_length = strlen(directory);
    const size_t name_length = strlen(app_name);
    char *const file = malloc(directory_length + 1 + name_length + 5);
    if (file == NULL)
        return NULL;
    memcpy(file, directory, directory_length);
    file[directory_length] = '/';
    memcpy(file + directory_length + 1, app_name, name_length);
    memcpy(file + directory_length + 1 + name_length, ".mdb", 5);
    return file;
}

cache *cache_open(const char *app_name, const char *directory, const unsigned readers)
{
    if (readers == 0)
        return NULL;
    char *const file = cache_file_of(app_name, directory);
    if (file == NULL)
        return NULL;
    cache *const of_app = calloc(1, sizeof *of_app);
    if (of_app == NULL) {
        free(file);
        return NULL;
    }
    if (mdb_env_create(&of_app->environment) != 0) {
        free(file);
        free(of_app);
        return NULL;
    }
    if (mdb_env_set_maxdbs(of_app->environment, 2) != 0 ||
        mdb_env_set_maxreaders(of_app->environment, readers) != 0 ||
        mdb_env_open(of_app->environment, file, MDB_RDONLY | MDB_NOSUBDIR | MDB_NOTLS, 0600) != 0) {
        mdb_env_close(of_app->environment);
        free(file);
        free(of_app);
        return NULL;
    }
    free(file);
    MDB_txn *opening = NULL;
    if (mdb_txn_begin(of_app->environment, NULL, MDB_RDONLY, &opening) != 0) {
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    if (mdb_dbi_open(opening, "fields", MDB_INTEGERKEY | MDB_DUPSORT, &of_app->fields) != 0 ||
        mdb_dbi_open(opening, "bodies", MDB_INTEGERKEY, &of_app->bodies) != 0) {
        mdb_txn_abort(opening);
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    if (mdb_txn_commit(opening) != 0) {
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    return of_app;
}

void cache_close(cache *of_app)
{
    if (of_app == NULL)
        return;
    mdb_env_close(of_app->environment);
    free(of_app);
}

cache_reader *cache_reader_opened(cache *of_app)
{
    if (of_app == NULL)
        return NULL;
    cache_reader *const of_thread = calloc(1, sizeof *of_thread);
    if (of_thread == NULL)
        return NULL;
    of_thread->environment = of_app->environment;
    of_thread->fields = of_app->fields;
    of_thread->bodies = of_app->bodies;
    if (mdb_txn_begin(of_thread->environment, NULL, MDB_RDONLY, &of_thread->reading) != 0) {
        free(of_thread);
        return NULL;
    }
    if (mdb_cursor_open(of_thread->reading, of_thread->fields, &of_thread->walking) != 0) {
        mdb_txn_abort(of_thread->reading);
        free(of_thread);
        return NULL;
    }
    return of_thread;
}

void cache_reader_closed(cache_reader *of_thread)
{
    if (of_thread == NULL)
        return;
    if (of_thread->walking != NULL)
        mdb_cursor_close(of_thread->walking);
    if (of_thread->reading != NULL)
        mdb_txn_abort(of_thread->reading);
    free(of_thread);
}

cache_answer cache_asked(cache_reader *of_thread, const uint64_t of_route, const uint8_t field,
                         const uint64_t now)
{
    const cache_answer nothing = {NULL, 0};
    if (of_thread == NULL || of_thread->walking == NULL)
        return nothing;
    uint8_t wanted = field;
    MDB_val asked = {sizeof of_route, (void *) &of_route};
    MDB_val found = {sizeof wanted, &wanted};
    if (mdb_cursor_get(of_thread->walking, &asked, &found, MDB_GET_BOTH_RANGE) != 0)
        return nothing;
    const uint8_t *const bytes = found.mv_data;
    if (found.mv_size < kFieldPrefix || bytes[0] != field)
        return nothing;
    uint64_t until = 0;
    memcpy(&until, bytes + 1, sizeof until);
    if (until <= now)
        return nothing;
    of_thread->in_flight++;
    const cache_answer answer = {bytes + kFieldPrefix, found.mv_size - kFieldPrefix};
    return answer;
}

cache_answer cache_body_asked(cache_reader *of_thread, const uint64_t of_route,
                              const uint64_t now)
{
    const cache_answer nothing = {NULL, 0};
    if (of_thread == NULL || of_thread->reading == NULL)
        return nothing;
    MDB_val asked = {sizeof of_route, (void *) &of_route};
    MDB_val found = {0, NULL};
    if (mdb_get(of_thread->reading, of_thread->bodies, &asked, &found) != 0)
        return nothing;
    const uint8_t *const bytes = found.mv_data;
    if (found.mv_size < kBodyPrefix)
        return nothing;
    uint64_t until = 0;
    memcpy(&until, bytes, sizeof until);
    if (until <= now)
        return nothing;
    of_thread->in_flight++;
    const cache_answer answer = {bytes + kBodyPrefix, found.mv_size - kBodyPrefix};
    return answer;
}

void cache_sent(cache_reader *of_thread)
{
    if (of_thread == NULL || of_thread->in_flight == 0)
        return;
    if (--of_thread->in_flight != 0)
        return;
    if (of_thread->walking != NULL) {
        mdb_cursor_close(of_thread->walking);
        of_thread->walking = NULL;
    }
    mdb_txn_reset(of_thread->reading);
    if (mdb_txn_renew(of_thread->reading) != 0) {
        mdb_txn_abort(of_thread->reading);
        of_thread->reading = NULL;
        return;
    }
    if (mdb_cursor_open(of_thread->reading, of_thread->fields, &of_thread->walking) != 0)
        of_thread->walking = NULL;
}
