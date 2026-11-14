#include "gb_ws.h"
#include "gb_base64.h"
#include "gb_sha1.h"
#include <stdio.h>
#include <string.h>

static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int gb_ws_accept_key(const char *client_key, char out[29]) {
    size_t kl = client_key ? strlen(client_key) : 0;
    if (kl == 0 || kl > 63) return -1;
    uint8_t tmp[64 + sizeof GUID], dig[20];
    memcpy(tmp, client_key, kl);
    memcpy(tmp + kl, GUID, sizeof GUID - 1);
    gb_sha1(tmp, kl + sizeof GUID - 1, dig);
    return gb_base64_encode(dig, 20, out, 29) == 28 ? 0 : -1;
}

/* ---- handshake ---- */
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int ieq(const char *a, size_t an, const char *lit) { /* case-insensitive, lit lowercase */
    size_t i = 0;
    for (; i < an && lit[i]; i++) if (lc((unsigned char)a[i]) != lit[i]) return 0;
    return i == an && lit[i] == 0;
}
/* Does the comma-separated list contain the (lowercase) token? */
static int has_token(const char *v, size_t n, const char *tok) {
    size_t i = 0;
    while (i < n) {
        size_t s = i;
        while (i < n && v[i] != ',') i++;
        size_t e = i;
        while (s < e && (v[s] == ' ' || v[s] == '\t')) s++;
        while (e > s && (v[e - 1] == ' ' || v[e - 1] == '\t')) e--;
        if (ieq(v + s, e - s, tok)) return 1;
        i++;
    }
    return 0;
}

