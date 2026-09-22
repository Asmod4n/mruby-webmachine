#pragma once

#include <assert.h>
#include <stdint.h>

typedef struct {
    uint64_t group;
    uint64_t key;
    uint32_t message;
    uint32_t packet;
    uint32_t freshness_lifetime;
    uint32_t key_length;
} cache_packet_header;

static_assert(sizeof(cache_packet_header) == 32, "a cache packet header is 32 bytes");
static_assert(_Alignof(cache_packet_header) == 8, "a cache packet header aligns to eight");
