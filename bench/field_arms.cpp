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

constexpr uint8_t kFields = 10;
constexpr size_t kFieldBytes = 32;
constexpr size_t kAskedTogether = 3;

std::string route_at(const size_t which)
{
    char room[64];
    const int length =
        std::snprintf(room, sizeof room, "GET /articles/%zu?param=xyz&foo=bar", which);
    return std::string(room, static_cast<size_t>(length));
}

uint64_t route_key_of(const std::string_view text)
{
    uint64_t number = 1469598103934665603ULL;
    for (const char one : text) {
        number ^= static_cast<unsigned char>(one);
        number *= 1099511628211ULL;
    }
    return number;
}

uint64_t mixed(const uint64_t of_route, const uint8_t field)
{
    return (of_route ^ (field * 0x9e3779b97f4a7c15ULL)) * 0xff51afd7ed558ccdULL;
}

uint64_t in_the_top_byte(const uint64_t of_route, const uint8_t field)
{
    return (of_route & 0x00ffffffffffffffULL) | (static_cast<uint64_t>(field) << 56);
}

enum class Shape { kMixed, kTopByte, kOwnDatabase, kIndex };

struct Filled {
    MDB_env *environment;
    MDB_dbi database[kFields];
    MDB_txn *reading;
    MDB_cursor *walking;
    std::vector<uint64_t> route;
};

const char *named(const Shape shape)
{
    switch (shape) {
    case Shape::kMixed: return "mixed";
    case Shape::kTopByte: return "topbyte";
    case Shape::kOwnDatabase: return "perfield";
    case Shape::kIndex: return "index";
    }
    std::abort();
}

Filled &filled_with(const size_t routes, const Shape shape)
{
    static std::vector<Filled *> made;
    static std::vector<std::string> names;
    const std::string name = std::to_string(routes) + "-" + named(shape);
    for (size_t at = 0; at < names.size(); at++)
        if (names.at(at) == name)
            return *made.at(at);

    const std::string file = "/tmp/wm-field-" + name + ".mdb";
    std::remove(file.c_str());
    std::remove((file + "-lock").c_str());

    std::string value(kFieldBytes, 'v');

    MDB_env *writing = nullptr;
    if (mdb_env_create(&writing) != 0)
        std::abort();
    mdb_env_set_mapsize(writing, size_t{8} << 30);
    mdb_env_set_maxdbs(writing, kFields);
    if (mdb_env_open(writing, file.c_str(), MDB_NOSUBDIR | MDB_WRITEMAP, 0600) != 0)
        std::abort();
    MDB_txn *putting = nullptr;
    MDB_dbi database[kFields] = {};
    if (mdb_txn_begin(writing, nullptr, 0, &putting) != 0)
        std::abort();
    if (shape == Shape::kOwnDatabase) {
        for (uint8_t field = 0; field < kFields; field++) {
            const std::string one = "field-" + std::to_string(field);
            if (mdb_dbi_open(putting, one.c_str(), MDB_INTEGERKEY | MDB_CREATE,
                             &database[field]) != 0)
                std::abort();
        }
    } else if (shape == Shape::kIndex) {
        if (mdb_dbi_open(putting, nullptr, MDB_INTEGERKEY | MDB_DUPSORT, &database[0]) != 0)
            std::abort();
    } else {
        if (mdb_dbi_open(putting, nullptr, MDB_INTEGERKEY, &database[0]) != 0)
            std::abort();
    }

    uint8_t room[1 + kFieldBytes];
    std::memset(room + 1, 'v', kFieldBytes);
    for (size_t which = 0; which < routes; which++) {
        const uint64_t of_route = route_key_of(route_at(which));
        for (uint8_t field = 0; field < kFields; field++) {
            if (shape == Shape::kIndex) {
                room[0] = field;
                MDB_val k{sizeof of_route, const_cast<uint64_t *>(&of_route)};
                MDB_val v{sizeof room, room};
                if (mdb_put(putting, database[0], &k, &v, 0) != 0)
                    std::abort();
                continue;
            }
            const uint64_t key = shape == Shape::kMixed      ? mixed(of_route, field)
                                 : shape == Shape::kTopByte  ? in_the_top_byte(of_route, field)
                                                             : of_route;
            MDB_val k{sizeof key, const_cast<uint64_t *>(&key)};
            MDB_val v{value.size(), const_cast<char *>(value.data())};
            if (mdb_put(putting, database[shape == Shape::kOwnDatabase ? field : 0], &k, &v,
                        0) != 0)
                std::abort();
        }
    }
    if (mdb_txn_commit(putting) != 0)
        std::abort();
    mdb_env_close(writing);

    Filled *const one = new Filled{};
    if (mdb_env_create(&one->environment) != 0)
        std::abort();
    mdb_env_set_mapsize(one->environment, size_t{8} << 30);
    mdb_env_set_maxdbs(one->environment, kFields);
    if (mdb_env_open(one->environment, file.c_str(), MDB_RDONLY | MDB_NOSUBDIR | MDB_NOTLS,
                     0600) != 0)
        std::abort();
    if (mdb_txn_begin(one->environment, nullptr, MDB_RDONLY, &one->reading) != 0)
        std::abort();
    if (shape == Shape::kOwnDatabase) {
        for (uint8_t field = 0; field < kFields; field++) {
            const std::string named_one = "field-" + std::to_string(field);
            if (mdb_dbi_open(one->reading, named_one.c_str(), MDB_INTEGERKEY,
                             &one->database[field]) != 0)
                std::abort();
        }
    } else if (shape == Shape::kIndex) {
        if (mdb_dbi_open(one->reading, nullptr, MDB_INTEGERKEY | MDB_DUPSORT,
                         &one->database[0]) != 0)
            std::abort();
        if (mdb_cursor_open(one->reading, one->database[0], &one->walking) != 0)
            std::abort();
    } else {
        if (mdb_dbi_open(one->reading, nullptr, MDB_INTEGERKEY, &one->database[0]) != 0)
            std::abort();
    }

    one->route.reserve(routes);
    for (size_t which = 0; which < routes; which++)
        one->route.push_back(route_key_of(route_at(which)));
    std::mt19937_64 order(1);
    std::shuffle(one->route.begin(), one->route.end(), order);

    made.push_back(one);
    names.push_back(name);
    return *one;
}

