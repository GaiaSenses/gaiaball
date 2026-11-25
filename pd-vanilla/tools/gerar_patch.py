#!/usr/bin/env python3
"""Generates ../gaiaball-vanilla.pd: the Gaiaball receiver in Pd vanilla, with no externals.

The config-app (Chrome/Edge) connects to the ball over Bluetooth and forwards the raw
IMU (0x01), CO2 (0x02) and navigation (0x07) packets, one per binary WebSocket message, to
ws://127.0.0.1:9478. The patch is that WebSocket server, built only from vanilla objects:

  - TCP: [netreceive -b] listening on 127.0.0.1 only (bytes arrive one float at a time);
  - handshake: SHA-1 + base64 of the Sec-WebSocket-Key computed in [expr]. Pd floats are
    32-bit (exact integers only up to 2^24), so every 32-bit word is a pair of 16-bit halves
    and no intermediate value goes past 24 bits;
  - frames: a byte-by-byte state machine in a single [expr] (state kept as a list);
  - packets: decoded like pd-gaiaball/src/gb_decode.c and published on the same gaia.*
    names as the [gaiaball] external.

Usage: python3 gerar_patch.py   (writes ../gaiaball-vanilla.pd). The .pd is the deliverable;
this script exists so the patch can be regenerated after a change without rewiring by hand.
"""

import re
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "gaiaball-vanilla.pd"


# ---------------------------------------------------------------- .pd writer

def esc(s):
    s = s.replace("$", "\\$")
    s = s.replace(";", " \\; ").replace(",", " \\, ")
    return re.sub(r"\s+", " ", s).strip()


class Canvas:
    def __init__(self, name=None, rect=(60, 60, 900, 640), font=12):
        self.name, self.rect, self.font = name, rect, font
        self.items, self.conns, self.n = [], [], 0
        self.pos = (0, 0)

    def _add(self, item):
        self.items.append(item)
        self.n += 1
        return self.n - 1

    def obj(self, x, y, text, w=None):
        return self._add(f"#X obj {x} {y} {esc(text)}" + (f", f {w}" if w else "") + ";")

    def msg(self, x, y, text, w=None):
        return self._add(f"#X msg {x} {y} {esc(text)}" + (f", f {w}" if w else "") + ";")

    def text(self, x, y, text, w=None):
        return self._add(f"#X text {x} {y} {esc(text)}" + (f", f {w}" if w else "") + ";")

    def atom(self, x, y, width=6, rcv="-"):
        return self._add(f"#X floatatom {x} {y} {width} 0 0 0 - {esc(rcv)} - 0;")

    def toggle(self, x, y, size=20):
        return self._add(f"#X obj {x} {y} tgl {size} 0 empty empty empty 17 7 0 10 "
                         "#fcfcfc #000000 #000000 0 1;")

    def sub(self, x, y, canvas):
        canvas.pos = (x, y)
        return self._add(canvas)

    def c(self, a, ao, b, bi):
        self.conns.append((a, ao, b, bi))

    def render(self, top=False):
        x, y, w, h = self.rect
        out = [f"#N canvas {x} {y} {w} {h} {self.font};" if top
               else f"#N canvas {x} {y} {w} {h} {self.name} 0;"]
        for it in self.items:
            if isinstance(it, Canvas):
                out += it.render()
                out.append(f"#X restore {it.pos[0]} {it.pos[1]} pd {it.name};")
            else:
                out.append(it)
        out += [f"#X connect {a} {ao} {b} {bi};" for a, ao, b, bi in self.conns]
        return out


def ascii_codes(s):
    return " ".join(str(ord(ch)) for ch in s)


def pack(n):
    return "pack " + " ".join(["f"] * n)


def expr(*outs):
    return "expr " + " ; ".join(outs)


def halves(word):
    return word >> 16, word & 0xFFFF



# ---------------------------------------------------------------- [pd rede]

def build_rede():
    cv = Canvas("rede", (80, 80, 760, 560))
    cv.text(20, 10, "TCP na porta do argumento (padrão 9478) e só em 127.0.0.1. "
            "Um cliente por vez: se um segundo conectar, todos caem e o config-app "
            "reconecta sozinho em 2 s.", 70)

    lb = cv.obj(20, 60, "loadbang")
    fa = cv.obj(20, 90, "f $1")
    ex = cv.obj(20, 120, "expr if($f1 > 0, $f1, 9478)")
    tb = cv.obj(20, 150, "t b f")
    vp = cv.obj(120, 180, "v $0-port")
    sg = cv.obj(220, 180, "s $0-gui-port")
    sr = cv.obj(20, 180, "s $0-relisten")
    cv.c(lb, 0, fa, 0); cv.c(fa, 0, ex, 0); cv.c(ex, 0, tb, 0)
    cv.c(tb, 1, vp, 0); cv.c(tb, 1, sg, 0); cv.c(tb, 0, sr, 0)

    rr = cv.obj(20, 230, "r $0-relisten")
    vp2 = cv.obj(20, 260, "v $0-port")
    ml = cv.msg(20, 290, "listen $1 127.0.0.1")
    rtx = cv.obj(200, 290, "r $0-tx")
    nr = cv.obj(20, 330, "netreceive -b")
    cv.c(rr, 0, vp2, 0); cv.c(vp2, 0, ml, 0); cv.c(ml, 0, nr, 0); cv.c(rtx, 0, nr, 0)

    # bytes -> HTTP (mode 0) or WebSocket (mode 1); mode 2 drops them
    rm = cv.obj(140, 360, "r $0-mode")
    lp = cv.obj(20, 390, "list prepend 0")
    rt = cv.obj(20, 420, "route 0 1")
    sh = cv.obj(20, 450, "s $0-http-byte")
    sw = cv.obj(140, 450, "s $0-ws-byte")
    cv.c(nr, 0, lp, 0); cv.c(rm, 0, lp, 1); cv.c(lp, 0, rt, 0)
    cv.c(rt, 0, sh, 0); cv.c(rt, 1, sw, 0)

    # number of connections: (n, previous n) -> 1 gone, 2 new client, 3 more than one
    tc = cv.obj(330, 360, "t f f")
    la = cv.obj(330, 390, "list append 0")
    ce = cv.obj(330, 420, "expr if($f1 >= 2, 3, if($f1 == 0, 1, if($f2 == 0, 2, 0)))")
    se = cv.obj(330, 450, "sel 1 2 3")
    sgo = cv.obj(330, 480, "s $0-gone")
    sne = cv.obj(420, 480, "s $0-newclient")
    ski = cv.obj(540, 480, "s $0-kick")
    cv.c(nr, 1, tc, 0); cv.c(tc, 1, la, 0); cv.c(tc, 0, la, 1); cv.c(la, 0, ce, 0)
    cv.c(ce, 0, se, 0); cv.c(se, 0, sgo, 0); cv.c(se, 1, sne, 0); cv.c(se, 2, ski, 0)

    # client gone: back to HTTP, reset everything, status disconnected
    rg = cv.obj(420, 60, "r $0-gone")
    tg = cv.obj(420, 90, "t b b b b")
    m0 = cv.msg(560, 120, "0")
    sm0 = cv.obj(560, 150, "s $0-mode")
    shr = cv.obj(500, 180, "s $0-httpreset")
    swr = cv.obj(460, 210, "s $0-wsreset")
    sdi = cv.obj(420, 240, "s $0-disc")
    cv.c(rg, 0, tg, 0); cv.c(tg, 3, m0, 0); cv.c(m0, 0, sm0, 0)
    cv.c(tg, 2, shr, 0); cv.c(tg, 1, swr, 0); cv.c(tg, 0, sdi, 0)

    # new client: HTTP mode, clean request parser
    rn = cv.obj(620, 60, "r $0-newclient")
    tn = cv.obj(620, 90, "t b b")
    m1 = cv.msg(660, 120, "0")
    sm1 = cv.obj(660, 150, "s $0-mode")
    shr2 = cv.obj(620, 180, "s $0-httpreset")
    cv.c(rn, 0, tn, 0); cv.c(tn, 1, m1, 0); cv.c(m1, 0, sm1, 0); cv.c(tn, 0, shr2, 0)

    # kick: drop the bytes now; close every connection and listen again right after
    rk = cv.obj(420, 290, "r $0-kick")
    tk = cv.obj(420, 320, "t b b")
    m2 = cv.msg(480, 350, "2")
    sm2 = cv.obj(480, 380, "s $0-mode")
    dl = cv.obj(420, 380, "del 0")
    sr2 = cv.obj(420, 410, "s $0-relisten")
    cv.c(rk, 0, tk, 0); cv.c(tk, 1, m2, 0); cv.c(m2, 0, sm2, 0)
    cv.c(tk, 0, dl, 0); cv.c(dl, 0, sr2, 0)
    return cv


