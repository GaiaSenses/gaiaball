#include "gb_base64.h"

static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t gb_base64_encode(const uint8_t *d, size_t n, char *out, size_t cap) {
    size_t need = 4 * ((n + 2) / 3) + 1;
    if (cap < need) return 0;
    size_t o = 0, i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8) | d[i + 2];
        out[o++] = T[v >> 18]; out[o++] = T[(v >> 12) & 63]; out[o++] = T[(v >> 6) & 63]; out[o++] = T[v & 63];
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)d[i] << 16;
        out[o++] = T[v >> 18]; out[o++] = T[(v >> 12) & 63]; out[o++] = '='; out[o++] = '=';
    } else if (n - i == 2) {
        uint32_t v = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8);
        out[o++] = T[v >> 18]; out[o++] = T[(v >> 12) & 63]; out[o++] = T[(v >> 6) & 63]; out[o++] = '=';
    }
    out[o] = 0;
    return o;
}
