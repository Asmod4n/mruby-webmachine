#include "cache.h"

#include <stdlib.h>
#include <string.h>

#include <lmdb.h>

struct cache {
    MDB_env *environment;
    MDB_dbi database;
    MDB_txn *reading;
};

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

static char *file_of(const char *directory, const char *app_name)
{
    const size_t directory_length = strlen(directory);
    const size_t name_length = strlen(app_name);
    const size_t length = directory_length + 1 + name_length + 4;
    char *const file = malloc(length + 1);
    if (file == NULL)
        return NULL;
    memcpy(file, directory, directory_length);
    file[directory_length] = '/';
    memcpy(file + directory_length + 1, app_name, name_length);
    memcpy(file + directory_length + 1 + name_length, ".mdb", 5);
    return file;
}

cache *cache_open(const char *app_name, const char *directory)
{
    if (!app_name_is_a_token(app_name) || directory == NULL)
        return NULL;
    char *const file = file_of(directory, app_name);
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
    if (mdb_env_open(of_app->environment, file, MDB_RDONLY | MDB_NOSUBDIR | MDB_NOTLS, 0600) != 0) {
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
    if (mdb_dbi_open(opening, NULL, 0, &of_app->database) != 0) {
        mdb_txn_abort(opening);
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    mdb_txn_abort(opening);
    return of_app;
}

void cache_close(cache *of_app)
{
    if (of_app == NULL)
        return;
    if (of_app->reading != NULL)
        mdb_txn_abort(of_app->reading);
    mdb_env_close(of_app->environment);
    free(of_app);
}

bool cache_reading_began(cache *of_app)
{
    if (of_app == NULL)
        return false;
    if (of_app->reading != NULL)
        return mdb_txn_renew(of_app->reading) == 0;
    return mdb_txn_begin(of_app->environment, NULL, MDB_RDONLY, &of_app->reading) == 0;
}

cache_answer cache_asked(cache *of_app, const char *key, size_t key_length)
{
    const cache_answer nothing = {NULL, 0};
    if (of_app == NULL || of_app->reading == NULL || key == NULL || key_length == 0)
        return nothing;
    MDB_val asked = {key_length, (void *) key};
    MDB_val found = {0, NULL};
    if (mdb_get(of_app->reading, of_app->database, &asked, &found) != 0)
        return nothing;
    const cache_answer answer = {found.mv_data, found.mv_size};
    return answer;
}

void cache_reading_ended(cache *of_app)
{
    if (of_app == NULL || of_app->reading == NULL)
        return;
    mdb_txn_reset(of_app->reading);
}
