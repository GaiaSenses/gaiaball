#!/usr/bin/env python3
# Gaiaball -> Pure Data bridge.
#
# Connects to the Gaiaball over BLE (read-only: it subscribes to the three
# notify characteristics and NEVER writes to the ball), decodes the binary
# wire protocol v1 (firmware/docs/protocol.md) and forwards every reading to
# Pure Data as FUDI text messages over UDP (consumed by the [gaiaball]
# abstraction via [netreceive -u]).
#
# Audio reaches Pd as TREATED VARIABLES, never as samples: the "micenv" message
# carries the per-channel RMS and peak in dBFS, measured by the firmware over a
# 20 ms window (peak sampled at the full 16 kHz) and delivered at the
# orientation rate. The raw 2 kHz stream (packet 0x05) is deliberately not
# decoded here -- a Pd patch wants a control signal, not a waveform, and the
# spectrum analysis (FFT) lives in the browser-based config-app.
#
# Cross-platform: Windows 10+, macOS 10.15+, Linux (BlueZ >= 5.43), via bleak.
# Requires Python >= 3.9 and `pip install bleak`.
#
# The Euler conversion reproduces the legacy convention byte-for-byte
# (firmware/Gaiaball/src/logic/QuatMath.cpp quatToEulerLegacy, the same one the
# GaiaSenses site feeds to the Pd patch): remap qw=w, qx=y, qy=x, qz=-z, then
# aerospace formulas, in degrees.

import argparse
import asyncio
import math
import socket
import struct
import sys
import time

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover
    print("erro: biblioteca 'bleak' ausente. Instale com:  pip install bleak",
          file=sys.stderr)
    sys.exit(1)

VERSION = "1.0.0"

SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214"
CHAR_IMU = "19b10001-e8f2-537e-4f6c-d104768a1214"   # notify: ImuPacket (0x01)
CHAR_CO2 = "19b10003-e8f2-537e-4f6c-d104768a1214"   # notify: Co2Packet (0x02)
CHAR_DIAG = "19b10005-e8f2-537e-4f6c-d104768a1214"  # notify: Diag/Sensors (0x03/0x04)

PROTOCOL_VERSION = 1
QUAT_SCALE = 16384.0          # int16 Q14
AUDIO_FULL_SCALE = 1 << 23    # 24-bit counts (32-bit I2S frame >> 8)
RAD_TO_DEG = 180.0 / math.pi


def euler_legacy(w: float, x: float, y: float, z: float):
    """Legacy Euler convention in degrees (same output the site hands to Pd)."""
    qw, qx, qy, qz = w, y, x, -z
    sinp = 2.0 * (qw * qy - qx * qz)
    sinp = max(-1.0, min(1.0, sinp))
    roll = math.atan2(2.0 * (qw * qx + qy * qz),
                      1.0 - 2.0 * (qx * qx + qy * qy)) * RAD_TO_DEG
    pitch = math.asin(sinp) * RAD_TO_DEG
    yaw = math.atan2(2.0 * (qw * qz + qx * qy),
                     1.0 - 2.0 * (qy * qy + qz * qz)) * RAD_TO_DEG
    return roll, pitch, yaw


def dbfs(rms: float) -> float:
    """AES17 dBFS (0 dBFS = full-scale sine) from an RMS in 24-bit counts."""
    if rms <= 0.0:
        return -120.0
    return max(-120.0, 20.0 * math.log10(rms * math.sqrt(2.0) / AUDIO_FULL_SCALE))


def fudi_symbol(text: str) -> str:
    """A string safe to embed in a FUDI message (one atom, no separators)."""
    out = "".join(c if c.isalnum() or c in "-_." else "_" for c in text.strip())
    return out or "unknown"


class Fudi:
    """UDP sender of FUDI datagrams (ASCII messages terminated by ';')."""

    def __init__(self, host: str, port: int):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, text: str) -> None:
        try:
            self.sock.sendto(text.encode("ascii", "replace"), self.addr)
        except OSError:
            pass  # Pd not listening yet; keep running


