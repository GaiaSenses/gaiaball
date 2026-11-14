#include "gb_base64.h"
#include "gb_sha1.h"
#include "gb_ws.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- SHA-1 / base64 ---- */
static void hex(const uint8_t *d, size_t n, char *out) {
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = H[d[i] >> 4]; out[2 * i + 1] = H[d[i] & 15]; }
    out[2 * n] = 0;
}
static void sha_is(const char *msg, size_t n, const char *want) {
    uint8_t d[20]; char h[41];
    gb_sha1((const uint8_t *)msg, n, d);
    hex(d, 20, h);
    if (strcmp(h, want) != 0) { printf("FAIL sha1 got %s want %s\n", h, want); fails++; }
}
static void test_sha1(void) {
    sha_is("abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha_is("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
           "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    static char big[1000000]; memset(big, 'a', sizeof big);
    sha_is(big, sizeof big, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}
static void b64_is(const char *in, const char *want) {
    char o[32];
    size_t n = gb_base64_encode((const uint8_t *)in, strlen(in), o, sizeof o);
    CHECK(n == strlen(want));
    CHECK(strcmp(o, want) == 0);
}
static void test_base64(void) {
    b64_is("", ""); b64_is("f", "Zg=="); b64_is("fo", "Zm8="); b64_is("foo", "Zm9v");
    b64_is("foob", "Zm9vYg=="); b64_is("fooba", "Zm9vYmE="); b64_is("foobar", "Zm9vYmFy");
    char small[4];
    CHECK(gb_base64_encode((const uint8_t *)"foo", 3, small, sizeof small) == 0); /* needs 5 */
}

/* ---- handshake ---- */
static void test_accept(void) {
    char out[29];
    CHECK(gb_ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", out) == 0);
    CHECK(strcmp(out, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
    CHECK(gb_ws_accept_key("", out) != 0);
}
#define REQ_HEAD "GET /chat HTTP/1.1\r\nHost: 127.0.0.1:9478\r\n"
#define REQ_UPG "Upgrade: websocket\r\nConnection: Upgrade\r\n"
#define REQ_KEY "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
#define REQ_VER "Sec-WebSocket-Version: 13\r\n"
static gb_hs_result hs(const char *s, char *key) { return gb_ws_parse_request(s, strlen(s), key); }
static void test_request(void) {
    char key[64];
    CHECK(hs(REQ_HEAD REQ_UPG REQ_KEY REQ_VER "\r\n", key) == GB_HS_OK);
    CHECK(strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);
    CHECK(hs(REQ_HEAD REQ_UPG REQ_KEY REQ_VER "Origin: https://qualquer.exemplo\r\n\r\n", key) == GB_HS_OK);
    CHECK(hs(REQ_HEAD "Origin: null\r\n" REQ_UPG REQ_KEY REQ_VER "\r\n", key) == GB_HS_OK);
    CHECK(hs("GET / HTTP/1.1\r\nupgrade: WebSocket\r\nCONNECTION: keep-alive, upgrade\r\n"
             "sec-websocket-key:   abc \r\nsec-websocket-version:13\r\n\r\n", key) == GB_HS_OK);
    CHECK(strcmp(key, "abc") == 0);
    CHECK(hs(REQ_HEAD "Connection: Upgrade\r\n" REQ_KEY REQ_VER "\r\n", key) == GB_HS_BAD);      /* no Upgrade */
    CHECK(hs(REQ_HEAD "Upgrade: websocket\r\n" REQ_KEY REQ_VER "\r\n", key) == GB_HS_BAD);       /* no Connection */
    CHECK(hs(REQ_HEAD "Upgrade: h2c\r\nConnection: Upgrade\r\n" REQ_KEY REQ_VER "\r\n", key) == GB_HS_BAD);
    CHECK(hs(REQ_HEAD REQ_UPG REQ_KEY "Sec-WebSocket-Version: 8\r\n\r\n", key) == GB_HS_BAD);
    CHECK(hs(REQ_HEAD REQ_UPG REQ_KEY "\r\n", key) == GB_HS_BAD);                                /* no version */
    CHECK(hs(REQ_HEAD REQ_UPG REQ_VER "\r\n", key) == GB_HS_BAD);                                /* no key */
    CHECK(hs(REQ_HEAD REQ_UPG "Sec-WebSocket-Key: \r\n" REQ_VER "\r\n", key) == GB_HS_BAD);      /* empty key */
    CHECK(hs("POST / HTTP/1.1\r\n" REQ_UPG REQ_KEY REQ_VER "\r\n", key) == GB_HS_BAD);
    CHECK(hs("garbage\r\n\r\n", key) == GB_HS_BAD);
    /* key longer than the output buffer */
    char longreq[512];
    snprintf(longreq, sizeof longreq, REQ_HEAD REQ_UPG REQ_VER "Sec-WebSocket-Key: %0100d\r\n\r\n", 0);
    CHECK(hs(longreq, key) == GB_HS_BAD);
    /* truncated at every point: NEED_MORE, never OK */
    const char *full = REQ_HEAD REQ_UPG REQ_KEY REQ_VER "\r\n";
    size_t fl = strlen(full);
    for (size_t i = 0; i < fl; i++) CHECK(gb_ws_parse_request(full, i, key) == GB_HS_NEED_MORE);
    CHECK(gb_ws_parse_request(full, fl, key) == GB_HS_OK);
    /* oversized headers without terminator or with a far terminator */
    static char big[8192]; memset(big, 'a', sizeof big);
    memcpy(big, "GET / HTTP/1.1\r\nX: ", 19);
    CHECK(gb_ws_parse_request(big, 4000, key) == GB_HS_NEED_MORE);
    CHECK(gb_ws_parse_request(big, 5000, key) == GB_HS_BAD);
    memcpy(big + 6000, "\r\n\r\n", 4);
    CHECK(gb_ws_parse_request(big, 6004, key) == GB_HS_BAD);
}
static void test_responses(void) {
    char out[256], acc[29];
    gb_ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", acc);
    size_t n = gb_ws_response_101(acc, out, sizeof out);
    CHECK(n > 0 && n == strlen(out));
    CHECK(strncmp(out, "HTTP/1.1 101 ", 13) == 0);
    CHECK(strstr(out, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != NULL);
    CHECK(strstr(out, "Upgrade: websocket\r\n") != NULL);
    CHECK(n >= 4 && strcmp(out + n - 4, "\r\n\r\n") == 0);
    CHECK(gb_ws_response_101(acc, out, 20) == 0);
    n = gb_ws_response_400(out, sizeof out);
    CHECK(n > 0 && strncmp(out, "HTTP/1.1 400 ", 13) == 0 && strcmp(out + n - 4, "\r\n\r\n") == 0);
    CHECK(gb_ws_response_400(out, 10) == 0);
}

/* ---- frames ---- */
static const uint8_t MASK[4] = {0x37, 0xFA, 0x21, 0x3D};
/* Builds a masked client frame; payload bytes are real, len field may be forced. */
static size_t cframe(uint8_t b0, const uint8_t *pl, size_t n, int masked, uint8_t *out) {
    size_t i = 0;
    out[i++] = b0;
    uint8_t m = masked ? 0x80 : 0;
    if (n < 126) out[i++] = m | (uint8_t)n;
    else { out[i++] = m | 126; out[i++] = (uint8_t)(n >> 8); out[i++] = (uint8_t)n; }
    if (masked) { memcpy(out + i, MASK, 4); i += 4; }
    for (size_t k = 0; k < n; k++) out[i + k] = masked ? (uint8_t)(pl[k] ^ MASK[k & 3]) : pl[k];
    return i + n;
}
static uint8_t pay[1100]; static size_t plen; static uint16_t code;
static gb_ws_kind nxt(gb_ws_parser *p) { plen = 0; code = 0; return gb_ws_next(p, pay, &plen, &code); }
static void init(gb_ws_parser *p) { memset(p, 0, sizeof *p); }

static void test_frames(void) {
    uint8_t data[27], f[2200]; gb_ws_parser p;
    for (int i = 0; i < 27; i++) data[i] = (uint8_t)(i * 9 + 1);
    size_t fn = cframe(0x82, data, 27, 1, f);

    init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_BINARY); CHECK(plen == 27); CHECK(memcmp(pay, data, 27) == 0);
    CHECK(nxt(&p) == GB_WS_NONE);

    /* two frames in one feed */
    uint8_t two[400]; size_t tn = fn;
    memcpy(two, f, fn);
    uint8_t d2[5] = {1, 2, 3, 4, 5};
    tn += cframe(0x82, d2, 5, 1, two + fn);
    init(&p); gb_ws_feed(&p, two, tn);
    CHECK(nxt(&p) == GB_WS_BINARY && plen == 27 && memcmp(pay, data, 27) == 0);
    CHECK(nxt(&p) == GB_WS_BINARY && plen == 5 && memcmp(pay, d2, 5) == 0);
    CHECK(nxt(&p) == GB_WS_NONE);

    /* one frame split in three */
    init(&p);
    gb_ws_feed(&p, f, 1);           CHECK(nxt(&p) == GB_WS_NONE);
    gb_ws_feed(&p, f + 1, 10);      CHECK(nxt(&p) == GB_WS_NONE);
    gb_ws_feed(&p, f + 11, fn - 11);
    CHECK(nxt(&p) == GB_WS_BINARY && plen == 27 && memcmp(pay, data, 27) == 0);

    /* byte-at-a-time, both frames */
    init(&p); int got = 0;
    for (size_t i = 0; i < tn; i++) { gb_ws_feed(&p, two + i, 1); if (nxt(&p) == GB_WS_BINARY) got++; }
    CHECK(got == 2);

    /* unmasked -> 1002 */
    fn = cframe(0x82, data, 27, 0, f);
    init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002); /* sticky */

    /* text -> 1003 ; continuation -> 1003 ; FIN=0 -> 1003 */
    fn = cframe(0x81, data, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1003);
    fn = cframe(0x80, data, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1003);
    fn = cframe(0x02, data, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1003);

    /* RSV bits / reserved opcode -> 1002 */
    fn = cframe(0xC2, data, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002);
    fn = cframe(0x83, data, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002);

    /* 2000 B -> 1009 (detected from the header alone) */
    static uint8_t big[2000], bf[2100]; memset(big, 7, sizeof big);
    fn = cframe(0x82, big, 2000, 1, bf); init(&p);
    gb_ws_feed(&p, bf, 8);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1009);
    /* 1024 is accepted, 1025 is not */
    fn = cframe(0x82, big, 1024, 1, bf); init(&p); gb_ws_feed(&p, bf, fn);
    CHECK(nxt(&p) == GB_WS_BINARY && plen == 1024 && memcmp(pay, big, 1024) == 0);
    fn = cframe(0x82, big, 1025, 1, bf); init(&p); gb_ws_feed(&p, bf, 8);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1009);
    /* 64-bit length with huge value */
    uint8_t huge[14] = {0x82, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    init(&p); gb_ws_feed(&p, huge, sizeof huge);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1009);

    /* ping -> PING with payload */
    fn = cframe(0x89, d2, 5, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_PING && plen == 5 && memcmp(pay, d2, 5) == 0);
    /* pong is swallowed; frame after it still parsed */
    uint8_t pp[100]; size_t pn = cframe(0x8A, d2, 5, 1, pp);
    pn += cframe(0x82, d2, 2, 1, pp + pn);
    init(&p); gb_ws_feed(&p, pp, pn);
    CHECK(nxt(&p) == GB_WS_BINARY && plen == 2);
    /* oversize control frame -> 1002 */
    fn = cframe(0x89, big, 126, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002);

    /* close with code */
    uint8_t cl[2] = {0x03, 0xE8};
    fn = cframe(0x88, cl, 2, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_CLOSE && code == 1000);
    fn = cframe(0x88, cl, 0, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_CLOSE && code == 1005);
    fn = cframe(0x88, cl, 1, 1, f); init(&p); gb_ws_feed(&p, f, fn);
    CHECK(nxt(&p) == GB_WS_ERROR && code == 1002);

    /* feed that overflows the buffer is an error, not a write past the end */
    init(&p);
    static uint8_t junk[4000]; memset(junk, 0, sizeof junk);
    gb_ws_feed(&p, junk, sizeof junk);
    CHECK(p.len <= sizeof p.buf);
    CHECK(nxt(&p) == GB_WS_ERROR);
}

static void test_stream_chunks(void) {
    static uint8_t big[1024], one[1100], stream[1100 * 20];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 7);
    size_t on = cframe(0x82, big, 1024, 1, one), tot = 0;
    for (int k = 0; k < 20; k++) { memcpy(stream + tot, one, on); tot += on; }
    gb_ws_parser p; init(&p);
    CHECK(gb_ws_space(&p) == sizeof p.buf);
    int got = 0, bad = 0;
    for (size_t pos = 0; pos < tot; ) {
        size_t c = tot - pos; if (c > 1024) c = 1024;
        CHECK(c <= gb_ws_space(&p));
        gb_ws_feed(&p, stream + pos, c); pos += c;
        for (;;) {
            gb_ws_kind k = nxt(&p);
            if (k == GB_WS_NONE) break;
            if (k != GB_WS_BINARY || plen != 1024 || memcmp(pay, big, 1024) != 0) { bad++; break; }
            got++;
        }
    }
    CHECK(got == 20); CHECK(bad == 0);
}

static void test_server_frame(void) {
    uint8_t out[2200], d[300]; memset(d, 0x5A, sizeof d);
    size_t n = gb_ws_frame(0x2, d, 5, out, sizeof out);
    CHECK(n == 7 && out[0] == 0x82 && out[1] == 5 && memcmp(out + 2, d, 5) == 0);
    n = gb_ws_frame(0x8, d, 2, out, sizeof out);
    CHECK(n == 4 && out[0] == 0x88 && out[1] == 2);
    n = gb_ws_frame(0xA, d, 0, out, sizeof out);
    CHECK(n == 2 && out[0] == 0x8A && out[1] == 0);
    n = gb_ws_frame(0x2, d, 300, out, sizeof out);
    CHECK(n == 304 && out[1] == 126 && out[2] == 1 && out[3] == 44);
    CHECK(gb_ws_frame(0x2, d, 300, out, 100) == 0);
    CHECK(gb_ws_frame(0x2, d, 5, out, 6) == 0);
    CHECK(gb_ws_frame(0x2, d, 5, out, 7) == 7);
}

/* ---- fuzz ---- */
static uint32_t rng_state = 0x1234ABCDu;
static uint32_t rnd(void) { /* xorshift32 */
    uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x;
}
static void test_fuzz(void) {
    uint8_t in[3000] = {0}, seed_frame[64]; size_t seed_n = cframe(0x82, in, 20, 1, seed_frame);
    int frames = 0, errors = 0;
    for (int iter = 0; iter < 10000; iter++) {
        gb_ws_parser p; init(&p);
        size_t total = rnd() % 2500;
        for (size_t i = 0; i < total; i++) in[i] = (uint8_t)rnd();
        /* half the inputs start with plausible frame structure to get past the header checks */
        if ((iter & 1) && total > seed_n) { memcpy(in, seed_frame, seed_n); if (iter & 2) in[1] = (uint8_t)(0x80 | (rnd() % 127)); }
        char key[64];
        gb_ws_parse_request((const char *)in, total, key);          /* must not crash */
        size_t pos = 0; int dead = 0;
        while (pos < total && !dead) {
            size_t c = 1 + rnd() % 300; if (c > total - pos) c = total - pos;
            gb_ws_feed(&p, in + pos, c); pos += c;
            for (;;) {
                size_t l = 0; uint16_t cc = 0; uint8_t out[GB_WS_MAX_PAYLOAD];
                gb_ws_kind k = gb_ws_next(&p, out, &l, &cc);
                CHECK(p.len <= sizeof p.buf);
                if (k == GB_WS_NONE) break;
                if (k == GB_WS_ERROR) { errors++; dead = 1; break; }
                CHECK(l <= GB_WS_MAX_PAYLOAD);
                frames++;
                if (k == GB_WS_CLOSE) { dead = 1; break; }
            }
        }
    }
    printf("fuzz: %d frames, %d errors over 10000 inputs\n", frames, errors);
    CHECK(frames > 0 && errors > 0);
}

int main(void) {
    test_sha1(); test_base64(); test_accept(); test_request(); test_responses();
    test_frames(); test_stream_chunks(); test_server_frame(); test_fuzz();
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("test_ws: all passed\n");
    return 0;
}
