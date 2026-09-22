#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kCacheFieldStatus = 0,
    kCacheFieldEntityTag,
    kCacheFieldLastModified,
    kCacheFieldContentType,
    kCacheFieldContentLanguage,
    kCacheFieldContentEncoding,
    kCacheFieldExpires,
    kCacheFieldVary,
    kCacheFieldLocation,
    kCacheFieldBody,
    kCacheFieldCount,
};

typedef struct cache cache;
typedef struct cache_reader cache_reader;

typedef struct {
    const uint8_t *value;
    size_t length;
    unsigned snapshot;
} cache_answer;

uint64_t cache_key_of(const uint8_t *route, size_t route_length);

uint64_t cache_field_of(uint64_t of_route, uint8_t field);

cache *cache_open(const char *app_name, const char *directory, unsigned readers);

void cache_close(cache *of_app);

cache_reader *cache_reader_opened(cache *of_app);

void cache_reader_closed(cache_reader *of_thread);

cache_answer cache_asked(cache_reader *of_thread, uint64_t key);

void cache_sent(cache_reader *of_thread, unsigned snapshot);

bool cache_changed(cache_reader *of_thread);

#ifdef __cplusplus
}
#endif