class Decoder:
    """Decodes Gaiaball packets and emits FUDI messages."""

    def __init__(self, fudi: Fudi, imu_div: int, verbose: bool):
        self.fudi = fudi
        self.imu_div = max(1, imu_div)
        self.verbose = verbose
        self.imu_count = 0
        self.counts = {"imu": 0, "co2": 0, "sensors": 0, "diag": 0}

    def on_imu(self, data: bytes) -> None:
        if len(data) < 31 or data[0] != PROTOCOL_VERSION or data[1] != 0x01:
            return
        self.counts["imu"] += 1
        self.imu_count += 1
        if self.imu_count % self.imu_div:
            return
        qw, qx, qy, qz, ax, ay, az = struct.unpack_from("<7h", data, 8)
        w, x, y, z = (qw / QUAT_SCALE, qx / QUAT_SCALE,
                      qy / QUAT_SCALE, qz / QUAT_SCALE)
        roll, pitch, yaw = euler_legacy(w, x, y, z)
        msg = (f"euler {roll:.2f} {pitch:.2f} {yaw:.2f};\n"
               f"quat {w:.5f} {x:.5f} {y:.5f} {z:.5f};\n"
               f"accel {ax} {ay} {az};\n")
        # Microphone envelope measured by the firmware over the 20 ms window that
        # ended with this sample (RMS in AES17 dBFS, peak in amplitude dBFS). It
        # arrives at the orientation rate, so it is the signal to use for breath
        # control -- the 5 Hz "mics" message is only a slow readout.
        if data[22] & 0x08:
            rms_l, rms_r, pk_l, pk_r = struct.unpack_from("<4h", data, 23)
            msg += ("micenv {:.2f} {:.2f} {:.2f} {:.2f};\n".format(
                rms_l / 100.0, rms_r / 100.0, pk_l / 100.0, pk_r / 100.0))
        self.fudi.send(msg)

    def on_co2(self, data: bytes) -> None:
        if len(data) < 12 or data[0] != PROTOCOL_VERSION or data[1] != 0x02:
            return
        self.counts["co2"] += 1
        ppm, flags, age_s = struct.unpack_from("<HBB", data, 8)
        valid = flags & 1
        stale = (flags >> 1) & 1
        virtual = (flags >> 2) & 1
        self.fudi.send(f"co2 {ppm} {valid} {stale} {virtual} {age_s};\n")

    def on_diag_char(self, data: bytes) -> None:
        """The diag characteristic multiplexes several packet types.

        Only 0x03 (diagnostics) and 0x04 (live levels) are forwarded. The raw
        audio stream (0x05) and the magnetometer capture (0x06) are ignored on
        purpose: Pd gets the TREATED audio variables (RMS and peak, in the
        "micenv" message, at the orientation rate) and never the samples. The
        spectrum belongs to the browser, which is where the FFT runs.
        """
        if len(data) < 2 or data[0] != PROTOCOL_VERSION:
            return
        ptype = data[1]
        if ptype == 0x03:
            self._diag(data)
        elif ptype == 0x04:
            self._sensors(data)

    def _diag(self, data: bytes) -> None:
        if len(data) < 23:
            return
        self.counts["diag"] += 1
        (uptime_s, imu_hz10, notify_hz10, imu_err, i2s_err, uart_err,
         heap_kb, min_heap_kb) = struct.unpack_from("<I7H", data, 4)
        flags = data[22]
        self.fudi.send(
            "diag {} {:.1f} {:.1f} {} {} {} {} {} {} {} {};\n".format(
                uptime_s, imu_hz10 / 10.0, notify_hz10 / 10.0, imu_err,
                i2s_err, uart_err, heap_kb, min_heap_kb,
                flags & 1, (flags >> 1) & 1, (flags >> 2) & 1))

    def _sensors(self, data: bytes) -> None:
        if len(data) < 23:
            return
        self.counts["sensors"] += 1
        rms_l, rms_r, threshold = struct.unpack_from("<3I", data, 8)
        (pot_raw,) = struct.unpack_from("<H", data, 20)
        flags = data[22]
        filtered = (flags >> 3) & 1
        controls = (flags >> 2) & 1
        percent = pot_raw / 4095.0 * 100.0
        self.fudi.send(
            f"mics {dbfs(rms_l):.1f} {dbfs(rms_r):.1f} {rms_l} {rms_r} {filtered};\n"
            f"pot {percent:.1f} {pot_raw} {threshold} {controls};\n")

    def report(self) -> str:
        line = "  ".join(f"{k}:{v}" for k, v in self.counts.items())
        for k in self.counts:
            self.counts[k] = 0
        return line


