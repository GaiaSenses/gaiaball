/* Small base64 encoder (RFC 4648, with padding). */
#ifndef GB_BASE64_H
#define GB_BASE64_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Writes a NUL-terminated string; needs cap >= 4*ceil(n/3)+1. Returns the string
 * length, or 0 if cap is too small (out is then left untouched). n == 0 gives "" (length 0, cap >= 1). */
size_t gb_base64_encode(const uint8_t *data, size_t n, char *out, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