void got(Filled &held, const Shape shape, const uint64_t of_route, const uint8_t field)
{
    if (shape == Shape::kIndex) {
        uint8_t wanted[1 + kFieldBytes] = {field};
        MDB_val asked{sizeof of_route, const_cast<uint64_t *>(&of_route)};
        MDB_val found{sizeof wanted, wanted};
        if (mdb_cursor_get(held.walking, &asked, &found, MDB_GET_BOTH_RANGE) != 0) [[unlikely]]
            std::abort();
        benchmark::DoNotOptimize(found.mv_data);
        return;
    }
    const uint64_t key = shape == Shape::kMixed      ? mixed(of_route, field)
                         : shape == Shape::kTopByte  ? in_the_top_byte(of_route, field)
                                                     : of_route;
    MDB_val asked{sizeof key, const_cast<uint64_t *>(&key)};
    MDB_val found{0, nullptr};
    if (mdb_get(held.reading, held.database[shape == Shape::kOwnDatabase ? field : 0], &asked,
                &found) != 0) [[unlikely]]
        std::abort();
    benchmark::DoNotOptimize(found.mv_data);
}

void one_field(benchmark::State &state, const Shape shape)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), shape);
    size_t at = 0;
    uint8_t field = 0;
    for (auto _ : state) {
        const uint64_t of_route = held.route.at(at);
        if (++at == held.route.size())
            at = 0;
        got(held, shape, of_route, field);
        if (++field == kFields)
            field = 0;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void three_fields(benchmark::State &state, const Shape shape)
{
    Filled &held = filled_with(static_cast<size_t>(state.range(0)), shape);
    size_t at = 0;
    for (auto _ : state) {
        const uint64_t of_route = held.route.at(at);
        if (++at == held.route.size())
            at = 0;
        for (uint8_t field = 0; field < kAskedTogether; field++)
            got(held, shape, of_route, field);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void one_field_mixed(benchmark::State &s) { one_field(s, Shape::kMixed); }
void one_field_in_the_top_byte(benchmark::State &s) { one_field(s, Shape::kTopByte); }
void one_field_in_its_own_database(benchmark::State &s) { one_field(s, Shape::kOwnDatabase); }
void one_field_out_of_an_index(benchmark::State &s) { one_field(s, Shape::kIndex); }

void three_fields_mixed(benchmark::State &s) { three_fields(s, Shape::kMixed); }
void three_fields_in_the_top_byte(benchmark::State &s) { three_fields(s, Shape::kTopByte); }
void three_fields_in_their_own_databases(benchmark::State &s)
{
    three_fields(s, Shape::kOwnDatabase);
}
void three_fields_out_of_an_index(benchmark::State &s) { three_fields(s, Shape::kIndex); }

BENCHMARK(one_field_mixed)->Arg(10000)->Arg(100000);
BENCHMARK(one_field_in_the_top_byte)->Arg(10000)->Arg(100000);
BENCHMARK(one_field_in_its_own_database)->Arg(10000)->Arg(100000);
BENCHMARK(one_field_out_of_an_index)->Arg(10000)->Arg(100000);
BENCHMARK(three_fields_mixed)->Arg(10000)->Arg(100000);
BENCHMARK(three_fields_in_the_top_byte)->Arg(10000)->Arg(100000);
BENCHMARK(three_fields_in_their_own_databases)->Arg(10000)->Arg(100000);
BENCHMARK(three_fields_out_of_an_index)->Arg(10000)->Arg(100000);

}
