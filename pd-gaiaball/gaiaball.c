/* [gaiaball]: Gaiaball data inside Pure Data.
 *
 * The config-app (in Chrome or Edge) connects to the ball over Bluetooth and forwards the raw
 * IMU (0x01), CO2 (0x02) and navigation (0x07) packets, one per binary WebSocket message, to
 * ws://127.0.0.1:<port>. This object is that WebSocket server: it decodes each packet and
 * publishes it on the gaia.* receive names ([r gaia.lat], [r gaia.co2], ...).
 *
 * - Listens on 127.0.0.1 only; [gaiaball] = port 9478, [gaiaball 9500] = port 9500.
 * - No threads: the sockets are non-blocking and polled by Pd's own loop (sys_addpollfn).
 * - One client at a time: a new connection closes the previous one.
 * - One listener per port: a [gaiaball] that finds its port in use says so once, retries every
 *   2 s and takes the port over as soon as the other object is gone (e.g. the help patch closed).
 * - No Origin check (the project's final domain is not known yet). The protection is the
 *   loopback-only bind and Chrome's "Apps neste dispositivo" permission.
 * - Status outlet: "connected 0|1" on change; every second while connected, "rate <packets/s>"
 *   and "gap <longest interval without packets, ms>". Closed socket or 2 s without packets =
 *   disconnected: "connected 0", and gaia.breath / gaia.co2.trigger get 0 once.
 */

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <errno.h>
# include <fcntl.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <unistd.h>
#endif
#include <math.h>
#include <string.h>

#include "m_pd.h"
#include "s_stuff.h" /* sys_addpollfn, sys_rmpollfn */

#include "gb_decode.h"
#include "gb_ws.h"

#define GB_DEFAULT_PORT 9478
#define GB_TIMEOUT_MS 2000.
#define GB_STATS_MS 1000.
#define GB_RETRY_MS 2000.

#ifdef _WIN32
typedef SOCKET gb_sock;
# define GB_NOSOCK INVALID_SOCKET
# define gb_closesocket closesocket
# define gb_lasterror() WSAGetLastError()
# define GB_EADDRINUSE WSAEADDRINUSE
#else
typedef int gb_sock;
# define GB_NOSOCK (-1)
# define gb_closesocket close
# define gb_lasterror() errno
# define GB_EADDRINUSE EADDRINUSE
#endif

#ifdef MSG_NOSIGNAL
# define GB_SENDFLAGS MSG_NOSIGNAL
#else
# define GB_SENDFLAGS 0
#endif

enum {
    CH_LAT, CH_LON, CH_SPEED, CH_BEARING, CH_SETTLED,
    CH_ACCX, CH_ACCY, CH_ACCZ, CH_QUAT, CH_MICRMS, CH_MICPEAK, CH_BREATH, CH_SENSORS,
    CH_CO2, CH_CO2TRIGGER,
    CH_COUNT
};

static const char *const gb_channel_names[CH_COUNT] = {
    "gaia.lat", "gaia.lon", "gaia.speed", "gaia.bearing", "gaia.settled",
    "gaia.acc.x", "gaia.acc.y", "gaia.acc.z", "gaia.quat", "gaia.mic.rms", "gaia.mic.peak",
    "gaia.breath", "gaia.sensors",
    "gaia.co2", "gaia.co2.trigger"
};

static t_class *gaiaball_class;

typedef struct _gaiaball {
    t_object x_obj;
    t_outlet *x_status;
    t_clock *x_statsclock;   /* every 1000 ms while connected: rate and gap */
    t_clock *x_watchdog;     /* 2000 ms without packets: disconnected */
    t_clock *x_retryclock;   /* port in use: try the bind again every 2000 ms */
    t_symbol *x_ch[CH_COUNT];
    int x_port;
    int x_busy;              /* the port was in use (already reported once) */
    gb_sock x_listen;        /* GB_NOSOCK: not listening (bad port, or waiting for it) */
    gb_sock x_client;        /* the one client, GB_NOSOCK if none */
    int x_open;              /* the client got its 101 */
    int x_connected;         /* last state sent on the status outlet */
    size_t x_reqlen;
    char x_req[GB_WS_MAX_REQUEST + 1];
    gb_ws_parser x_ws;
    uint8_t x_payload[GB_WS_MAX_PAYLOAD];
    int x_count;             /* packets in the current stats window */
    double x_lastrt;         /* real time of the last packet (or of the connection), s */
    double x_maxgap;         /* longest gap between packets in the current window, ms */
    t_float x_co2;           /* last CO2 ppm, for gaia.sensors; 0 until the first packet */
} t_gaiaball;

