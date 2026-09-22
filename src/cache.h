#ifndef WEBMACHINE_CACHE_H
#define WEBMACHINE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct cache cache;
typedef struct cache_reader cache_reader;

typedef struct {
    const void *value;
    size_t length;
    unsigned snapshot;
} cache_answer;

cache *cache_open(const char *app_name, const char *directory, unsigned readers);

void cache_close(cache *of_app);

cache_reader *cache_reader_opened(cache *of_app);

void cache_reader_closed(cache_reader *of_thread);

cache_answer cache_asked(cache_reader *of_thread, const char *key, size_t key_length);

void cache_sent(cache_reader *of_thread, unsigned snapshot);

bool cache_changed(cache_reader *of_thread);

#endif
