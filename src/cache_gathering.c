#include "cache_gathering.h"

#include <string.h>

uint32_t cache_gathering_reserves(const cache_datagram_header header, const size_t payload_length)
{
    if (header.at != 0)
        return 0;
    if (header.key_length == 0 || payload_length < header.key_length)
        return 0;
    if (payload_length - header.key_length > header.message_length)
        return 0;
    if ((uint64_t) header.key_length + header.message_length > UINT32_MAX)
        return 0;
    return header.key_length + header.message_length;
}

bool cache_gathering_began(cache_gathering *const of_connection,
                           const cache_datagram_header header, const uint8_t *const payload,
                           const size_t payload_length, uint8_t *const into)
{
    if (of_connection == NULL || payload == NULL || into == NULL)
        return false;
    if (cache_gathering_reserves(header, payload_length) == 0)
        return false;
    memcpy(into, payload, payload_length);
    of_connection->group = header.group;
    of_connection->key = header.key;
    of_connection->expecting = 1;
    of_connection->message_length = header.message_length;
    of_connection->gathered = (uint32_t) (payload_length - header.key_length);
    of_connection->key_length = header.key_length;
    of_connection->freshness_lifetime = header.freshness_lifetime;
    of_connection->into = into;
    return true;
}

bool cache_gathering_took(cache_gathering *const of_connection,
                          const cache_datagram_header header, const uint8_t *const payload,
                          const size_t payload_length)
{
    if (of_connection == NULL || of_connection->into == NULL || payload == NULL)
        return false;
    if (header.group != of_connection->group || header.key != of_connection->key)
        return false;
    if (header.at != of_connection->expecting)
        return false;
    if (header.message_length != of_connection->message_length)
        return false;
    if (payload_length == 0)
        return false;
    if (payload_length > of_connection->message_length - of_connection->gathered)
        return false;
    memcpy(of_connection->into + of_connection->key_length + of_connection->gathered, payload,
           payload_length);
    of_connection->gathered += (uint32_t) payload_length;
    of_connection->expecting++;
    return true;
}

bool cache_gathering_whole(const cache_gathering *const of_connection)
{
    return of_connection != NULL && of_connection->into != NULL &&
           of_connection->gathered == of_connection->message_length;
}

void cache_gathering_dropped(cache_gathering *const of_connection)
{
    if (of_connection == NULL)
        return;
    memset(of_connection, 0, sizeof *of_connection);
}
