#include "cache_forget.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <lmdb.h>

#include "cache.h"
#include "cache_datagram.h"
#include "cache_file.h"

struct cache_forgetting {
    int to_the_writer;
    MDB_env *environment;
    MDB_dbi fields;
    MDB_dbi bodies;
    MDB_dbi due;
};

cache_forgetting *cache_forgetting_through(const int to_the_writer)
{
    if (to_the_writer < 0)
        return NULL;
    cache_forgetting *const of_app = calloc(1, sizeof *of_app);
    if (of_app == NULL)
        return NULL;
    of_app->to_the_writer = to_the_writer;
    return of_app;
}

cache_forgetting *cache_forgetting_on(const char *const app_name, const char *const directory)
{
    char *const file = cache_file_of(app_name, directory);
    if (file == NULL)
        return NULL;
    cache_forgetting *const of_app = calloc(1, sizeof *of_app);
    if (of_app == NULL) {
        free(file);
        return NULL;
    }
    of_app->to_the_writer = -1;
    if (mdb_env_create(&of_app->environment) != 0) {
        free(file);
        free(of_app);
        return NULL;
    }
    if (mdb_env_set_maxdbs(of_app->environment, 3) != 0 ||
        mdb_env_open(of_app->environment, file, MDB_NOSUBDIR | MDB_NOTLS, 0600) != 0) {
        mdb_env_close(of_app->environment);
        free(file);
        free(of_app);
        return NULL;
    }
    free(file);
    MDB_txn *opening = NULL;
    if (mdb_txn_begin(of_app->environment, NULL, 0, &opening) != 0) {
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    if (mdb_dbi_open(opening, "fields", MDB_INTEGERKEY | MDB_DUPSORT | MDB_CREATE,
                     &of_app->fields) != 0 ||
        mdb_dbi_open(opening, "bodies", MDB_INTEGERKEY | MDB_CREATE, &of_app->bodies) != 0 ||
        mdb_dbi_open(opening, "due", MDB_INTEGERKEY | MDB_DUPSORT | MDB_CREATE, &of_app->due) !=
            0 ||
        mdb_txn_commit(opening) != 0) {
        mdb_txn_abort(opening);
        mdb_env_close(of_app->environment);
        free(of_app);
        return NULL;
    }
    return of_app;
}

void cache_forgetting_closed(cache_forgetting *of_app)
{
    if (of_app == NULL)
        return;
    if (of_app->environment != NULL)
        mdb_env_close(of_app->environment);
    free(of_app);
}

static bool told(cache_forgetting *const of_app, const uint64_t route, const uint8_t field,
                 const uint8_t forget)
{
    cache_datagram_header header;
    memset(&header, 0, sizeof header);
    header.route = route;
    header.field = field;
    header.forget = forget;
    return send(of_app->to_the_writer, &header, sizeof header, MSG_NOSIGNAL) ==
           (ssize_t) sizeof header;
}

static bool did_it(cache_forgetting *const of_app, const uint64_t route, const uint8_t field,
                   const uint8_t forget)
{
    MDB_txn *deleting = NULL;
    if (mdb_txn_begin(of_app->environment, NULL, 0, &deleting) != 0)
        return false;
    bool done = true;
    MDB_val key = {sizeof route, (void *) &route};
    if (forget == kCacheForgetsEverything) {
        done = mdb_drop(deleting, of_app->fields, 0) == 0 &&
               mdb_drop(deleting, of_app->bodies, 0) == 0 &&
               mdb_drop(deleting, of_app->due, 0) == 0;
    } else if (forget == kCacheForgetsARoute) {
        const int fields = mdb_del(deleting, of_app->fields, &key, NULL);
        const int bodies = mdb_del(deleting, of_app->bodies, &key, NULL);
        done = (fields == 0 || fields == MDB_NOTFOUND) &&
               (bodies == 0 || bodies == MDB_NOTFOUND);
    } else if (field == kCacheFieldBody) {
        const int gone = mdb_del(deleting, of_app->bodies, &key, NULL);
        done = gone == 0 || gone == MDB_NOTFOUND;
    } else {
        MDB_cursor *walking = NULL;
        if (mdb_cursor_open(deleting, of_app->fields, &walking) != 0) {
            mdb_txn_abort(deleting);
            return false;
        }
        uint8_t wanted = field;
        MDB_val standing = {sizeof wanted, &wanted};
        if (mdb_cursor_get(walking, &key, &standing, MDB_GET_BOTH_RANGE) == 0 &&
            standing.mv_size >= 1 && ((const uint8_t *) standing.mv_data)[0] == field)
            done = mdb_cursor_del(walking, 0) == 0;
        mdb_cursor_close(walking);
    }
    if (!done) {
        mdb_txn_abort(deleting);
        return false;
    }
    return mdb_txn_commit(deleting) == 0;
}

static bool forgetting(cache_forgetting *const of_app, const uint64_t route, const uint8_t field,
                       const uint8_t forget)
{
    if (of_app == NULL)
        return false;
    if (of_app->environment != NULL)
        return did_it(of_app, route, field, forget);
    return told(of_app, route, field, forget);
}

bool cache_forget_value(cache_forgetting *of_app, const uint64_t route, const uint8_t field)
{
    if (field >= kCacheFieldCount)
        return false;
    return forgetting(of_app, route, field, kCacheForgetsAValue);
}

bool cache_forget_route(cache_forgetting *of_app, const uint64_t route)
{
    return forgetting(of_app, route, kCacheFieldCount, kCacheForgetsARoute);
}

bool cache_forget_everything(cache_forgetting *of_app)
{
    return forgetting(of_app, 0, kCacheFieldCount, kCacheForgetsEverything);
}