# ---------------------------------------------------------------- [pd http]

def build_http():
    cv = Canvas("http", (100, 80, 860, 640))
    cv.text(20, 10, "Pedido HTTP do navegador, byte a byte. Guarda o valor da linha "
            "Sec-WebSocket-Key (sem diferença de maiúsculas no nome). Linha vazia = fim: "
            "chave de 24 caracteres -> resposta 101, senão 400.", 80)

    rb = cv.obj(20, 60, "r $0-http-byte")
    tb = cv.obj(20, 90, "t f b")
    vc = cv.obj(80, 120, "v $0-col")
    pk = cv.obj(20, 150, "pack 0 0")
    # act: 0 ignore (CR), 1 first 18 bytes of the line, 2 end of line, 3 value byte, 4 other
    ex = cv.obj(20, 180, expr(
        "if($f1 == 10, 2, if($f1 == 13, 0, if($f2 < 18, 1, if($f1 > 32 && $f2 < 200, 3, 4))))",
        "if($f1 >= 65 && $f1 <= 90, $f1 + 32, $f1)",
        "$f1"), 60)
    fb = cv.obj(300, 250, "f")
    fl = cv.obj(160, 250, "f")
    se = cv.obj(20, 250, "sel 1 2 3 4")
    cv.c(rb, 0, tb, 0); cv.c(tb, 1, vc, 0); cv.c(vc, 0, pk, 1); cv.c(tb, 0, pk, 0)
    cv.c(pk, 0, ex, 0); cv.c(ex, 2, fb, 1); cv.c(ex, 1, fl, 1); cv.c(ex, 0, se, 0)

    vinc = cv.obj(560, 300, "v $0-col")
    inc = cv.obj(560, 330, "+ 1")
    cv.c(vinc, 0, inc, 0); cv.c(inc, 0, vinc, 0)

    t1 = cv.obj(160, 280, "t b b")
    a1 = cv.msg(160, 340, "append $1")
    low = cv.obj(160, 370, "list store")
    cv.c(se, 0, t1, 0); cv.c(t1, 1, fl, 0); cv.c(fl, 0, a1, 0); cv.c(a1, 0, low, 0)
    cv.c(t1, 0, vinc, 0)

    t3 = cv.obj(300, 280, "t b b")
    a3 = cv.msg(300, 340, "append $1")
    val = cv.obj(300, 370, "list store")
    cv.c(se, 2, t3, 0); cv.c(t3, 1, fb, 0); cv.c(fb, 0, a3, 0); cv.c(a3, 0, val, 0)
    cv.c(t3, 0, vinc, 0)
    cv.c(se, 3, vinc, 0)

    # end of line: 1) is it the key line? 2) empty line = end of headers 3) clear 4) col = 0
    te = cv.obj(20, 410, "t b b b b")
    lts = cv.obj(160, 440, "list tosymbol")
    sk = cv.obj(160, 470, "sel sec-websocket-key:")
    key = cv.obj(20, 530, "list")
    cv.c(se, 1, te, 0); cv.c(te, 3, low, 0); cv.c(low, 0, lts, 0); cv.c(lts, 0, sk, 0)
    cv.c(sk, 0, val, 0); cv.c(val, 0, key, 1)

    vc2 = cv.obj(80, 440, "v $0-col")
    s0 = cv.obj(80, 470, "sel 0")
    cv.c(te, 2, vc2, 0); cv.c(vc2, 0, s0, 0); cv.c(s0, 0, key, 0)

    clr = cv.msg(420, 410, "list")
    m0 = cv.msg(560, 410, "0")
    cv.c(te, 1, clr, 0); cv.c(clr, 0, low, 1); cv.c(clr, 0, val, 1)
    cv.c(te, 0, m0, 0); cv.c(m0, 0, vinc, 0)

    rr = cv.obj(420, 60, "r $0-httpreset")
    tr = cv.obj(420, 90, "t b b")
    clr2 = cv.msg(460, 120, "list")
    m02 = cv.msg(420, 150, "0")
    cv.c(rr, 0, tr, 0); cv.c(tr, 1, clr2, 0)
    cv.c(clr2, 0, low, 1); cv.c(clr2, 0, val, 1); cv.c(clr2, 0, key, 1)
    cv.c(tr, 0, m02, 0); cv.c(m02, 0, vinc, 0)

    # end of headers: a 24-char key goes to SHA-1 (the 101 goes out from there), else 400
    tk = cv.obj(20, 560, "t l l")
    ln = cv.obj(120, 590, "list length")
    eq = cv.obj(120, 620, expr("$f1 == 24", "$f1 != 24"))
    sok = cv.obj(20, 650, "spigot")
    sbad = cv.obj(260, 650, "spigot")
    cv.c(key, 0, tk, 0); cv.c(tk, 1, ln, 0); cv.c(ln, 0, eq, 0)
    cv.c(eq, 0, sok, 1); cv.c(eq, 1, sbad, 1); cv.c(tk, 0, sok, 0); cv.c(tk, 0, sbad, 0)

    tok = cv.obj(20, 680, "t b l")
    ssha = cv.obj(80, 710, "s $0-sha1-in")
    shs = cv.obj(20, 740, "s $0-hs-ok")
    cv.c(sok, 0, tok, 0); cv.c(tok, 1, ssha, 0); cv.c(tok, 0, shs, 0)

    tbad = cv.obj(260, 680, "t b b")
    bad = cv.msg(320, 710, ascii_codes("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"), 50)
    stx = cv.obj(320, 800, "s $0-tx")
    skk = cv.obj(260, 830, "s $0-kick")
    cv.c(sbad, 0, tbad, 0); cv.c(tbad, 1, bad, 0); cv.c(bad, 0, stx, 0); cv.c(tbad, 0, skk, 0)
    return cv


