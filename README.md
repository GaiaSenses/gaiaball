# Gaiaball

Firmware, configuration tooling and documentation for the **Gaiaball** — a ~40 cm
physical globe that people spin, blow on and interact with. Its motion and sensor
readings stream over **Bluetooth Low Energy** to the
[GaiaSenses](https://github.com/GaiaSenses/Gaiasenses-web) web site, where a virtual
Earth mirrors the ball in real time and sensor events drive generative audio-visual
compositions (Pure Data).

```
 ┌─────────────────────────── Gaiaball (this repo) ───────────────────────────┐
 │                                                                            │
 │  GY-91 IMU ──┐                                                             │
 │  2× INMP441 ─┤   ESP32 firmware          BLE (binary protocol v1)          │
 │  SC8 CO2 ────┼─▶ FreeRTOS + NimBLE  ───────────────┬──────────────▶ GaiaSenses site
 │  Pot + LEDs ─┘   (firmware/)                       └──────────────▶ config-app
 │                                                             (bench tool, this repo)
 └────────────────────────────────────────────────────────────────────────────┘
```

## Hardware

| Component | Part | Bus / pins (ESP32 DevKitC) |
|---|---|---|
| Orientation | **GY-91 module** — MPU-9250 (accel + gyro) + AK8963 (mag) + BMP280 (barometer, unused) | I²C — SDA 21, SCL 22, addr 0x68 |
| Microphones | 2× INMP441 (stereo, for blow detection) | I²S — WS 25, SCK 26, SD 33 |
| CO₂ | SC8-CO2-5K ("YYS SC8", NDIR, active 1 Hz stream) | UART1 — RX 16, 9600 8N1, listen-only |
| Threshold knob | 50 kΩ potentiometer | ADC — GPIO 34 |
| LEDs | Sound (D2) and CO₂ alert (D1) | GPIO 2 and GPIO 4 — one pin, one function |

The full netlist cross-check against the KiCAD board design lives in
[`firmware/docs/hardware-pinout.md`](firmware/docs/hardware-pinout.md).

## How it works (short version)

- **Orientation.** The MPU-9250 is read at the configured rate (10–200 Hz) and fused
  on the ESP32 with a **Madgwick filter** (6-axis relative mode by default; 9-axis
  with the magnetometer as an option). A gyro-bias tracker adapts only while the
  ball rests, so a stationary globe stays stationary. Mounting calibration and a
  "zero orientation" command align the physical ball with the virtual one.
- **Blow detection.** The mics are band-passed (configurable HP/LP biquads, default
  10–200 Hz) before an RMS peak detector; a detected blow triggers a "virtual CO₂"
  pulse that the site turns into a composition. The raw (unfiltered) audio is also
  streamed, decimated to 2 kHz, so the config-app can show a live spectrum for
  tuning the band.
- **CO₂.** The SC8-CO2-5K streams one checksummed 16-byte frame per second on its
  own. The firmware is strictly listen-only — per the SC8 datasheet, any query
  halts the active stream until the next power cycle — and resynchronizes on the
  frame header after any byte loss.
- **Transport.** Everything leaves the ball as small, versioned, little-endian
  binary packets with sequence numbers and timestamps (23 B orientation packets at
  up to 200 Hz), over a NimBLE GATT server that accepts **two centrals at once** —
  the site and the config-app side by side. Sensitive commands and configuration
  writes require a 4-digit PIN unlock per connection.
- **Configuration.** Every parameter (rates, filters, thresholds, pins, names) is a
  TLV register, editable live from the config-app; applying persists to NVS, so
  settings survive power cycles.

The wire format — packets, TLV registry, commands — is specified in
[`firmware/docs/protocol.md`](firmware/docs/protocol.md). The task/queue
architecture, including a per-core execution diagram, is in
[`firmware/docs/architecture.md`](firmware/docs/architecture.md).

## Repository map

| Path | What it is |
|---|---|
| `firmware/` | **The v2 firmware** (ESP32, FreeRTOS, NimBLE, binary protocol). Dual toolchain: PlatformIO and Arduino IDE/CLI. See [`firmware/README.md`](firmware/README.md). |
| `config-app/` | **Bench & configuration web app** (Vite + TypeScript, Web Bluetooth): 3D orientation view, per-sensor real-time charts with a shared time axis, audio spectrum analyzer, stream metrics, full parameter editor with presets. See [`config-app/README.md`](config-app/README.md). |
| `pd-gaiaball/` | **Pure Data object `[gaiaball]`** (vanilla abstraction + cross-platform BLE→UDP bridge in Python/bleak): read-only access to every Gaiaball reading from inside Pd on Windows, Linux and macOS. See [`pd-gaiaball/README.md`](pd-gaiaball/README.md). |
| `docs/bench-validation.md` | Step-by-step hardware validation script (flash, rates, loss, filters, CO₂, 24 h soak). |
| `relatorio/index.html` | Interactive report (pt-BR): every defect found in the legacy firmware, the fix, the expected impact, and the bench checklist. Open it in a browser. |
| `firmware-original.ino` | The legacy (v1) firmware, **preserved untouched as a historical reference**; no longer built. |
| `relatorio.md` | The original third-party analysis report (reference). |
| `docs/superpowers/` | Design spec and implementation plan the v2 was built from. |

## Quickstart

### Firmware

```sh
# from the repository root
pio test -d firmware -e native                 # host-side unit tests (no hardware)
pio run  -d firmware -e esp32dev -t upload     # build + flash
pio device monitor -d firmware -e esp32dev     # serial console, 115200 baud
```

Arduino CLI alternative:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball
```

Toolchain versions are pinned (ESP32 Arduino core 2.0.17, NimBLE-Arduino 1.4.3);
details and the full pinout are in [`firmware/README.md`](firmware/README.md).

### Config-app

```sh
cd config-app
npm install
npm run dev        # http://localhost:5173 — Chrome/Edge only (Web Bluetooth)
npm test           # vitest
npm run build      # static bundle in dist/
```

Add `?demo=1` to the URL to exercise every panel with synthetic data, no device
needed. With a real ball: connect, unlock with the PIN (factory default `1234`),
read the configuration, and follow the bench script in
[`docs/bench-validation.md`](docs/bench-validation.md).

## Deploying with GaiaSenses (order matters)

The site needs the `feature/gaiaball-binary-protocol` branch of `Gaiasenses-web`,
which adds the binary-protocol decoder (with a JSON fallback for the legacy
firmware).

| Combination | Works? |
|---|---|
| New site + old firmware | ✅ yes — the decoder falls back to legacy JSON |
| New site + new firmware | ✅ yes |
| Old site + new firmware | ❌ **no** — the old site only parses JSON |

**Deploy the site first (or simultaneously), then flash the v2 firmware.**

## Testing

| Suite | Where | Command |
|---|---|---|
| Firmware logic (packets, TLV, fusion, filters, SC8 framing, DSP) | `firmware/test/` | `pio test -d firmware -e native` |
| Config-app (decoders, FFT, Euler convention, presets) | `config-app/src/**/*.test.ts` | `npm test` |
| Site decoder (same golden vectors) | `Gaiasenses-web/tests/` | `npm test` in that repo |

The three test suites share verbatim golden byte vectors from
`firmware/docs/protocol.md`, so the wire contract is pinned across C++, the
config-app and the site.
