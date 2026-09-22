#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <slipstream_syscall.h>

#include "../../src/head.hpp"
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
        [&](const uint8_t *const asked, const size_t asked_length, uint8_t *const into,
            const size_t room) -> size_t {
            if (asked_length >= 4 && memcmp(asked, "STOP", 4) == 0)
                enough = true;
            std::string_view left(reinterpret_cast<const char *>(asked), asked_length);
            size_t written = 0;
            for (;;) {
                wm::Head head;
                const wm::Reading read = wm::head_of(left, head);
                if (read == wm::Reading::kWantsMore)
                    break;
                if (read == wm::Reading::kRefused) {
                    if (room - written < sizeof kRefused - 1)
                        break;
                    memcpy(into + written, kRefused, sizeof kRefused - 1);
                    written += sizeof kRefused - 1;
                    break;
                }
                if (room - written < sizeof kAnswer - 1)
                    break;
                memcpy(into + written, kAnswer, sizeof kAnswer - 1);
                written += sizeof kAnswer - 1;
                left.remove_prefix(head.bytes);
                if (left.empty())
                    break;
            }
            return written;
        },
            enough);
    } catch (const wm::QueueIsFull &full) {
        fprintf(stderr, "webmachine-serve: %s\n", full.what());
        return 1;
    }
    return 0;
}