async def find_device(args):
    if args.address:
        return await BleakScanner.find_device_by_address(args.address,
                                                         timeout=args.timeout)

    def match(device, adv) -> bool:
        if device.name and device.name == args.name:
            return True
        return SERVICE_UUID in (adv.service_uuids or [])

    return await BleakScanner.find_device_by_filter(match, timeout=args.timeout)


async def list_devices(timeout: float) -> None:
    print(f"procurando dispositivos BLE por {timeout:.0f} s...")
    devices = await BleakScanner.discover(timeout=timeout,
                                          return_adv=True)
    for device, adv in devices.values():
        mark = "  <- Gaiaball" if SERVICE_UUID in (adv.service_uuids or []) else ""
        print(f"  {device.address}  rssi {adv.rssi:>4}  {device.name or '?'}{mark}")


async def run(args) -> None:
    fudi = Fudi(args.host, args.port)
    dec = Decoder(fudi, args.imu_div, args.verbose)
    fudi.send(f"status bridge {VERSION};\n")
    print(f"[gaiaball-bridge {VERSION}] FUDI/UDP -> {args.host}:{args.port}"
          f"  (alvo: {args.address or args.name})")

    while True:
        fudi.send("status scanning;\n")
        print("procurando a bola...")
        try:
            device = await find_device(args)
        except Exception as e:
            print(f"erro no scan BLE: {e}", file=sys.stderr)
            device = None
        if device is None:
            await asyncio.sleep(2.0)
            continue

        disconnected = asyncio.Event()
        loop = asyncio.get_running_loop()

        def on_disconnect(_client) -> None:
            loop.call_soon_threadsafe(disconnected.set)

        try:
            async with BleakClient(device,
                                   disconnected_callback=on_disconnect) as client:
                await client.start_notify(CHAR_IMU,
                                          lambda _c, d: dec.on_imu(bytes(d)))
                await client.start_notify(CHAR_CO2,
                                          lambda _c, d: dec.on_co2(bytes(d)))
                await client.start_notify(CHAR_DIAG,
                                          lambda _c, d: dec.on_diag_char(bytes(d)))
                name = fudi_symbol(device.name or device.address)
                fudi.send(f"status connected {name};\n")
                print(f"conectado: {device.name or '?'} ({device.address})")
                while not disconnected.is_set():
                    try:
                        await asyncio.wait_for(disconnected.wait(), timeout=5.0)
                    except asyncio.TimeoutError:
                        if args.verbose:
                            print(f"  pacotes/5s  {dec.report()}")
        except Exception as e:
            print(f"erro BLE: {e}", file=sys.stderr)

        fudi.send("status disconnected;\n")
        print("desconectado; tentando de novo em 1 s")
        await asyncio.sleep(1.0)


def main() -> None:
    p = argparse.ArgumentParser(
        description="Ponte Gaiaball -> Pure Data (BLE -> UDP/FUDI). Somente "
                    "leitura: nunca escreve na bola.")
    p.add_argument("--name", default="Globo_1",
                   help="nome BLE da bola (padrao: Globo_1)")
    p.add_argument("--address", default=None,
                   help="endereco BLE (MAC/UUID); ignora --name")
    p.add_argument("--host", default="127.0.0.1",
                   help="host UDP do Pd (padrao: 127.0.0.1)")
    p.add_argument("--port", type=int, default=9478,
                   help="porta UDP do Pd (padrao: 9478, a mesma do [gaiaball])")
    p.add_argument("--imu-div", type=int, default=1, metavar="N",
                   help="encaminha 1 a cada N pacotes de IMU (padrao: 1 = todos)")
    p.add_argument("--timeout", type=float, default=10.0,
                   help="timeout de scan em segundos (padrao: 10)")
    p.add_argument("--list", action="store_true",
                   help="so lista os dispositivos BLE visiveis e sai")
    p.add_argument("-v", "--verbose", action="store_true",
                   help="imprime contadores de pacotes a cada 5 s")
    args = p.parse_args()

    try:
        if args.list:
            asyncio.run(list_devices(args.timeout))
        else:
            asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nencerrado.")


if __name__ == "__main__":
    main()
