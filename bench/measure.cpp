#include <benchmark/benchmark.h>
#include <dlfcn.h>

#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "arm.hpp"

namespace
{

using Run = size_t (*)(size_t, ...);

struct Loaded {
    std::string name;
    Run run;
    std::vector<std::string> arguments;
};

std::expected<Run, std::string> arm_loaded(const std::filesystem::path &shared_object)
{
    void *const handle = dlopen(shared_object.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) return std::unexpected(dlerror());
    void *const symbol = dlsym(handle, "run");
    if (symbol == nullptr) return std::unexpected(dlerror());
    return reinterpret_cast<Run>(symbol);
}

size_t called(const Loaded &arm)
{
    switch (arm.arguments.size()) {
    case 0:
        return arm.run(0);
    case 1:
        return arm.run(1, arm.arguments.at(0).c_str());
    case 2:
        return arm.run(2, arm.arguments.at(0).c_str(), arm.arguments.at(1).c_str());
    case 3:
        return arm.run(3, arm.arguments.at(0).c_str(), arm.arguments.at(1).c_str(), arm.arguments.at(2).c_str());
    default:
        return arm.run(4, arm.arguments.at(0).c_str(), arm.arguments.at(1).c_str(), arm.arguments.at(2).c_str(),
                       arm.arguments.at(3).c_str());
    }
}

void timed(benchmark::State &state, const Loaded &arm)
{
    for (auto _ : state) benchmark::DoNotOptimize(called(arm));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

std::vector<Loaded> arms_of(const int argc, char **const argv, std::vector<char *> &left_for_benchmark)
{
    std::vector<Loaded> arms;
    left_for_benchmark.push_back(argv[0]);
    for (int at = 1; at < argc; at++) {
        const std::string word = argv[at];
        if (word.starts_with("--benchmark")) {
            left_for_benchmark.push_back(argv[at]);
        } else if (word == "--") {
            continue;
        } else if (word.ends_with(".so")) {
            arms.push_back({std::filesystem::path(word).stem().string(), nullptr, {}});
        } else if (!arms.empty()) {
            arms.back().arguments.push_back(word);
            arms.back().name += ":" + word;
        }
    }
    return arms;
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<char *> left;
    std::vector<Loaded> arms = arms_of(argc, argv, left);
    if (arms.empty()) {
        std::fprintf(stderr, "measure <arm.so> [arguments...] [-- <arm.so> ...] [--benchmark_...]\n");
        return 2;
    }
    for (int at = 1; at < argc; at++) {
        const std::string word = argv[at];
        if (!word.ends_with(".so")) continue;
        const std::expected<Run, std::string> run = arm_loaded(word);
        if (!run) {
            std::fprintf(stderr, "%s: %s\n", argv[at], run.error().c_str());
            return 1;
        }
        for (Loaded &arm : arms)
            if (arm.run == nullptr && arm.name.starts_with(std::filesystem::path(word).stem().string())) {
                arm.run = *run;
                break;
            }
    }
    size_t first_answer = 0;
    for (size_t at = 0; at < arms.size(); at++) {
        const size_t answer = called(arms.at(at));
        if (at == 0) first_answer = answer;
        std::fprintf(stderr, "%s answers %zu\n", arms.at(at).name.c_str(), answer);
    }
    (void)first_answer;
    for (const Loaded &arm : arms)
        benchmark::RegisterBenchmark(arm.name.c_str(), [&arm](benchmark::State &state) { timed(state, arm); });
    int left_count = static_cast<int>(left.size());
    benchmark::Initialize(&left_count, left.data());
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
