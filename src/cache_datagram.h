#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t group;
    uint64_t key;
    uint32_t message_length;
    uint32_t at;
    uint32_t freshness_lifetime;
    uint32_t key_length;
} cache_datagram_header;

#ifdef __cplusplus
static_assert(sizeof(cache_datagram_header) == 32, "a cache datagram header is 32 bytes");
static_assert(alignof(cache_datagram_header) == 8, "a cache datagram header aligns to eight");
#else
_Static_assert(sizeof(cache_datagram_header) == 32, "a cache datagram header is 32 bytes");
_Static_assert(__alignof__(cache_datagram_header) == 8,
               "a cache datagram header aligns to eight");
#endif

#ifdef __cplusplus
}
#endif
