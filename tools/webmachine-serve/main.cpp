#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include <slipstream_syscall.h>

#include <mruby.h>

#include "../../src/cache.h"
#include "../../src/config.hpp"
#include "../../src/ring.hpp"
#include "../../src/serve.hpp"

int
main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: webmachine-serve <port|unix path> [engine] [-c webmachine.toml]\n");
        return 2;
    }
    const bool on_a_path = strchr(argv[1], '/') != nullptr;
    const uint16_t port = (uint16_t) strtoul(argv[1], nullptr, 10);
    std::optional<std::filesystem::path> named_config;
    for (int at = 2; at < argc; at++) {
        if (strcmp(argv[at], "engine") == 0) {
            slipstream_syscall_set_engine(1);
        } else if (strcmp(argv[at], "-c") == 0 && at + 1 < argc) {
            named_config = argv[++at];
        } else {
            fprintf(stderr, "webmachine-serve: %s is no argument this server takes\n", argv[at]);
            return 2;
        }
    }

    serve::Cache c{nullptr, nullptr, -1, {}};
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);

    const std::filesystem::path config_path =
        named_config.value_or(self.parent_path().parent_path() / "etc" / "webmachine" / "webmachine.toml");
    wm::Timeouts timeouts = wm::kTimeoutsByDefault;
    if (named_config || std::filesystem::exists(config_path)) {
        std::ifstream file(config_path);
        if (!file) {
            fprintf(stderr, "webmachine-serve: %s cannot be read\n", config_path.c_str());
            return 1;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        mrb_state *const mrb = mrb_open();
        if (mrb == nullptr) {
            fprintf(stderr, "webmachine-serve: no mruby state to read %s\n", config_path.c_str());
            return 1;
        }
        const auto read = config::timeouts_in(mrb, text);
        mrb_close(mrb);
        if (!read) {
            fprintf(stderr, "webmachine-serve: %s\n", read.error().what());
            return 1;
        }
        timeouts = *read;
    }
    const pid_t writer = failed ? -1
                                : serve::the_writer_stands(self.parent_path() / "webmachine-cache",
                                                           std::filesystem::temp_directory_path(), c);
    if (writer < 0 || c.of_thread == nullptr) fprintf(stderr, "webmachine-serve: no cache, every page is rendered\n");

    wm::Ring ring;
    const int stood = ring.stood_up(4096, timeouts);
    if (stood < 0) {
        fprintf(stderr, "webmachine-serve: stood_up: %s\n", strerror(-stood));
        return 1;
    }
    const int listening = on_a_path ? ring.listens_on(argv[1]) : ring.listens_on(port);
    if (listening < 0) {
        fprintf(stderr, "webmachine-serve: listens_on: %s\n", strerror(-listening));
        return 1;
    }
    printf("%s\n", argv[1]);

    const bool enough = false;
    serve::Today today{};
    try {
        ring.serves(
            [&](const std::string_view asked, const std::span<char> room) -> wm::Answered {
                return serve::answered(asked, room, c, today);
            },
            [&](const std::span<char> room, const uint16_t status) -> wm::Answered {
                return serve::refused(room, today, status, 0);
            },
            [&](const void *const held) {
                serve::released(c, static_cast<cache_held *>(const_cast<void *>(held)));
            },
            [&]() { serve::brought_up_to_date(today); },
            enough);
    } catch (const wm::QueueIsFull &full) {
        fprintf(stderr, "webmachine-serve: %s\n", full.what());
        return 1;
    }
    if (c.of_thread != nullptr) cache_reader_closed(c.of_thread);
    if (c.of_app != nullptr) cache_close(c.of_app);
    if (writer > 0) {
        close(c.to_the_writer);
        waitpid(writer, nullptr, 0);
    }
    return 0;
}
