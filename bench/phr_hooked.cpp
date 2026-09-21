#include "phr_wanted.hpp"

namespace wanted
{
Wanted *sink = nullptr;
}

#define phr_parse_request hooked_phr_parse_request
#define phr_parse_response hooked_phr_parse_response
#define phr_parse_headers hooked_phr_parse_headers
#define phr_decode_chunked hooked_phr_decode_chunked
#define phr_decode_chunked_is_in_data hooked_phr_decode_chunked_is_in_data
#define phr_is_field_name hooked_phr_is_field_name
#define phr_is_lowercase_field_name hooked_phr_is_lowercase_field_name
#define phr_is_field_value hooked_phr_is_field_value

#define PHR_ON_FIELD(name, name_len, value, value_len)                                             \
    wanted::note_field(std::string_view(name, name_len), std::string_view(value, value_len),       \
                       *wanted::sink)

#include "picohttpparser/picohttpparser.c"
