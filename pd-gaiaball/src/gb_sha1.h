/* Small public-domain-style SHA-1 (FIPS 180-4). Used only for the WebSocket accept key. */
#ifndef GB_SHA1_H
#define GB_SHA1_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void gb_sha1(const uint8_t *data, size_t n, uint8_t out[20]);
#ifdef __cplusplus
}
#endif
#endif
