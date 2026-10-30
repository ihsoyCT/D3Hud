/* Compiles the host API header as C: the header must stay plain C (no C++ in it) so that any
 * binding can sit on it. Called from test_core.cpp. */
#include "HostApi.h"

typedef char d3h_vec2_is_8_bytes[sizeof(d3h_vec2) == 8 ? 1 : -1];
typedef char d3h_rect_is_16_bytes[sizeof(d3h_rect) == 16 ? 1 : -1];

int HostApiCCheck(void) {
    d3h_host host = {0};
    host.size = sizeof(host);
    host.api_version = D3H_API_VERSION;
    return D3H_HOST_HAS(&host, set_salvage_policy) ? (int)host.api_version : -1;
}