/* ---- sockets ---- */

static int gb_retry(void)
{
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static int gb_nonblocking(gb_sock s)
{
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) return -1;
    (void)fcntl(s, F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

static long gb_recv(gb_sock s, void *buf, size_t cap)
{
#ifdef _WIN32
    return recv(s, (char *)buf, (int)cap, 0);
#else
    return (long)recv(s, buf, cap, 0);
#endif
}

/* Every reply is small (101, 400, pong, close) and goes to a socket whose send buffer is
 * empty or nearly so: one non-blocking send. Returns 1 if everything was sent. */
static int gb_send(gb_sock s, const void *buf, size_t n)
{
#ifdef _WIN32
    return send(s, (const char *)buf, (int)n, 0) == (int)n;
#else
    return send(s, buf, n, GB_SENDFLAGS) == (ssize_t)n;
#endif
}

/* ---- output ---- */

static void gb_float(t_symbol *s, t_float f)
{
    if (s->s_thing) pd_float(s->s_thing, f);
}

static void gb_list(t_symbol *s, int argc, t_atom *argv)
{
    if (s->s_thing) pd_list(s->s_thing, &s_list, argc, argv);
}

static void gaiaball_report(t_gaiaball *x, const char *what, t_float f)
{
    t_atom a;
    SETFLOAT(&a, f);
    outlet_anything(x->x_status, gensym(what), 1, &a);
}

static void gaiaball_setconnected(t_gaiaball *x, int on)
{
    if (on == x->x_connected) return;
    x->x_connected = on;
    if (on)
    {
        x->x_count = 0;
        x->x_maxgap = 0;
        x->x_lastrt = sys_getrealtime();
        clock_delay(x->x_statsclock, GB_STATS_MS);
        gaiaball_report(x, "connected", 1);
    }
    else
    {
        clock_unset(x->x_statsclock);
        clock_unset(x->x_watchdog);
        gaiaball_report(x, "connected", 0);
            /* no 1 may stay stuck in the patch after a disconnection */
        gb_float(x->x_ch[CH_BREATH], 0);
        gb_float(x->x_ch[CH_CO2TRIGGER], 0);
    }
}

static void gaiaball_stats(t_gaiaball *x)
{
    double since = (sys_getrealtime() - x->x_lastrt) * 1000.;
    double gap = since > x->x_maxgap ? since : x->x_maxgap;
    int n = x->x_count;
    x->x_count = 0;
    x->x_maxgap = 0;
    clock_delay(x->x_statsclock, GB_STATS_MS);
    gaiaball_report(x, "rate", (t_float)n);
    gaiaball_report(x, "gap", (t_float)floor(gap + 0.5));
}

static void gaiaball_timeout(t_gaiaball *x)
{
    gaiaball_setconnected(x, 0); /* the socket stays open: the next packet reconnects */
}

/* ---- packets ---- */

static void gaiaball_imu(t_gaiaball *x, const uint8_t *p, size_t n)
{
    gb_imu m;
    t_atom a[7];
    float roll, pitch, yaw;
    int i;
    if (!gb_decode_imu(p, n, &m)) return;
    gb_float(x->x_ch[CH_ACCX], m.acc_mg[0]);
    gb_float(x->x_ch[CH_ACCY], m.acc_mg[1]);
    gb_float(x->x_ch[CH_ACCZ], m.acc_mg[2]);
    for (i = 0; i < 4; i++) SETFLOAT(a + i, m.quat[i]);
    gb_list(x->x_ch[CH_QUAT], 4, a);
    if (m.audio_valid)
    {
        SETFLOAT(a, m.rms_db[0]);
        SETFLOAT(a + 1, m.rms_db[1]);
        gb_list(x->x_ch[CH_MICRMS], 2, a);
        SETFLOAT(a, m.peak_db[0]);
        SETFLOAT(a + 1, m.peak_db[1]);
        gb_list(x->x_ch[CH_MICPEAK], 2, a);
    }
    gb_float(x->x_ch[CH_BREATH], m.breath);
    gb_euler_legacy(m.quat, &roll, &pitch, &yaw);
    SETFLOAT(a, roll);
    SETFLOAT(a + 1, pitch);
    SETFLOAT(a + 2, yaw);
    for (i = 0; i < 3; i++) SETFLOAT(a + 3 + i, m.acc_mg[i]);
    SETFLOAT(a + 6, x->x_co2);
    gb_list(x->x_ch[CH_SENSORS], 7, a);
}

static void gaiaball_co2(t_gaiaball *x, const uint8_t *p, size_t n)
{
    gb_co2 c;
    if (!gb_decode_co2(p, n, &c)) return;
    x->x_co2 = c.ppm;
    gb_float(x->x_ch[CH_CO2], c.ppm);
    gb_float(x->x_ch[CH_CO2TRIGGER], c.trigger);
}

static void gaiaball_nav(t_gaiaball *x, const uint8_t *p, size_t n)
{
    gb_nav v;
    if (!gb_decode_nav(p, n, &v) || !v.valid) return;
    gb_float(x->x_ch[CH_LAT], (t_float)v.lat);
    gb_float(x->x_ch[CH_LON], (t_float)v.lon);
    gb_float(x->x_ch[CH_SPEED], v.speed);
    gb_float(x->x_ch[CH_BEARING], v.bearing);
    gb_float(x->x_ch[CH_SETTLED], v.settled);
}

static void gaiaball_packet(t_gaiaball *x, const uint8_t *p, size_t n)
{
    double now = sys_getrealtime(), gap = (now - x->x_lastrt) * 1000.;
    clock_delay(x->x_watchdog, GB_TIMEOUT_MS);
    if (!x->x_connected)
        gaiaball_setconnected(x, 1); /* packets again after a silence */
    else if (gap > x->x_maxgap)
        x->x_maxgap = gap;
    x->x_lastrt = now;
    x->x_count++;
        /* anything else (or a packet the decoder refuses) only keeps the link alive */
    if (n < 2) return;
    switch (p[1])
    {
    case 0x01: gaiaball_imu(x, p, n); break;
    case 0x02: gaiaball_co2(x, p, n); break;
    case 0x07: gaiaball_nav(x, p, n); break;
    default: break;
    }
}

/* ---- client ---- */

/* Returns 0 if the frame did not go out whole: the stream to the client would be out of sync. */
static int gaiaball_sendframe(t_gaiaball *x, uint8_t opcode, const uint8_t *p, size_t n)
{
    uint8_t out[2 + 125]; /* control frames only: payload <= 125 */
    size_t m = gb_ws_frame(opcode, p, n, out, sizeof out);
    return m && gb_send(x->x_client, out, m);
}

/* Always followed by dropping the client, so a short send needs no extra handling. */
static void gaiaball_sendclose(t_gaiaball *x, uint16_t code)
{
    uint8_t c[2];
    c[0] = (uint8_t)(code >> 8);
    c[1] = (uint8_t)code;
    (void)gaiaball_sendframe(x, 0x8, c, code == 1005 ? 0 : 2); /* 1005 = "no code": empty */
}

/* Close the client socket without touching the status. */
static void gaiaball_drop(t_gaiaball *x)
{
    if (x->x_client == GB_NOSOCK) return;
    sys_rmpollfn((int)x->x_client);
    gb_closesocket(x->x_client);
    x->x_client = GB_NOSOCK;
    x->x_open = 0;
    x->x_reqlen = 0;
}

static void gaiaball_lost(t_gaiaball *x)
{
    gaiaball_drop(x);
    gaiaball_setconnected(x, 0);
}

/* Runs every complete frame in the parser. Returns 0 if the client was dropped. */
static int gaiaball_drain(t_gaiaball *x)
{
    for (;;)
    {
        size_t len;
        uint16_t code;
        switch (gb_ws_next(&x->x_ws, x->x_payload, &len, &code))
        {
        case GB_WS_NONE:
            return 1;
        case GB_WS_BINARY:
            gaiaball_packet(x, x->x_payload, len);
            break;
        case GB_WS_PING:
            if (!gaiaball_sendframe(x, 0xA, x->x_payload, len))
            {
                gaiaball_lost(x); /* short send: drop rather than desynchronise the stream */
                return 0;
            }
            break;
        case GB_WS_CLOSE: /* echo the client's code */
        case GB_WS_ERROR: /* the code gb_ws chose: 1002, 1003 or 1009 */
            gaiaball_sendclose(x, code);
            gaiaball_lost(x);
            return 0;
        }
    }
}

/* Feed in chunks the parser can take, draining between them (gb_ws.h contract). */
static void gaiaball_feed(t_gaiaball *x, const uint8_t *p, size_t n)
{
    while (n > 0)
    {
        size_t room = gb_ws_space(&x->x_ws), take = n < room ? n : room;
        gb_ws_feed(&x->x_ws, p, take);
        p += take;
        n -= take;
        if (!gaiaball_drain(x) || !take) return; /* !take: the parser reported an error */
    }
}

static void gaiaball_handshake(t_gaiaball *x)
{
    char key[64], acceptkey[29], resp[256];
    size_t n = 0, end;
    gb_hs_result r = gb_ws_parse_request(x->x_req, x->x_reqlen, key);
    if (r == GB_HS_NEED_MORE) return;
    if (r == GB_HS_OK && gb_ws_accept_key(key, acceptkey) == 0)
        n = gb_ws_response_101(acceptkey, resp, sizeof resp);
    if (!n)
    {
        n = gb_ws_response_400(resp, sizeof resp);
        if (n) gb_send(x->x_client, resp, n);
        logpost(x, 3 /* PD_DEBUG */, "gaiaball: pedido recusado (não é um WebSocket)");
        gaiaball_lost(x);
        return;
    }
    if (!gb_send(x->x_client, resp, n))
    {
        gaiaball_lost(x);
        return;
    }
    x->x_open = 1;
    memset(&x->x_ws, 0, sizeof x->x_ws);
    clock_delay(x->x_watchdog, GB_TIMEOUT_MS);
    gaiaball_setconnected(x, 1);
        /* bytes after CRLFCRLF in the same read come from a client that did not wait for
        the 101: gb_ws_parse_request does not consume them, so they go to the frame parser */
    for (end = 0; end + 4 <= x->x_reqlen; end++)
        if (!memcmp(x->x_req + end, "\r\n\r\n", 4)) break;
    end += 4;
    n = x->x_reqlen > end ? x->x_reqlen - end : 0;
    x->x_reqlen = 0;
    if (n && x->x_open) gaiaball_feed(x, (const uint8_t *)x->x_req + end, n);
}

static void gaiaball_read(void *z, int fd)
{
    t_gaiaball *x = (t_gaiaball *)z;
    uint8_t buf[4096];
    void *dst = buf;
    size_t cap = sizeof buf;
    long r;
    (void)fd;
    if (x->x_client == GB_NOSOCK) return;
    if (!x->x_open)
    {
        dst = x->x_req + x->x_reqlen;
        cap = sizeof x->x_req - x->x_reqlen; /* > 0: an overlong request is refused first */
    }
    r = gb_recv(x->x_client, dst, cap);
    if (r < 0 && gb_retry()) return;
    if (r <= 0)
    {
        gaiaball_lost(x); /* closed by the other side, or a socket error */
        return;
    }
    if (x->x_open)
        gaiaball_feed(x, buf, (size_t)r);
    else
    {
        x->x_reqlen += (size_t)r;
        gaiaball_handshake(x);
    }
}

static void gaiaball_accept(void *z, int fd)
{
    t_gaiaball *x = (t_gaiaball *)z;
    gb_sock s;
    int one = 1;
    (void)fd;
    s = accept(x->x_listen, NULL, NULL);
    if (s == GB_NOSOCK) return;
    if (gb_nonblocking(s) != 0)
    {
        gb_closesocket(s);
        return;
    }
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#ifdef SO_NOSIGPIPE
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
        /* one client at a time: the new one replaces the old (status unchanged) */
    if (x->x_client != GB_NOSOCK)
    {
        if (x->x_open) gaiaball_sendclose(x, 1000);
        gaiaball_drop(x);
    }
    x->x_client = s;
    x->x_open = 0;
    x->x_reqlen = 0;
    sys_addpollfn((int)s, gaiaball_read, x);
}

/* ---- class ---- */

/* Returns 1 when listening, -1 if the port is in use (worth retrying), 0 on other errors
 * (reported here). */
static int gaiaball_trylisten(t_gaiaball *x)
{
    struct sockaddr_in sa;
    gb_sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int err;
    if (s == GB_NOSOCK)
    {
        pd_error(x, "gaiaball: não foi possível criar o socket (erro %d)", gb_lasterror());
        return 0;
    }
#ifndef _WIN32
    {
            /* reopening the patch right after closing it must not find the port busy;
            two listeners on the same port still fail (EADDRINUSE). Not on Windows, where
            SO_REUSEADDR would let a second [gaiaball] take the port. */
        int one = 1;
        (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
#endif
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)x->x_port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&sa, (socklen_t)sizeof sa) != 0)
    {
        err = gb_lasterror();
        gb_closesocket(s);
        if (err == GB_EADDRINUSE) return -1;
        pd_error(x, "gaiaball: não foi possível escutar na porta %d (erro %d)", x->x_port, err);
        return 0;
    }
    if (listen(s, 4) != 0 || gb_nonblocking(s) != 0)
    {
        err = gb_lasterror();
        gb_closesocket(s);
        pd_error(x, "gaiaball: não foi possível escutar na porta %d (erro %d)", x->x_port, err);
        return 0;
    }
    x->x_listen = s;
    sys_addpollfn((int)s, gaiaball_accept, x);
    return 1;
}

/* At creation, and from the retry clock while the port is in use. */
static void gaiaball_listen(t_gaiaball *x)
{
    int r = gaiaball_trylisten(x);
    if (r < 0)
    {
        if (!x->x_busy) /* reported once; the retries are quiet */
        {
            pd_error(x, "gaiaball: porta %d em uso", x->x_port);
            logpost(x, 2 /* PD_NORMAL */, "gaiaball: este objeto assume a porta sozinho quando "
                "ela ficar livre (por exemplo, ao fechar o outro [gaiaball])");
        }
        x->x_busy = 1;
        clock_delay(x->x_retryclock, GB_RETRY_MS);
    }
    else if (r > 0 && x->x_busy)
    {
        x->x_busy = 0;
        logpost(x, 2 /* PD_NORMAL */,
            "gaiaball: a porta %d ficou livre: este objeto passou a escutar", x->x_port);
    }
    else if (r > 0)
        logpost(x, 3 /* PD_DEBUG */, "gaiaball: aguardando a config-app em ws://127.0.0.1:%d",
            x->x_port);
}

static void *gaiaball_new(t_floatarg f)
{
    t_gaiaball *x = (t_gaiaball *)pd_new(gaiaball_class);
    int i;
    x->x_status = outlet_new(&x->x_obj, 0);
    x->x_statsclock = clock_new(x, (t_method)gaiaball_stats);
    x->x_watchdog = clock_new(x, (t_method)gaiaball_timeout);
    x->x_retryclock = clock_new(x, (t_method)gaiaball_listen);
    for (i = 0; i < CH_COUNT; i++) x->x_ch[i] = gensym(gb_channel_names[i]);
    x->x_listen = x->x_client = GB_NOSOCK;
    if (f != 0 && (f < 1 || f > 65535 || f != (t_float)(int)f))
    {
        pd_error(x, "gaiaball: porta inválida: %g (use um número de 1 a 65535)", f);
        return x; /* inert */
    }
    x->x_port = f == 0 ? GB_DEFAULT_PORT : (int)f;
    gaiaball_listen(x);
    return x;
}

static void gaiaball_free(t_gaiaball *x)
{
    if (x->x_client != GB_NOSOCK && x->x_open) gaiaball_sendclose(x, 1001); /* going away */
    gaiaball_drop(x);
    if (x->x_connected)
    {
            /* deleted mid-breath: no 1 may stay stuck (no status output while freeing) */
        gb_float(x->x_ch[CH_BREATH], 0);
        gb_float(x->x_ch[CH_CO2TRIGGER], 0);
    }
    if (x->x_listen != GB_NOSOCK)
    {
        sys_rmpollfn((int)x->x_listen);
        gb_closesocket(x->x_listen);
    }
    clock_free(x->x_statsclock);
    clock_free(x->x_watchdog);
    clock_free(x->x_retryclock);
}

void gaiaball_setup(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        pd_error(0, "gaiaball: WSAStartup falhou");
        return;
    }
#endif
        /* via void (*)(void), the type -Wcast-function-type accepts as "any function" */
    gaiaball_class = class_new(gensym("gaiaball"),
        (t_newmethod)(void (*)(void))gaiaball_new, (t_method)gaiaball_free,
        sizeof(t_gaiaball), CLASS_NOINLET, A_DEFFLOAT, 0);
}