gb_hs_result gb_ws_parse_request(const char *buf, size_t n, char key[64]) {
    size_t end = 0; int found = 0;
    for (size_t i = 0; i + 4 <= n; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { end = i; found = 1; break; }
    if (!found) return n > GB_WS_MAX_REQUEST ? GB_HS_BAD : GB_HS_NEED_MORE;
    if (end + 4 > GB_WS_MAX_REQUEST) return GB_HS_BAD;

    size_t i = 0;
    while (i < end && buf[i] != '\r') i++;
    if (i < 14 || memcmp(buf, "GET ", 4) != 0 || memcmp(buf + i - 9, " HTTP/1.1", 9) != 0) return GB_HS_BAD;

    int upgrade = 0, connection = 0, version = 0, have_key = 0;
    key[0] = 0;
    while (i < end) {
        i += 2; /* past CRLF */
        size_t ls = i;
        while (i < end + 2 && !(buf[i] == '\r' && i + 1 < n && buf[i + 1] == '\n')) i++;
        size_t le = i; /* line is [ls, le) */
        if (le <= ls) continue;
        size_t c = ls;
        while (c < le && buf[c] != ':') c++;
        if (c == le) return GB_HS_BAD;
        size_t vs = c + 1, ve = le;
        while (vs < ve && (buf[vs] == ' ' || buf[vs] == '\t')) vs++;
        while (ve > vs && (buf[ve - 1] == ' ' || buf[ve - 1] == '\t')) ve--;
        size_t nl = c - ls; const char *nm = buf + ls; const char *v = buf + vs; size_t vl = ve - vs;
        if (ieq(nm, nl, "upgrade")) { if (has_token(v, vl, "websocket")) upgrade = 1; }
        else if (ieq(nm, nl, "connection")) { if (has_token(v, vl, "upgrade")) connection = 1; }
        else if (ieq(nm, nl, "sec-websocket-version")) { if (vl == 2 && v[0] == '1' && v[1] == '3') version = 1; }
        else if (ieq(nm, nl, "sec-websocket-key")) {
            if (vl == 0 || vl > 63) return GB_HS_BAD;
            memcpy(key, v, vl); key[vl] = 0; have_key = 1;
        }
    }
    return (upgrade && connection && version && have_key) ? GB_HS_OK : GB_HS_BAD;
}

size_t gb_ws_response_101(const char accept[29], char *out, size_t cap) {
    int r = snprintf(out, cap, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                               "Connection: Upgrade\r\nSec-WebSocket-Accept: %.28s\r\n\r\n", accept);
    return (r > 0 && (size_t)r < cap) ? (size_t)r : 0;
}
size_t gb_ws_response_400(char *out, size_t cap) {
    int r = snprintf(out, cap, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    return (r > 0 && (size_t)r < cap) ? (size_t)r : 0;
}

/* ---- frames ---- */
void gb_ws_feed(gb_ws_parser *p, const uint8_t *data, size_t n) {
    if (p->err || n == 0) return;
    if (p->len > sizeof p->buf || n > sizeof p->buf - p->len) { p->err = 1009; return; }
    memcpy(p->buf + p->len, data, n);
    p->len += n;
}

static gb_ws_kind fail(gb_ws_parser *p, uint16_t code, uint16_t *out) { p->err = code; *out = code; return GB_WS_ERROR; }

gb_ws_kind gb_ws_next(gb_ws_parser *p, uint8_t *payload, size_t *len, uint16_t *close_code) {
    *len = 0; *close_code = 0;
    for (;;) {
        if (p->err) { *close_code = p->err; return GB_WS_ERROR; }
        if (p->len > sizeof p->buf) return fail(p, 1009, close_code);
        if (p->len < 2) return GB_WS_NONE;
        const uint8_t *b = p->buf;
        int fin = b[0] & 0x80, opcode = b[0] & 0x0F, masked = b[1] & 0x80;
        uint64_t plen = b[1] & 0x7F;
        size_t hdr = 2;
        if (b[0] & 0x70) return fail(p, 1002, close_code);
        if (!masked) return fail(p, 1002, close_code);
        int control = opcode >= 8;
        if (opcode == 1 || opcode == 0) return fail(p, 1003, close_code);
        if (opcode != 2 && opcode != 8 && opcode != 9 && opcode != 0xA) return fail(p, 1002, close_code);
        if (!fin) return fail(p, control ? 1002 : 1003, close_code);
        if (plen == 126) {
            if (p->len < 4) return GB_WS_NONE;
            plen = ((uint64_t)b[2] << 8) | b[3]; hdr = 4;
        } else if (plen == 127) {
            if (p->len < 10) return GB_WS_NONE;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | b[2 + i];
            hdr = 10;
        }
        if (control && plen > 125) return fail(p, 1002, close_code);
        if (plen > GB_WS_MAX_PAYLOAD) return fail(p, 1009, close_code);
        size_t total = hdr + 4 + (size_t)plen;
        if (p->len < total) {
            if (p->len == sizeof p->buf) return fail(p, 1009, close_code); /* full, still incomplete */
            return GB_WS_NONE;
        }
        const uint8_t *mk = b + hdr, *src = b + hdr + 4;
        for (size_t i = 0; i < (size_t)plen; i++) payload[i] = (uint8_t)(src[i] ^ mk[i & 3]);
        *len = (size_t)plen;
        gb_ws_kind kind = GB_WS_BINARY;
        uint16_t cc = 0;
        if (opcode == 9) kind = GB_WS_PING;
        else if (opcode == 8) {
            if (plen == 1) return fail(p, 1002, close_code);
            cc = plen >= 2 ? (uint16_t)((payload[0] << 8) | payload[1]) : 1005;
            kind = GB_WS_CLOSE;
        }
        memmove(p->buf, p->buf + total, p->len - total);
        p->len -= total;
        if (opcode == 0xA) { *len = 0; continue; } /* pong: ignore */
        *close_code = cc;
        return kind;
    }
}

size_t gb_ws_frame(uint8_t opcode, const uint8_t *payload, size_t n, uint8_t *out, size_t cap) {
    size_t hdr = n < 126 ? 2 : (n <= 0xFFFF ? 4 : 10);
    if (n > SIZE_MAX - hdr || cap < hdr + n) return 0;
    out[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (hdr == 2) out[1] = (uint8_t)n;
    else if (hdr == 4) { out[1] = 126; out[2] = (uint8_t)(n >> 8); out[3] = (uint8_t)n; }
    else {
        out[1] = 127;
        for (int i = 0; i < 8; i++) out[2 + i] = (uint8_t)((uint64_t)n >> (8 * (7 - i)));
    }
    if (n) memcpy(out + hdr, payload, n);
    return hdr + n;
}
