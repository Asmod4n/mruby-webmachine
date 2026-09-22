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
} cache_packet;

static_assert(sizeof(cache_packet) == 32, "a cache packet is 32 bytes");
static_assert(_Alignof(cache_packet) == 8, "a cache packet aligns to eight");
