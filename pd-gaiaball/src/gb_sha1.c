#include "gb_sha1.h"
#include <string.h>

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80], a, b, c, d, e, t;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void gb_sha1(const uint8_t *data, size_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t i = 0;
    for (; n - i >= 64; i += 64) block(h, data + i);
    uint8_t tail[128]; size_t r = n - i;
    memset(tail, 0, sizeof tail);
    if (r) memcpy(tail, data + i, r);
    tail[r] = 0x80;
    size_t tl = (r < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8u;
    for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (uint8_t)(bits >> (8 * k));
    block(h, tail);
    if (tl == 128) block(h, tail + 64);
    for (int k = 0; k < 5; k++) {
        out[4 * k] = (uint8_t)(h[k] >> 24); out[4 * k + 1] = (uint8_t)(h[k] >> 16);
        out[4 * k + 2] = (uint8_t)(h[k] >> 8); out[4 * k + 3] = (uint8_t)h[k];
    }
}
