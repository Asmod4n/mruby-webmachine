#include <benchmark/benchmark.h>

#include <array>
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
    const int length = std::snprintf(room, sizeof room, "GET /articles/%zu?param=xyz&foo=bar", which);
    return std::string(room, static_cast<size_t>(length));
}

struct Filled {
    MDB_env *environment;
    MDB_dbi database;
    MDB_txn *reading;
    std::vector<std::string> key;
};

Filled &filled_with(const size_t entries, const bool shuffled)
{
    static std::vector<Filled *> made;
    static std::vector<size_t> sizes;
    for (size_t at = 0; at < sizes.size(); at++)
        if (sizes.at(at) == entries + (shuffled ? 0 : 1))
            return *made.at(at);

    const std::string file = "/tmp/wm-cache-" + std::to_string(entries) + ".mdb";
    std::remove(file.c_str());
    std::remove((file + "-lock").c_str());

    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 960\r\n\r\n";
    response.resize(kResponseBytes, 'x');

    MDB_env *writing = nullptr;
    if (mdb_env_create(&writing) != 0)
        std::abort();
    mdb_env_set_mapsize(writing, size_t{4} << 30);
    if (mdb_env_open(writing, file.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP, 0600) != 0)
        std::abort();
    MDB_txn *putting = nullptr;
    MDB_dbi database = 0;
    if (mdb_txn_begin(writing, nullptr, 0, &putting) != 0)
        std::abort();
    if (mdb_dbi_open(putting, nullptr, 0, &database) != 0)
        std::abort();
    for (size_t which = 0; which < entries; which++) {
        const std::string one = key_at(which);
        MDB_val k{one.size(), const_cast<char *>(one.data())};
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
    mdb_env_set_mapsize(one->environment, size_t{4} << 30);
    if (mdb_env_open(one->environment, file.c_str(),
                     MDB_RDONLY | MDB_NOSUBDIR | MDB_NOTLS, 0600) != 0)
        std::abort();
    if (mdb_txn_begin(one->environment, nullptr, MDB_RDONLY, &one->reading) != 0)
        std::abort();
    if (mdb_dbi_open(one->reading, nullptr, 0, &one->database) != 0)
        std::abort();
    one->key.reserve(entries);
    for (size_t which = 0; which < entries; which++)
        one->key.push_back(key_at(which));
    if (shuffled) {
        std::mt19937_64 order(1);
        std::shuffle(one->key.begin(), one->key.end(), order);
    }

    made.push_back(one);
    sizes.push_back(entries + (shuffled ? 0 : 1));
    return *one;
}

void cache_asked(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), state.range(1) != 0);
    size_t at = 0;
    size_t bytes = 0;
    for (auto _ : state) {
        const std::string &one = held.key.at(at);
        if (++at == held.key.size())
            at = 0;
        MDB_val asked{one.size(), const_cast<char *>(one.data())};
        MDB_val found{0, nullptr};
        if (mdb_get(held.reading, held.database, &asked, &found) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
        bytes += found.mv_size;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<int64_t>(bytes));
}

BENCHMARK(cache_asked)->Args({1000, 1})->Args({100000, 1})->Args({1000000, 1})->Args({1000, 0})->Args({100000, 0})->Args({1000000, 0});

void cache_asked_and_renewed(benchmark::State &state)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), true);
    size_t at = 0;
    for (auto _ : state) {
        mdb_txn_reset(held.reading);
        if (mdb_txn_renew(held.reading) != 0) [[unlikely]]
            std::abort();
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

BENCHMARK(cache_asked_and_renewed)->Arg(1000)->Arg(1000000);

}
