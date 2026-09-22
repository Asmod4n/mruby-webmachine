#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { kCacheBodyIsInline = 0, kCacheBodyIsInAFile = 1 };

enum { kCacheStores = 0, kCacheForgets = 1 };

typedef struct {
    uint64_t route;
    uint32_t freshness_lifetime;
    uint8_t field;
    uint8_t body;
    uint8_t forget;
    uint8_t unused;
} cache_datagram_header;

enum { kCacheExpired = 0, kCacheInvalidated = 1 };

typedef struct {
    uint64_t route;
    uint8_t field;
    uint8_t why;
    uint8_t unused[6];
} cache_gone_datagram;

#ifdef __cplusplus
static_assert(sizeof(cache_gone_datagram) == 16, "a cache gone datagram is 16 bytes");
static_assert(sizeof(cache_datagram_header) == 16, "a cache datagram header is 16 bytes");
static_assert(alignof(cache_datagram_header) == 8, "a cache datagram header aligns to eight");
#else
_Static_assert(sizeof(cache_gone_datagram) == 16, "a cache gone datagram is 16 bytes");
_Static_assert(sizeof(cache_datagram_header) == 16, "a cache datagram header is 16 bytes");
_Static_assert(__alignof__(cache_datagram_header) == 8,
               "a cache datagram header aligns to eight");
#endif

#ifdef __cplusplus
}
#endif
