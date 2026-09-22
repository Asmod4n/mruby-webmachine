#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include <lmdb.h>
}

namespace
{

constexpr size_t kResponseBytes = 1024;

std::string key_at(const size_t which)
{
    char room[64];
    const int length =
        std::snprintf(room, sizeof room, "GET /articles/%zu?param=xyz&foo=bar", which);
    return std::string(room, static_cast<size_t>(length));
}

size_t hash_of(const std::string_view text)
{
    size_t number = 1469598103934665603ULL;
    for (const char one : text) {
        number ^= static_cast<unsigned char>(one);
        number *= 1099511628211ULL;
    }
    return number;
}

enum class Shape { kWholeKey, kHashedKey };

struct Filled {
    MDB_env *environment;
    MDB_dbi database;
    MDB_txn *reading;
    std::vector<std::string> key;
    std::vector<size_t> hashed;
};

Filled &filled_with(const size_t entries, const Shape shape)
{
    static std::vector<Filled *> made;
    static std::vector<std::string> names;
    const std::string name =
        std::to_string(entries) + (shape == Shape::kHashedKey ? "-hashed" : "-whole");
    for (size_t at = 0; at < names.size(); at++)
        if (names.at(at) == name)
            return *made.at(at);

    const std::string file = "/tmp/wm-cache-" + name + ".mdb";
    std::remove(file.c_str());
    std::remove((file + "-lock").c_str());

    std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 960\r\n\r\n";
    response.resize(kResponseBytes, 'x');

    const unsigned flags = shape == Shape::kHashedKey ? MDB_INTEGERKEY : 0;

    MDB_env *writing = nullptr;
    if (mdb_env_create(&writing) != 0)
        std::abort();
    mdb_env_set_mapsize(writing, size_t{8} << 30);
    if (mdb_env_open(writing, file.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP, 0600) != 0)
        std::abort();
    MDB_txn *putting = nullptr;
    MDB_dbi database = 0;
    if (mdb_txn_begin(writing, nullptr, 0, &putting) != 0)
        std::abort();
    if (mdb_dbi_open(putting, nullptr, flags, &database) != 0)
        std::abort();
    for (size_t which = 0; which < entries; which++) {
        const std::string one = key_at(which);
        const size_t number = hash_of(one);
        MDB_val k = shape == Shape::kHashedKey
                        ? MDB_val{sizeof number, const_cast<size_t *>(&number)}
                        : MDB_val{one.size(), const_cast<char *>(one.data())};
        MDB_val v{response.size(), const_cast<char *>(response.data())};
        if (mdb_put(putting, database, &k, &v, 0) != 0)
            std::abort();
    }
    if (mdb_txn_commit(putting) != 0)
        std::abort();
    mdb_env_close(writing);

    Filled *const one = new Filled{};
    if (mdb_env_create(&one->environment) != 0)
        std::abort();
    mdb_env_set_mapsize(one->environment, size_t{8} << 30);
    if (mdb_env_open(one->environment, file.c_str(), MDB_RDONLY | MDB_NOSUBDIR | MDB_NOTLS,
                     0600) != 0)
        std::abort();
    if (mdb_txn_begin(one->environment, nullptr, MDB_RDONLY, &one->reading) != 0)
        std::abort();
    if (mdb_dbi_open(one->reading, nullptr, flags, &one->database) != 0)
        std::abort();
    one->key.reserve(entries);
    for (size_t which = 0; which < entries; which++)
        one->key.push_back(key_at(which));
    std::mt19937_64 order(1);
    std::shuffle(one->key.begin(), one->key.end(), order);
    one->hashed.reserve(entries);
    for (const std::string &text : one->key)
        one->hashed.push_back(hash_of(text));

    made.push_back(one);
    names.push_back(name);
    return *one;
}

void cache_asked_by_whole_key(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), Shape::kWholeKey);
    size_t at = 0;
    for (auto _ : state) {
        const std::string &one = held.key.at(at);
        if (++at == held.key.size())
            at = 0;
        MDB_val asked{one.size(), const_cast<char *>(one.data())};
        MDB_val found{0, nullptr};
        if (mdb_get(held.reading, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void cache_asked_by_hashed_key(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), Shape::kHashedKey);
    size_t at = 0;
    for (auto _ : state) {
        const size_t number = held.hashed.at(at);
        if (++at == held.hashed.size())
            at = 0;
        MDB_val asked{sizeof number, const_cast<size_t *>(&number)};
        MDB_val found{0, nullptr};
        if (mdb_get(held.reading, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void cache_asked_by_hashing_then_key(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), Shape::kHashedKey);
    size_t at = 0;
    for (auto _ : state) {
        const std::string &text = held.key.at(at);
        if (++at == held.key.size())
            at = 0;
        const size_t number = hash_of(text);
        MDB_val asked{sizeof number, const_cast<size_t *>(&number)};
        MDB_val found{0, nullptr};
        if (mdb_get(held.reading, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void cache_asked_on_a_renewed_snapshot(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), Shape::kHashedKey);
    size_t at = 0;
    for (auto _ : state) {
        const size_t number = held.hashed.at(at);
        if (++at == held.hashed.size())
            at = 0;
        MDB_val asked{sizeof number, const_cast<size_t *>(&number)};
        MDB_val found{0, nullptr};
        if (mdb_get(held.reading, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
        mdb_txn_reset(held.reading);
        if (mdb_txn_renew(held.reading) != 0) [[unlikely]]
            std::abort();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void cache_asked_on_a_fresh_transaction(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), Shape::kHashedKey);
    size_t at = 0;
    for (auto _ : state) {
        const size_t number = held.hashed.at(at);
        if (++at == held.hashed.size())
            at = 0;
        MDB_txn *one = nullptr;
        if (mdb_txn_begin(held.environment, nullptr, MDB_RDONLY, &one) != 0) [[unlikely]]
            std::abort();
        MDB_val asked{sizeof number, const_cast<size_t *>(&number)};
        MDB_val found{0, nullptr};
        if (mdb_get(one, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
        mdb_txn_abort(one);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(cache_asked_on_a_renewed_snapshot)->Arg(1000)->Arg(100000)->Arg(1000000);
BENCHMARK(cache_asked_on_a_fresh_transaction)->Arg(1000)->Arg(100000)->Arg(1000000);
BENCHMARK(cache_asked_by_whole_key)->Arg(1000)->Arg(100000)->Arg(1000000);
BENCHMARK(cache_asked_by_hashed_key)->Arg(1000)->Arg(100000)->Arg(1000000);
BENCHMARK(cache_asked_by_hashing_then_key)->Arg(1000)->Arg(100000)->Arg(1000000);

}