# ---------------------------------------------------------------- [pd sha1]

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
H_INIT = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0]
K = [0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6]


def round_expr():
    """Inputs: ah al bh bl ch cl dh dl eh el wh wl t.  Outputs: the next a..e (10 halves)."""
    ah, al, bh, bl, ch, cl, dh, dl, eh, el, wh, wl, t = (f"$f{i}" for i in range(1, 14))

    def f(b, c, d):
        ch_ = f"(({b} & {c}) | ((65535 - {b}) & {d}))"
        par = f"({b} ^ {c} ^ {d})"
        maj = f"(({b} & {c}) | ({b} & {d}) | ({c} & {d}))"
        return f"if({t} < 20, {ch_}, if({t} < 40, {par}, if({t} < 60, {maj}, {par})))"

    def k(part):
        v = [halves(x)[part] for x in K]
        return f"if({t} < 20, {v[0]}, if({t} < 40, {v[1]}, if({t} < 60, {v[2]}, {v[3]})))"

    rot5h = f"((({ah} & 2047) << 5) | ({al} >> 11))"
    rot5l = f"((({al} & 2047) << 5) | ({ah} >> 11))"
    suml = f"({rot5l} + {f(bl, cl, dl)} + {el} + {k(1)} + {wl})"
    temph = f"(({rot5h} + {f(bh, ch, dh)} + {eh} + {k(0)} + {wh} + ({suml} >> 16)) & 65535)"
    templ = f"({suml} & 65535)"
    rot30h = f"(({bh} >> 2) | (({bl} & 3) << 14))"
    rot30l = f"(({bl} >> 2) | (({bh} & 3) << 14))"
    return expr(temph, templ, ah, al, rot30h, rot30l, ch, cl, dh, dl)


def hadd_expr():
    """Inputs: H (10 halves) then S (10 halves). Outputs: H + S, word by word, mod 2^32."""
    outs = []
    for i in range(5):
        hh, hl, sh, sl = (f"$f{2 * i + 1}", f"$f{2 * i + 2}",
                          f"$f{10 + 2 * i + 1}", f"$f{10 + 2 * i + 2}")
        outs.append(f"(({hh} + {sh} + (({hl} + {sl}) >> 16)) & 65535)")
        outs.append(f"(({hl} + {sl}) & 65535)")
    return expr(*outs)


def schedule_expr():
    """Input: W[t-16..t-3] (28 halves). Output: W[t] = rotl1(W[t-3]^W[t-8]^W[t-14]^W[t-16])."""
    xh = "($f1 ^ $f5 ^ $f17 ^ $f27)"
    xl = "($f2 ^ $f6 ^ $f18 ^ $f28)"
    return expr(f"((({xh} & 32767) << 1) | ({xl} >> 15))",
                f"((({xl} & 32767) << 1) | ({xh} >> 15))")


def bytes_expr():
    """Input: the 10 halves of the digest. Output: its 20 bytes and a 0 (base64 padding)."""
    outs = []
    for kk in range(20):
        h = f"$f{kk // 2 + 1}"
        outs.append(f"({h} >> 8)" if kk % 2 == 0 else f"({h} & 255)")
    outs.append("0")
    return expr(*outs)


def sextets_expr():
    outs = []
    for g in range(7):
        b0, b1, b2 = (f"$f{3 * g + i}" for i in (1, 2, 3))
        outs += [f"({b0} >> 2)",
                 f"((({b0} & 3) << 4) | ({b1} >> 4))",
                 f"((({b1} & 15) << 2) | ({b2} >> 6))",
                 f"({b2} & 63)"]
    return expr(*outs)


def b64chars_expr():
    outs = []
    for i in range(27):
        v = f"$f{i + 1}"
        outs.append(f"if({v} < 26, {v} + 65, if({v} < 52, {v} + 71, "
                    f"if({v} < 62, {v} - 4, if({v} == 62, 43, 47))))")
    outs.append("61")  # 20 bytes -> the last of the 28 characters is '='
    return expr(*outs)


