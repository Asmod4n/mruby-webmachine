#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cache_forgetting cache_forgetting;

cache_forgetting *cache_forgetting_through(int to_the_writer);

cache_forgetting *cache_forgetting_on(const char *app_name, const char *directory);

void cache_forgetting_closed(cache_forgetting *of_app);

bool cache_forget_value(cache_forgetting *of_app, uint64_t route, uint8_t field);

bool cache_forget_route(cache_forgetting *of_app, uint64_t route);

bool cache_forget_everything(cache_forgetting *of_app);

#ifdef __cplusplus
}
#endif
