#ifndef BT3_UWP_HTTP_H
#define BT3_UWP_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int PortUwp_HttpRequest(const char *url, const char *body,
                        const char *const *headers, char *answer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
