#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <slipstream_syscall.h>

#include "../../src/http1.hpp"
#include "../../src/ring.hpp"

static const char kAnswer[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
static const char kRefused[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: webmachine-serve <port|unix path> [engine]\n");
        return 2;
    }
    const bool on_a_path = strchr(argv[1], '/') != nullptr;
    const uint16_t port = (uint16_t) strtoul(argv[1], nullptr, 10);
    if (argc > 2 && strcmp(argv[2], "engine") == 0)
        slipstream_syscall_set_engine(1);

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
    printf("%s\n", on_a_path ? argv[1] : argv[1]);
    fprintf(stderr, "webmachine-serve: io through %s\n",
            slipstream_syscall_uses_engine() ? "the engine" : "the kernel");

    bool enough = false;
    try {
        ring.serves(
        [&](const std::string_view asked) -> wm::Answered {
            if (asked.starts_with("STOP"))
                enough = true;
            const size_t bytes = http1::bytes_before_the_body(asked);
            if (bytes == 0)
                return {{}, 0};
            const std::expected<http1::Request, http::Refusal> request =
                http1::parse_request(asked);
            if (!request) [[unlikely]]
                return {std::string_view(kRefused, sizeof kRefused - 1), bytes};
            return {std::string_view(kAnswer, sizeof kAnswer - 1), request->bytes};
        },
            enough);
    } catch (const wm::QueueIsFull &full) {
        fprintf(stderr, "webmachine-serve: %s\n", full.what());
        return 1;
    }
    return 0;
}