def build_sha1():
    cv = Canvas("sha1", (120, 80, 1100, 820))
    cv.text(20, 10, "SHA-1 da chave + GUID (RFC 6455) e base64. O Pd guarda números em "
            "float de 32 bits (inteiros exatos só até 16.777.216): cada palavra de 32 bits "
            "vira duas metades de 16 bits (alta e baixa) e nenhuma conta passa de 24 bits. "
            "Roda uma vez por conexão.", 100)

    cv.obj(820, 60, "array define $0-b 128")
    cv.obj(820, 90, "array define $0-w 160")

    rin = cv.obj(20, 60, "r $0-sha1-in")
    tin = cv.obj(20, 90, "t b l")
    pad = [ord(ch) for ch in GUID] + [128, 0, 0, 0] + [0] * 56 + [0, 0, 0, 0, 0, 0, 1, 224]
    assert 24 + len(pad) == 128
    app = cv.obj(120, 120, "list append " + " ".join(map(str, pad)), 80)
    bset = cv.obj(120, 180, "array set $0-b")
    cv.c(rin, 0, tin, 0); cv.c(tin, 1, app, 0); cv.c(app, 0, bset, 0)

    tm = cv.obj(20, 220, "t b b b b")
    hin = cv.msg(380, 250, " ".join(str(v) for w in H_INIT for v in halves(w)))
    hd = cv.obj(380, 280, "t l l l")
    h_a = cv.obj(380, 520, "list")
    h_b = cv.obj(700, 640, "list")
    h_c = cv.obj(20, 640, "list")
    cv.c(tin, 0, tm, 0); cv.c(tm, 3, hin, 0); cv.c(hin, 0, hd, 0)
    cv.c(hd, 2, h_a, 1); cv.c(hd, 1, h_b, 1); cv.c(hd, 0, h_c, 1)

    blk0 = cv.msg(200, 250, "0")
    blk1 = cv.msg(250, 250, "64")
    tb = cv.obj(200, 300, "t b b b b f")
    cv.c(tm, 2, blk0, 0); cv.c(tm, 1, blk1, 0); cv.c(blk0, 0, tb, 0); cv.c(blk1, 0, tb, 0)

    # W[0..15] from the block's bytes
    wi_t = cv.obj(200, 340, "t b b")
    wi_n = cv.msg(200, 370, "16")
    wi_z = cv.msg(250, 370, "0")
    wi_u = cv.obj(200, 400, "until")
    wi_f = cv.obj(200, 430, "f")
    wi_i = cv.obj(240, 430, "+ 1")
    wi_tt = cv.obj(200, 460, "t f f")
    wi_m4 = cv.obj(200, 490, "* 4")
    wi_pl = cv.obj(200, 520, "+")
    wi_get = cv.obj(200, 550, "array get $0-b 0 4")  # a float on the left = onset + output
    wi_ex = cv.obj(200, 580, expr("$f1*256 + $f2", "$f3*256 + $f4"))
    wi_pk = cv.obj(200, 610, "pack f f")
    wi_m2 = cv.obj(300, 490, "* 2")
    wi_set = cv.obj(200, 640, "array set $0-w")
    cv.c(tb, 4, wi_pl, 1); cv.c(tb, 3, wi_t, 0)
    cv.c(wi_t, 1, wi_z, 0); cv.c(wi_z, 0, wi_f, 1); cv.c(wi_t, 0, wi_n, 0); cv.c(wi_n, 0, wi_u, 0)
    cv.c(wi_u, 0, wi_f, 0); cv.c(wi_f, 0, wi_i, 0); cv.c(wi_i, 0, wi_f, 1); cv.c(wi_f, 0, wi_tt, 0)
    cv.c(wi_tt, 1, wi_m2, 0); cv.c(wi_m2, 0, wi_set, 1)
    cv.c(wi_tt, 0, wi_m4, 0); cv.c(wi_m4, 0, wi_pl, 0); cv.c(wi_pl, 0, wi_get, 0)
    cv.c(wi_get, 0, wi_ex, 0); cv.c(wi_ex, 0, wi_pk, 0); cv.c(wi_ex, 1, wi_pk, 1)
    cv.c(wi_pk, 0, wi_set, 0)

    # W[16..79]
    we_t = cv.obj(420, 340, "t b b")
    we_n = cv.msg(420, 370, "64")
    we_z = cv.msg(470, 370, "16")
    we_u = cv.obj(420, 400, "until")
    we_f = cv.obj(420, 430, "f")
    we_i = cv.obj(460, 430, "+ 1")
    we_tt = cv.obj(420, 460, "t f f")
    we_on = cv.obj(420, 490, "expr ($f1 - 16) * 2")
    we_get = cv.obj(420, 520, "array get $0-w 0 28")
    we_ex = cv.obj(420, 550, schedule_expr(), 60)
    we_pk = cv.obj(420, 610, "pack f f")
    we_m2 = cv.obj(560, 490, "* 2")
    we_set = cv.obj(420, 640, "array set $0-w")
    cv.c(tb, 2, we_t, 0)
    cv.c(we_t, 1, we_z, 0); cv.c(we_z, 0, we_f, 1); cv.c(we_t, 0, we_n, 0); cv.c(we_n, 0, we_u, 0)
    cv.c(we_u, 0, we_f, 0); cv.c(we_f, 0, we_i, 0); cv.c(we_i, 0, we_f, 1); cv.c(we_f, 0, we_tt, 0)
    cv.c(we_tt, 1, we_m2, 0); cv.c(we_m2, 0, we_set, 1)
    cv.c(we_tt, 0, we_on, 0); cv.c(we_on, 0, we_get, 0); cv.c(we_get, 0, we_ex, 0)
    cv.c(we_ex, 0, we_pk, 0); cv.c(we_ex, 1, we_pk, 1); cv.c(we_pk, 0, we_set, 0)

    # 80 rounds: S = H, then S = round(S, W[t], t)
    ro_t = cv.obj(640, 340, "t b b b")
    ro_n = cv.msg(640, 370, "80")
    ro_z = cv.msg(690, 370, "0")
    ro_u = cv.obj(640, 400, "until")
    ro_f = cv.obj(640, 430, "f")
    ro_i = cv.obj(680, 430, "+ 1")
    ro_tt = cv.obj(640, 460, "t f f")
    ro_m2 = cv.obj(640, 490, "* 2")
    ro_get = cv.obj(640, 520, "array get $0-w 0 2")
    ro_la = cv.obj(640, 550, "list append")
    ro_rp = cv.obj(640, 580, "list prepend")
    ro_ex = cv.obj(820, 400, round_expr(), 40)
    ro_pk = cv.obj(820, 700, pack(10))
    ro_ts = cv.obj(820, 730, "t l l")
    s_fin = cv.obj(820, 760, "list")
    cv.c(tb, 1, ro_t, 0)
    cv.c(ro_t, 2, h_a, 0); cv.c(h_a, 0, ro_rp, 1)
    cv.c(ro_t, 1, ro_z, 0); cv.c(ro_z, 0, ro_f, 1); cv.c(ro_t, 0, ro_n, 0); cv.c(ro_n, 0, ro_u, 0)
    cv.c(ro_u, 0, ro_f, 0); cv.c(ro_f, 0, ro_i, 0); cv.c(ro_i, 0, ro_f, 1); cv.c(ro_f, 0, ro_tt, 0)
    cv.c(ro_tt, 1, ro_la, 1); cv.c(ro_tt, 0, ro_m2, 0); cv.c(ro_m2, 0, ro_get, 0)
    cv.c(ro_get, 0, ro_la, 0); cv.c(ro_la, 0, ro_rp, 0); cv.c(ro_rp, 0, ro_ex, 0)
    for i in range(10):
        cv.c(ro_ex, i, ro_pk, i)
    cv.c(ro_pk, 0, ro_ts, 0); cv.c(ro_ts, 1, ro_rp, 1); cv.c(ro_ts, 0, s_fin, 1)

    # H = H + S
    hu_t = cv.obj(700, 580, "t b b")
    hu_la = cv.obj(700, 670, "list append")
    hu_ex = cv.obj(980, 400, hadd_expr(), 30)
    hu_pk = cv.obj(980, 700, pack(10))
    cv.c(tb, 0, hu_t, 0); cv.c(hu_t, 1, s_fin, 0); cv.c(s_fin, 0, hu_la, 1)
    cv.c(hu_t, 0, h_b, 0); cv.c(h_b, 0, hu_la, 0); cv.c(hu_la, 0, hu_ex, 0)
    for i in range(10):
        cv.c(hu_ex, i, hu_pk, i)
    cv.c(hu_pk, 0, hd, 0)

    # digest -> base64 -> the 101 response
    by_ex = cv.obj(20, 680, bytes_expr(), 40)
    by_pk = cv.obj(20, 860, pack(21))
    sx_ex = cv.obj(20, 890, sextets_expr(), 60)
    sx_pk = cv.obj(20, 1050, pack(28))
    sx27 = cv.obj(20, 1080 - 25, "list split 27")
    ch_ex = cv.obj(20, 1080, b64chars_expr(), 60)
    ch_pk = cv.obj(20, 1300, pack(28))
    pre = cv.obj(20, 1330, "list prepend " + ascii_codes(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: "), 80)
    end = cv.obj(20, 1420, "list append 13 10 13 10")
    stx = cv.obj(20, 1450, "s $0-tx")
    cv.c(tm, 0, h_c, 0); cv.c(h_c, 0, by_ex, 0)
    for i in range(21):
        cv.c(by_ex, i, by_pk, i)
    cv.c(by_pk, 0, sx_ex, 0)
    for i in range(28):
        cv.c(sx_ex, i, sx_pk, i)
    cv.c(sx_pk, 0, sx27, 0); cv.c(sx27, 0, ch_ex, 0)
    for i in range(28):
        cv.c(ch_ex, i, ch_pk, i)
    cv.c(ch_pk, 0, pre, 0); cv.c(pre, 0, end, 0); cv.c(end, 0, stx, 0)
    return cv


# ---------------------------------------------------------------- [pd websocket]

def ws_expr():
    """Inputs: byte st op len cnt idx m0 m1 m2 m3.
    st: 0 first header byte, 1 second, 2 extended length, 4 mask, 5 payload.
    Outputs: act pbyte st' op' len' cnt' idx' m0' m1' m2' m3'.
    act: 0 nothing, 1 payload byte, 2 last payload byte (dispatch), 3 empty frame (dispatch),
    9 protocol error (unmasked frame or payload > 4096 bytes)."""
    b, st, op, ln, cnt, idx, m0, m1, m2, m3 = (f"$f{i}" for i in range(1, 11))
    l7 = f"({b} & 127)"
    act = (f"if({st} == 1, if(({b} >> 7) == 0, 9, 0), "
           f"if({st} == 2, if({ln}*256 + {b} > 4096, 9, 0), "
           f"if({st} == 4, if({cnt} == 3 && {ln} == 0, 3, 0), "
           f"if({st} == 5, if({idx} + 1 >= {ln}, 2, 1), 0))))")
    pbyte = (f"{b} ^ if({idx} % 4 == 0, {m0}, if({idx} % 4 == 1, {m1}, "
             f"if({idx} % 4 == 2, {m2}, {m3})))")
    st2 = (f"if({st} == 0, 1, if({st} == 1, if({l7} < 126, 4, 2), "
           f"if({st} == 2, if({cnt} <= 1, 4, 2), "
           f"if({st} == 4, if({cnt} >= 3, if({ln} == 0, 0, 5), 4), "
           f"if({st} == 5, if({idx} + 1 >= {ln}, 0, 5), 0)))))")
    op2 = f"if({st} == 0, {b} & 15, {op})"
    ln2 = f"if({st} == 1, if({l7} < 126, {l7}, 0), if({st} == 2, {ln}*256 + {b}, {ln}))"
    cnt2 = (f"if({st} == 1, if({l7} == 126, 2, if({l7} == 127, 8, 0)), "
            f"if({st} == 2, if({cnt} <= 1, 0, {cnt} - 1), if({st} == 4, {cnt} + 1, {cnt})))")
    idx2 = f"if({st} == 5, {idx} + 1, 0)"
    ms = [f"if({st} == 4 && {cnt} == {i}, {b}, {m})" for i, m in enumerate((m0, m1, m2, m3))]
    return expr(act, pbyte, st2, op2, ln2, cnt2, idx2, *ms)


def build_ws():
    cv = Canvas("websocket", (140, 80, 900, 640))
    cv.text(20, 10, "Quadros WebSocket do config-app, byte a byte (do cliente para o servidor vêm "
            "sempre com máscara). Binário (2) -> pacote; ping (9) -> pong; close (8) -> "
            "close e espera o navegador fechar. Texto e fragmentos são ignorados.", 80)
    rb = cv.obj(20, 70, "r $0-ws-byte")
    la = cv.obj(20, 100, "list append 0 0 0 0 0 0 0 0 0")
    ex = cv.obj(20, 130, ws_expr(), 70)
    pk = cv.obj(160, 330, pack(9))
    fop = cv.obj(560, 360, "f")
    fpb = cv.obj(300, 360, "f")
    se = cv.obj(20, 360, "sel 1 2 3 9")
    cv.c(rb, 0, la, 0); cv.c(la, 0, ex, 0)
    for j in range(9):
        cv.c(ex, 2 + j, pk, j)
    cv.c(pk, 0, la, 1); cv.c(ex, 3, fop, 1); cv.c(ex, 1, fpb, 1); cv.c(ex, 0, se, 0)

    ap = cv.msg(300, 420, "append $1")
    pay = cv.obj(300, 450, "list store")
    t2 = cv.obj(110, 390, "t b b")
    disp = cv.obj(20, 450, "t b b b")
    ski = cv.obj(240, 390, "s $0-kick")
    cv.c(se, 0, fpb, 0); cv.c(fpb, 0, ap, 0); cv.c(ap, 0, pay, 0)
    cv.c(se, 1, t2, 0); cv.c(t2, 1, fpb, 0); cv.c(t2, 0, disp, 0)
    cv.c(se, 2, disp, 0); cv.c(se, 3, ski, 0)

    lp = cv.obj(20, 510, "list prepend")
    clr = cv.msg(440, 480, "list")
    rte = cv.obj(20, 540, "route 2 9 8")
    spk = cv.obj(20, 600, "s $0-packet")
    cv.c(disp, 2, fop, 0); cv.c(fop, 0, lp, 1); cv.c(disp, 1, pay, 0); cv.c(pay, 0, lp, 0)
    cv.c(disp, 0, clr, 0); cv.c(clr, 0, pay, 1); cv.c(lp, 0, rte, 0); cv.c(rte, 0, spk, 0)

    tp = cv.obj(140, 570, "t l l")
    pl = cv.obj(220, 600, "list length")
    pp = cv.obj(220, 630, "list prepend 138")
    pong = cv.obj(140, 660, "list prepend")
    stx = cv.obj(140, 690, "s $0-tx")
    cv.c(rte, 1, tp, 0); cv.c(tp, 1, pl, 0); cv.c(pl, 0, pp, 0); cv.c(pp, 0, pong, 1)
    cv.c(tp, 0, pong, 0); cv.c(pong, 0, stx, 0)

    tcl = cv.obj(380, 570, "t b b b")
    cl = cv.msg(500, 600, "136 0")
    stx2 = cv.obj(500, 630, "s $0-tx")
    m2 = cv.msg(440, 600, "2")
    sm = cv.obj(440, 660, "s $0-mode")
    dk = cv.obj(380, 690, "del 1000")
    sk2 = cv.obj(380, 720, "s $0-kick")
    cv.c(rte, 2, tcl, 0); cv.c(tcl, 2, cl, 0); cv.c(cl, 0, stx2, 0)
    cv.c(tcl, 1, m2, 0); cv.c(m2, 0, sm, 0); cv.c(tcl, 0, dk, 0); cv.c(dk, 0, sk2, 0)

    rr = cv.obj(620, 70, "r $0-wsreset")
    tr = cv.obj(620, 100, "t b b b")
    z9 = cv.msg(700, 130, "0 0 0 0 0 0 0 0 0")
    clr2 = cv.msg(660, 160, "list")
    stp = cv.msg(620, 190, "stop")
    cv.c(rr, 0, tr, 0); cv.c(tr, 2, z9, 0); cv.c(z9, 0, la, 1)
    cv.c(tr, 1, clr2, 0); cv.c(clr2, 0, pay, 1); cv.c(tr, 0, stp, 0); cv.c(stp, 0, dk, 0)

    rh = cv.obj(620, 250, "r $0-hs-ok")
    m1 = cv.msg(620, 280, "1")
    sm1 = cv.obj(620, 310, "s $0-mode")
    swr = cv.obj(700, 280, "s $0-wsreset")
    th = cv.obj(620, 220, "t b b")
    cv.c(rh, 0, th, 0)
    # order: reset the parser first, then switch the byte router to WebSocket
    cv.c(th, 1, swr, 0); cv.c(th, 0, m1, 0); cv.c(m1, 0, sm1, 0)
    return cv


# ---------------------------------------------------------------- [pd pacotes]

def i16(lo, hi):
    return f"({lo} + {hi}*256 - if({hi} >= 128, 65536, 0))"


def imu_expr():
    """Input: ImuPacket v2 without its first 2 bytes (25 bytes): packet offset k = $f(k-1).
    Outputs, left to right (Pd fires them right to left):
    roll pitch yaw accX accY accZ breath peakL peakR rmsL rmsR w x y z accZ accY accX audioValid"""
    P = lambda k: f"$f{k - 1}"
    w, x, y, z = (f"({i16(P(k), P(k + 1))}/16384)" for k in (8, 10, 12, 14))
    ax, ay, az = (i16(P(k), P(k + 1)) for k in (16, 18, 20))
    flags = P(22)
    rms_l, rms_r, pk_l, pk_r = (f"({P(k)}/2 - 120)" for k in (23, 24, 25, 26))
    rad = "57.29577951308232"
    # legacy firmware convention: qw=w, qx=y, qy=x, qz=-z (gb_euler_legacy)
    roll = f"atan2(2*({w}*{y} - {x}*{z}), 1 - 2*({y}*{y} + {x}*{x}))*{rad} + 0"
    pitch = f"asin(max(-1, min(1, 2*({w}*{x} + {y}*{z}))))*{rad} + 0"
    yaw = f"atan2(2*({x}*{y} - {w}*{z}), 1 - 2*({x}*{x} + {z}*{z}))*{rad} + 0"
    return expr(roll, pitch, yaw, ax, ay, az, f"(({flags} >> 4) & 1)", pk_l, pk_r, rms_l, rms_r,
                w, x, y, z, az, ay, ax, f"(({flags} >> 3) & 1)")


def co2_expr():
    """Input: Co2Packet without its first 2 bytes, 9 bytes. Outputs: trigger ppm."""
    P = lambda k: f"$f{k - 1}"
    return expr(f"(({P(10)} >> 3) & 1)", f"{P(8)} + {P(9)}*256")


def nav_expr():
    """Input: NavPacket without its first 2 bytes, 20 bytes.
    Outputs: settled bearing speed lon lat valid."""
    P = lambda k: f"$f{k - 1}"
    lat = f"({i16(P(12), P(13))}*65536 + {P(10)} + {P(11)}*256)/10000000"
    lon = f"({i16(P(16), P(17))}*65536 + {P(14)} + {P(15)}*256)/10000000"
    return expr(f"(({P(9)} >> 1) & 1)", f"{i16(P(18), P(19))}/100",
                f"({P(20)} + {P(21)}*256)/10", lon, lat, f"({P(9)} & 1)")


def build_pacotes():
    cv = Canvas("pacotes", (160, 80, 1000, 700))
    cv.text(20, 10, "Pacotes da bola (firmware/docs/protocol.md): movimento 0x01 v2 "
            "(27 bytes), CO2 0x02 v1 (12), navegação 0x07 v1 (23). Bytes a mais "
            "(campos futuros) são ignorados; pacotes curtos ou de outra versão são descartados.", 90)
    rp = cv.obj(20, 60, "r $0-packet")
    tp = cv.obj(20, 90, "t l b")
    spk = cv.obj(110, 120, "s $0-pkt")
    r21 = cv.obj(20, 150, "route 2 1")
    ri = cv.obj(20, 180, "route 1")
    sp = cv.obj(20, 210, "list split 25")
    cv.c(rp, 0, tp, 0); cv.c(tp, 1, spk, 0); cv.c(tp, 0, r21, 0)
    cv.c(r21, 0, ri, 0); cv.c(ri, 0, sp, 0)

    ex = cv.obj(20, 240, imu_expr(), 120)
    y0 = 520
    s_ax = cv.obj(900, y0, "s gaia.acc.x")
    s_ay = cv.obj(800, y0 + 30, "s gaia.acc.y")
    s_az = cv.obj(700, y0 + 60, "s gaia.acc.z")
    qpk = cv.obj(540, y0, "pack f f f f")
    s_q = cv.obj(540, y0 + 30, "s gaia.quat")
    rpk = cv.obj(420, y0, "pack f f")
    rsp = cv.obj(420, y0 + 30, "spigot")
    s_r = cv.obj(420, y0 + 60, "s gaia.mic.rms")
    ppk = cv.obj(300, y0, "pack f f")
    psp = cv.obj(300, y0 + 30, "spigot")
    s_p = cv.obj(300, y0 + 60, "s gaia.mic.peak")
    s_b = cv.obj(200, y0 + 90, "s gaia.breath")
    sens = cv.obj(20, y0, pack(6))
    sla = cv.obj(20, y0 + 30, "list append 0")
    s_s = cv.obj(20, y0 + 60, "s gaia.sensors")
    rco = cv.obj(120, y0 + 0 - 30, "r $0-co2last")
    cv.c(sp, 0, ex, 0)
    cv.c(ex, 18, rsp, 1); cv.c(ex, 18, psp, 1)
    cv.c(ex, 17, s_ax, 0); cv.c(ex, 16, s_ay, 0); cv.c(ex, 15, s_az, 0)
    for i in range(4):
        cv.c(ex, 11 + i, qpk, i)
    cv.c(qpk, 0, s_q, 0)
    cv.c(ex, 9, rpk, 0); cv.c(ex, 10, rpk, 1); cv.c(rpk, 0, rsp, 0); cv.c(rsp, 0, s_r, 0)
    cv.c(ex, 7, ppk, 0); cv.c(ex, 8, ppk, 1); cv.c(ppk, 0, psp, 0); cv.c(psp, 0, s_p, 0)
    cv.c(ex, 6, s_b, 0)
    for i in range(6):
        cv.c(ex, i, sens, i)
    cv.c(sens, 0, sla, 0); cv.c(rco, 0, sla, 1); cv.c(sla, 0, s_s, 0)

    y1 = 680
    r27 = cv.obj(20, y1, "route 2 7")
    cv.c(r21, 1, r27, 0)
    c10 = cv.obj(20, y1 + 30, "list split 10")
    c9 = cv.obj(20, y1 + 60, "list split 9")
    cex = cv.obj(20, y1 + 90, co2_expr(), 40)
    tco = cv.obj(120, y1 + 120, "t f f")
    sco = cv.obj(120, y1 + 180, "s gaia.co2")
    sla2 = cv.obj(200, y1 + 150, "s $0-co2last")
    str_ = cv.obj(20, y1 + 210, "s gaia.co2.trigger")
    cv.c(r27, 0, c10, 0); cv.c(c10, 0, c9, 0); cv.c(c9, 0, cex, 0)
    cv.c(cex, 1, tco, 0); cv.c(tco, 1, sla2, 0); cv.c(tco, 0, sco, 0); cv.c(cex, 0, str_, 0)

    n21 = cv.obj(420, y1 + 30, "list split 21")
    n20 = cv.obj(420, y1 + 60, "list split 20")
    nex = cv.obj(420, y1 + 90, nav_expr(), 70)
    cv.c(r27, 1, n21, 0); cv.c(n21, 0, n20, 0); cv.c(n20, 0, nex, 0)
    names = ["gaia.settled", "gaia.bearing", "gaia.speed", "gaia.lon", "gaia.lat"]
    for i, name in enumerate(names):
        x = 420 + 110 * (4 - i)
        spg = cv.obj(x, y1 + 180, "spigot")
        snd = cv.obj(x, y1 + 210 + 30 * (i % 2), f"s {name}")
        cv.c(nex, 5, spg, 1); cv.c(nex, i, spg, 0); cv.c(spg, 0, snd, 0)
    return cv


# ---------------------------------------------------------------- [pd estado]

def build_estado():
    cv = Canvas("estado", (180, 80, 860, 600))
    cv.text(20, 10, "connected 1 no aperto de mão e quando os pacotes voltam; connected 0 "
            "quando o navegador fecha ou apos 2 s sem pacotes (gaia.breath e "
            "gaia.co2.trigger recebem 0 uma vez). Conectado: a cada 1 s , rate "
            "(pacotes/s) e gap (maior intervalo entre pacotes, ms).", 90)
    ch = cv.obj(20, 80, "change 0")
    tf = cv.obj(20, 110, "t f f f")
    vco = cv.obj(160, 140, "v $0-conn")
    rep = cv.msg(90, 140, "connected $1")
    out = cv.obj(20, 560, "outlet")
    sel = cv.obj(20, 170, "sel 0 1")
    cv.c(ch, 0, tf, 0); cv.c(tf, 2, vco, 0); cv.c(tf, 1, rep, 0); cv.c(rep, 0, out, 0)
    cv.c(tf, 0, sel, 0)

    stat = cv.obj(420, 420, "del 1000")
    wd = cv.obj(260, 80, "del 2000")
    z = cv.msg(260, 110, "0")
    cv.c(wd, 0, z, 0); cv.c(z, 0, ch, 0)

    toff = cv.obj(20, 200, "t b b b b")
    stp1 = cv.msg(20, 230, "stop")
    stp2 = cv.msg(70, 230, "stop")
    zb = cv.msg(120, 260, "0")
    sb = cv.obj(120, 290, "s gaia.breath")
    zt = cv.msg(170, 230, "0")
    st = cv.obj(170, 320, "s gaia.co2.trigger")
    cv.c(sel, 0, toff, 0); cv.c(toff, 3, stp1, 0); cv.c(stp1, 0, stat, 0)
    cv.c(toff, 2, stp2, 0); cv.c(stp2, 0, wd, 0)
    cv.c(toff, 1, zb, 0); cv.c(zb, 0, sb, 0); cv.c(toff, 0, zt, 0); cv.c(zt, 0, st, 0)

    vcnt = cv.obj(560, 200, "v $0-count")
    vgap = cv.obj(660, 200, "v $0-maxgap")
    tm1 = cv.obj(420, 300, "timer")
    tm2 = cv.obj(560, 520, "timer")

    ton = cv.obj(300, 170, "t b b b b")
    z1 = cv.msg(560, 170, "0")
    z2 = cv.msg(660, 170, "0")
    cv.c(sel, 1, ton, 0); cv.c(ton, 3, z1, 0); cv.c(z1, 0, vcnt, 0)
    cv.c(ton, 2, z2, 0); cv.c(z2, 0, vgap, 0)
    cv.c(ton, 1, tm1, 0); cv.c(ton, 1, tm2, 0); cv.c(ton, 0, stat, 0)

    rhs = cv.obj(400, 60, "r $0-hs-ok")
    ths = cv.obj(400, 90, "t b b")
    one = cv.msg(400, 120, "1")
    cv.c(rhs, 0, ths, 0); cv.c(ths, 1, one, 0); cv.c(one, 0, ch, 0); cv.c(ths, 0, wd, 0)

    rdi = cv.obj(500, 60, "r $0-disc")
    zd = cv.msg(500, 90, "0")
    cv.c(rdi, 0, zd, 0); cv.c(zd, 0, ch, 0)

    # each packet: (re)connect or measure the gap, reset timers, count, feed the watchdog
    rpk = cv.obj(420, 220, "r $0-pkt")
    tpk = cv.obj(420, 250, "t b b b b")
    vco2 = cv.obj(520, 280, "v $0-conn")
    s0 = cv.obj(520, 310, "sel 0")
    one2 = cv.msg(520, 340, "1")
    tb_ = cv.obj(580, 340, "t b")
    cv.c(rpk, 0, tpk, 0); cv.c(tpk, 3, vco2, 0); cv.c(vco2, 0, s0, 0)
    cv.c(s0, 0, one2, 0); cv.c(one2, 0, ch, 0); cv.c(s0, 1, tb_, 0); cv.c(tb_, 0, tm1, 1)
    g1 = cv.obj(420, 330, "t f b")
    vg1 = cv.obj(470, 360, "v $0-maxgap")
    mx1 = cv.obj(420, 390, "max")
    vg1s = cv.obj(420, 420 - 30 + 30, "v $0-maxgap")
    cv.c(tm1, 0, g1, 0); cv.c(g1, 1, vg1, 0); cv.c(vg1, 0, mx1, 1); cv.c(g1, 0, mx1, 0)
    cv.c(mx1, 0, vg1s, 0)
    cv.c(tpk, 2, tm1, 0); cv.c(tpk, 2, tm2, 0)
    vinc = cv.obj(700, 280, "v $0-count")
    inc = cv.obj(700, 310, "+ 1")
    cv.c(tpk, 1, vinc, 0); cv.c(vinc, 0, inc, 0); cv.c(inc, 0, vinc, 0)
    cv.c(tpk, 0, wd, 0)

    # every second while connected: rate then gap
    tst = cv.obj(420, 450, "t b b b b b")
    vr = cv.obj(700, 480, "v $0-count")
    rr = cv.msg(700, 510, "rate $1")
    z3 = cv.msg(640, 480, "0")
    vcz = cv.obj(640, 510, "v $0-count")
    g2 = cv.obj(560, 550, "t f b")
    vg2 = cv.obj(610, 580, "v $0-maxgap")
    mx2 = cv.obj(560, 610, "max")
    rnd = cv.obj(560, 640, "expr int($f1 + 0.5)")
    gp = cv.msg(560, 670, "gap $1")
    z4 = cv.msg(420, 480, "0")
    vgz = cv.obj(420, 510, "v $0-maxgap")
    cv.c(stat, 0, tst, 0); cv.c(tst, 4, stat, 0)
    cv.c(tst, 3, vr, 0); cv.c(vr, 0, rr, 0); cv.c(rr, 0, out, 0)
    cv.c(tst, 2, z3, 0); cv.c(z3, 0, vcz, 0)
    cv.c(tst, 1, tm2, 1); cv.c(tm2, 0, g2, 0); cv.c(g2, 1, vg2, 0); cv.c(vg2, 0, mx2, 1)
    cv.c(g2, 0, mx2, 0); cv.c(mx2, 0, rnd, 0); cv.c(rnd, 0, gp, 0); cv.c(gp, 0, out, 0)
    cv.c(tst, 0, z4, 0); cv.c(z4, 0, vgz, 0)
    return cv


# ---------------------------------------------------------------- main canvas

def build_main():
    cv = Canvas(rect=(40, 40, 980, 760), font=12)
    cv.text(20, 12, "GAIABALL NO PD VANILLA - recebe os dados da bola pelo config-app "
            "(sem objetos compilados)", 90)
    cv.text(20, 50, "1. Abra este patch no Pd (vanilla 0.51 ou mais novo). Ele escuta em "
            "ws://127.0.0.1:9478 (outra porta: use como objeto [gaiaball-vanilla 9500] "
            "e mude a porta no Avançado do config-app).", 60)
    cv.text(20, 130, "2. Abra o config-app no Chrome ou Edge e conecte a bola. A chave "
            "'Enviar ao Pd' já vem ligada: em até 2 s o quadrado 'conectado' acende "
            "e os números ao lado começam a mexer.", 60)
    cv.text(20, 210, "3. Nos seus patches use [r gaia.acc.x], [r gaia.co2] etc. "
            "(os mesmos nomes do objeto [gaiaball]). O estado também sai em "
            "[r gaiaball.status].", 60)
    cv.text(20, 280, "Se o console disser 'Address already in use': outro patch (ou o "
            "objeto [gaiaball]) já usa a porta. Feche-o e clique em 'reconectar'.", 60)

    y = 350
    tg = cv.toggle(20, y)
    cv.text(48, y, "conectado")
    a_rate = cv.atom(150, y, 5)
    cv.text(200, y, "pacotes/s")
    a_gap = cv.atom(290, y, 5)
    cv.text(340, y, "maior intervalo (ms)")
    cv.atom(20, y + 40, 6, "$0-gui-port")
    cv.text(80, y + 40, "porta")
    rel = cv.msg(150, y + 40, "reconectar")
    srel = cv.obj(150, y + 70, "s $0-relisten")
    cv.c(rel, 0, srel, 0)

    yy = 470
    cv.sub(20, yy, build_rede())
    cv.sub(20, yy + 30, build_http())
    cv.sub(20, yy + 60, build_sha1())
    cv.sub(110, yy, build_ws())
    est = cv.sub(110, yy + 30, build_estado())
    cv.sub(110, yy + 60, build_pacotes())
    cv.text(220, yy, "<- o servidor (não precisa abrir)", 30)
    ts = cv.obj(20, yy + 110, "t a a")
    rt = cv.obj(110, yy + 140, "route connected rate gap")
    ss = cv.obj(20, yy + 170, "s gaiaball.status")
    outl = cv.obj(20, yy + 200, "outlet")
    cv.text(80, yy + 200, "<- estado, quando usado como objeto", 40)
    cv.c(est, 0, ts, 0); cv.c(ts, 1, rt, 0); cv.c(ts, 0, ss, 0); cv.c(ts, 0, outl, 0)
    cv.c(rt, 0, tg, 0); cv.c(rt, 1, a_rate, 0); cv.c(rt, 2, a_gap, 0)

    # monitor: every channel, live
    x0, y0 = 520, 50
    cv.text(x0, y0 - 30, "MONITOR (os dados chegando agora)")
    scalars = [("gaia.acc.x", "mg"), ("gaia.acc.y", "mg"), ("gaia.acc.z", "mg"),
               ("gaia.breath", "0/1 sopro"), ("gaia.co2", "ppm"),
               ("gaia.co2.trigger", "0/1"), ("gaia.lat", "graus"), ("gaia.lon", "graus"),
               ("gaia.speed", "graus/s"), ("gaia.bearing", "graus"),
               ("gaia.settled", "0/1 parado")]
    for i, (name, unit) in enumerate(scalars):
        cv.atom(x0, y0 + 28 * i, 9, name)
        cv.text(x0 + 90, y0 + 28 * i, f"{name} ({unit})")
    lists = [("gaia.quat", 4, "w x y z"), ("gaia.mic.rms", 2, "L R dBFS"),
             ("gaia.mic.peak", 2, "L R dBFS"),
             ("gaia.sensors", 7, "roll pitch yaw accX accY accZ co2")]
    yl = y0 + 28 * len(scalars) + 20
    for name, n, desc in lists:
        cv.text(x0, yl, f"{name}: {desc}", 60)
        r = cv.obj(x0, yl + 24, f"r {name}")
        u = cv.obj(x0 + 120, yl + 24, "unpack " + " ".join(["f"] * n))
        cv.c(r, 0, u, 0)
        for j in range(n):
            a = cv.atom(x0 + 62 * j, yl + 54, 7)
            cv.c(u, j, a, 0)
        yl += 92
    return cv


def main():
    lines = build_main().render(top=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT} ({len(lines)} lines)")


if __name__ == "__main__":
    main()
