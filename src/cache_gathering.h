#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cache_datagram.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t group;
    uint64_t key;
    uint32_t expecting;
    uint32_t message_length;
    uint32_t gathered;
    uint32_t key_length;
    uint32_t freshness_lifetime;
    uint8_t *into;
} cache_gathering;

uint32_t cache_gathering_reserves(cache_datagram_header header, size_t payload_length);

bool cache_gathering_began(cache_gathering *of_connection, cache_datagram_header header,
                           const uint8_t *payload, size_t payload_length, uint8_t *into);

bool cache_gathering_took(cache_gathering *of_connection, cache_datagram_header header,
                          const uint8_t *payload, size_t payload_length);

bool cache_gathering_whole(const cache_gathering *of_connection);

void cache_gathering_dropped(cache_gathering *of_connection);

#ifdef __cplusplus
}
#endif
