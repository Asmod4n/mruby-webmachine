#pragma once

#include <assert.h>
#include <stdint.h>

typedef struct {
    uint64_t group;
    uint64_t key;
    uint32_t message_length;
    uint32_t at;
    uint32_t freshness_lifetime;
    uint32_t key_length;
} cache_datagram_header;

static_assert(sizeof(cache_datagram_header) == 32, "a cache datagram header is 32 bytes");
static_assert(_Alignof(cache_datagram_header) == 8, "a cache datagram header aligns to eight");
