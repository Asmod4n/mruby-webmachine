#ifndef WEBMACHINE_CACHE_H
#define WEBMACHINE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct cache cache;

typedef struct {
    const void *value;
    size_t length;
} cache_answer;

cache *cache_open(const char *app_name, const char *directory);

void cache_close(cache *of_app);

bool cache_reading_began(cache *of_app);

cache_answer cache_asked(cache *of_app, const char *key, size_t key_length);

void cache_reading_ended(cache *of_app);

#endif
