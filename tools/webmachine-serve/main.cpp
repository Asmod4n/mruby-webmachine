#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include <slipstream_syscall.h>

#include "../../src/cache.h"
#include "../../src/ring.hpp"
#include "../../src/serve.hpp"

int
main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: webmachine-serve <port|unix path> [engine]\n");
        return 2;
    }
    const bool on_a_path = strchr(argv[1], '/') != nullptr;
    const uint16_t port = (uint16_t) strtoul(argv[1], nullptr, 10);
    if (argc > 2 && strcmp(argv[2], "engine") == 0) slipstream_syscall_set_engine(1);

    serve::Cache c{nullptr, nullptr, -1, {}};
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    const pid_t writer = failed ? -1
                                : serve::the_writer_stands(self.parent_path() / "webmachine-cache",
                                                           std::filesystem::temp_directory_path(), c);
    if (writer < 0 || c.of_thread == nullptr) fprintf(stderr, "webmachine-serve: no cache, every page is rendered\n");

    wm::Ring ring;
    const int stood = ring.stood_up(4096);
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

    bool enough = false;
    serve::Today today{};
    try {
        ring.serves(
            [&](const std::string_view asked, const std::span<char> room) -> wm::Answered {
                serve::brought_up_to_date(today);
                if (asked.starts_with("STOP")) enough = true;
                return serve::answered(asked, room, c, today);
            },
            [&](const void *const held) {
                cache_sent(c.of_thread, static_cast<cache_held *>(const_cast<void *>(held)));
            },
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
