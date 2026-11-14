/* Transport-independent RFC 6455 server core for [gaiaball]: handshake parsing,
 * client-frame parsing, server-frame building. No sockets, no Pd headers.
 * No Origin check by design (the project's final domain is not known); the
 * protection is binding to 127.0.0.1 only. */
#ifndef GB_WS_H
#define GB_WS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GB_WS_MAX_PAYLOAD 1024
#define GB_WS_MAX_REQUEST 4096 /* request headers larger than this are BAD */

/* client_key: the Sec-WebSocket-Key value (NUL-terminated). Returns 0 on success, -1 if empty/too long. */
int gb_ws_accept_key(const char *client_key, char out[29]);

typedef enum { GB_HS_NEED_MORE, GB_HS_OK, GB_HS_BAD } gb_hs_result;
/* Needs GET, Upgrade: websocket, Connection: upgrade, Sec-WebSocket-Version: 13 and a key (<= 63 chars,
 * copied NUL-terminated into key). Origin is ignored. buf need not be NUL-terminated. */
gb_hs_result gb_ws_parse_request(const char *buf, size_t n, char key[64]);
/* Return the length written (excluding NUL), or 0 if cap is too small. */
size_t gb_ws_response_101(const char accept[29], char *out, size_t cap);
size_t gb_ws_response_400(char *out, size_t cap);

typedef struct {
    uint8_t buf[2 * GB_WS_MAX_PAYLOAD + 16];
    size_t len;
    uint16_t err; /* sticky close code once an error was found; 0 = none. Zero-initialise the struct. */
} gb_ws_parser;
typedef enum { GB_WS_NONE, GB_WS_BINARY, GB_WS_PING, GB_WS_CLOSE, GB_WS_ERROR } gb_ws_kind;

/* Appends data. If it does not fit in the buffer the parser enters the error state (1009)
 * and nothing is written out of bounds. Call gb_ws_next until NONE between feeds. */
void gb_ws_feed(gb_ws_parser *p, const uint8_t *data, size_t n);
/* payload must have room for GB_WS_MAX_PAYLOAD bytes. BINARY/PING: unmasked payload. CLOSE: close_code
 * (1005 if none) and the payload (code + reason). ERROR: close_code to send (1002/1003/1009), sticky.
 * Pongs are consumed silently. */
gb_ws_kind gb_ws_next(gb_ws_parser *p, uint8_t *payload, size_t *len, uint16_t *close_code);

/* Server frame (FIN=1, unmasked). Returns bytes written or 0 if cap is too small. */
size_t gb_ws_frame(uint8_t opcode, const uint8_t *payload, size_t n, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
