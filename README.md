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
 │  IMU ────────┐                                                             │
 │  2× INMP441 ─┤   ESP32 firmware          BLE (binary protocol)             │
 │  SC8 CO2 ────┼─▶ FreeRTOS + NimBLE  ───────────────┬──────────────▶ GaiaSenses site
 │  Pot + LEDs ─┘   (firmware/)                       └──────────────▶ config-app
 │                                                             (bench tool, this repo)
 └────────────────────────────────────────────────────────────────────────────┘
```

## Hardware

| Component | Part | Bus / pins (ESP32 DevKitC) |
|---|---|---|
| Orientation | **GY-ICM20948V2** (ICM-20948 + AK09916; the ball in hand) or **GY-91** (MPU-9250 + AK8963 + BMP280, unused; future units) — chosen in the config-app, default off | I²C — SDA 21, SCL 22, addr 0x68 |
| Microphones | 2× INMP441 (stereo, for blow detection) | I²S — WS 25, SCK 26, SD 33 |
| CO₂ | SC8-CO2-5K ("YYS SC8", NDIR, active 1 Hz stream) | UART1 — RX 16, 9600 8N1, listen-only |
| Threshold knob | 50 kΩ potentiometer | ADC — GPIO 34 |
| LEDs | Sound (D2) and CO₂ alert (D1) | GPIO 2 and GPIO 4 — one pin, one function |

The full netlist cross-check against the KiCAD board design lives in
[`firmware/docs/hardware-pinout.md`](firmware/docs/hardware-pinout.md).

## How it works (short version)

- **Orientation.** The selected IMU (ICM-20948 or MPU-9250) is read at the configured rate (10–200 Hz) and fused
  on the ESP32 with a **Madgwick filter** (6-axis relative mode; the firmware still has a 9-axis
  magnetometer mode, but the config-app no longer offers it). A gyro-bias tracker adapts only while the
  ball rests, so a stationary globe stays stationary. Mounting calibration and a
  "zero orientation" command align the physical ball with the virtual one.
- **Blow and breath detection.** The mics are band-passed (configurable HP/LP biquads,
  default 10–200 Hz) before the detector. The **breath detector** is selectable:
  *absolute* (a fixed line) or *adaptive* (the level must rise above a slow baseline),
  measuring RMS or peak. Its sensitivity comes from the potentiometer (−60…0 dBFS) or
  from a fixed dBFS value, and it re-arms once the level falls a whole number of dB
  below the line (TLVs `0x2B`–`0x2E`). A recognized breath sets the **breath bit** (flags
  bit 4) of the next orientation packet, and a detected blow also triggers a "virtual
  CO₂" pulse that the site turns into a composition. The raw (unfiltered) audio is also
  available as an **opt-in** stream (TLV `0x1F`, off by default), decimated to 2 kHz,
  so the config-app can show a live spectrum for tuning the band. What the application
  uses arrives on every orientation packet instead: the measured RMS and peak per
  channel.
- **Globe navigation.** The ball itself computes where the virtual globe points: in
  **mirror** mode the globe is the ball's orientation, in **joystick** mode the tilt of
  the ball steers the globe at a speed set by the tilt angle, and a moving/settled state
  machine with hysteresis tells the site when it has stopped. The result (centre
  latitude/longitude, Mapbox bearing, speed, flags, anchor count) leaves the ball as a
  23-byte NavPacket on its own BLE characteristic, so the site only has to draw it. On
  connect the site sends its current map view (command `0x17`, no PIN, RAM only) and the
  ball adopts it as the navigation reference, so the map does not jump. Frames,
  laws and parameters are in [`firmware/docs/protocol.md`](firmware/docs/protocol.md).
- **CO₂.** The SC8-CO2-5K streams one checksummed 16-byte frame per second on its
  own. The firmware is strictly listen-only — per the SC8 datasheet, any query
  halts the active stream until the next power cycle — and resynchronizes on the
  frame header after any byte loss.
- **Transport.** Everything leaves the ball as small, versioned, little-endian
  binary packets with sequence numbers and timestamps (27 B orientation packets, version 2,
  the only packet at that version: `int16` acceleration in milli-g, the microphone
  RMS and peak as `u8` half-dB steps — dBFS = v/2 − 120 — and the breath bit, at up to
  200 Hz),
  over a NimBLE GATT server that accepts **two centrals at once** —
  the site and the config-app side by side. Sensitive commands and configuration
  writes require a 4-digit PIN unlock per connection (the `0x17` map anchor is the one
  command that does not).
- **Configuration.** Every parameter (rates, filters, thresholds, pins, names) is a
  TLV register, editable live from the config-app; applying persists to NVS, so
  settings survive power cycles.

The wire format — packets, TLV registry, commands — is specified in
[`firmware/docs/protocol.md`](firmware/docs/protocol.md). For readers new to BLE (artists,
students, makers) and for engineers integrating another device, the same protocol is
explained step by step in Portuguese in [`docs/protocolo-ble.md`](docs/protocolo-ble.md):
BLE basics, why binary instead of JSON, every packet byte by byte, globe navigation, the
PIN and the TLV configuration, runnable JavaScript, Python and C clients, and the golden
vectors. The task/queue architecture, including a per-core execution diagram, is in
[`firmware/docs/architecture.md`](firmware/docs/architecture.md).

## Repository map

| Path | What it is |
|---|---|
| `firmware/` | **The v2 firmware** (ESP32, FreeRTOS, NimBLE, binary protocol). Dual toolchain: PlatformIO and Arduino IDE/CLI. See [`firmware/README.md`](firmware/README.md). |
| `config-app/` | **Bench & configuration web app** (Vite + TypeScript, Web Bluetooth): 3D orientation view with CAD-style camera views, real-time charts of every packet (fixed Y ranges, 5/10/30/60 s window, breath pulse row), audio spectrum analyzer (linear/log frequency, band-pass shading), stream metrics, full parameter editor (globe navigation, breath detector) with presets, forwarding to Pure Data, and a stage mode for performances. Provisional published address: <https://gaiasenses.github.io/gaiaball/>. See [`config-app/README.md`](config-app/README.md). |
| `pd-gaiaball/` | **Pure Data external `[gaiaball]`** (C, pd-lib-builder, installable from Deken; maintainers publish it with [`PUBLICAR-NO-DEKEN.md`](pd-gaiaball/PUBLICAR-NO-DEKEN.md)): the config-app forwards the ball's packets over a local WebSocket (`ws://127.0.0.1:9478`) and the object publishes them as `[r gaia.*]` channels, read-only, on Windows, Linux and macOS. There is no Python bridge any more. Audio arrives as the treated envelope (per-channel RMS and peak, at the orientation rate), never as samples — the spectrum stays in the browser. See [`pd-gaiaball/README.md`](pd-gaiaball/README.md). |
| `.github/workflows/` | CI: `pd-gaiaball.yml` (multi-OS build of the external + Deken package) and `config-app-pages.yml` (publishes the config-app to GitHub Pages). `config-app-pages.yml` runs **on every push to `main` that touches `config-app/`** (and by hand); `pd-gaiaball.yml` runs **only by hand** (`workflow_dispatch`) for now. |
| `LICENSE` | GNU GPL v3 (see [License](#license)). |
| `docs/protocolo-ble.md` | **Didactic BLE protocol guide** (pt-BR): BLE basics, every packet and TLV register, navigation, runnable JavaScript/Python/C clients and golden vectors. An identical copy lives in `Gaiasenses-web` as `docs/gaiaball-protocolo-ble.md`; this one is the reference. See [`docs/protocolo-ble.md`](docs/protocolo-ble.md). |
| `docs/bench-validation.md` | Step-by-step hardware validation script (flash, rates, loss, filters, CO₂, 24 h soak). |
| `relatorio/index.html` | Interactive report (pt-BR): every defect found in the legacy firmware, the fix, the expected impact, and the bench checklist. Open it in a browser. |
| `firmware-original.ino` | The legacy (v1) firmware, **preserved untouched as a historical reference**; no longer built. |
| `relatorio.md` | The original third-party analysis report (reference). |
| `docs/code-review-v2.md` | Code review of the v2 firmware and config-app (pt-BR), a snapshot from the time of the review. |
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
node e2e-smoke.mjs # smoke test of the built app
```

Add `?demo=1` to the URL to exercise every panel with synthetic data, no device
needed. With a real ball: connect, unlock with the PIN (factory default `1234`),
read the configuration, and follow the bench script in
[`docs/bench-validation.md`](docs/bench-validation.md).

## Deploying with GaiaSenses (order matters)

The current GaiaSenses site reads **only** the binary protocol (there is no JSON
fallback any more) and takes the globe pose and the CO₂ trigger from the ball: it needs
characteristic `…0006` (NavPacket) to move the globe and the Co2Packet `trigger` bit to
open compositions.

| Combination | Works? |
|---|---|
| Current site + navigation firmware | ✅ yes |
| Current site + v2 firmware older than the navigation build | ⚠️ partly — orientation and CO₂ ppm arrive, but there is no characteristic `…0006` (the globe does not move; the site shows a warning) and the CO₂ `trigger` bit is never set, so CO₂-triggered compositions silently stop opening |
| Current site + legacy firmware (`firmware-original.ino`, JSON) | ❌ **no** — the site drops the JSON notifications |
| A site that still parses only JSON + any v2 firmware | ❌ **no** — that site cannot read the binary packets |

The motion packet is now IMU **v2** (acceleration in mg, audio in half-dB, breath bit),
and it breaks both orders: the site published before the balls rejects v2 (it requires
version 1 and at least 31 bytes), and the new site rejects v1 (the IMU goes silent; the
site shows an "update the firmware" warning).

| Combination | Works? |
|---|---|
| Old site (IMU v1 only) + new firmware (IMU v2) | ❌ the site drops every IMU packet |
| New site (IMU v2 only) + old firmware (IMU v1) | ❌ the IMU is silently ignored |
| New site + new firmware | ✅ yes |

**Flash the balls and publish the site in the same window.** The site's globe patch is
now `mapa15` ("TesteGlobo2.0"), which replaced paraisoGaia43. Its acceleration
constants, written for the old raw counts (1 g = 8192), were converted to mg
(1 g = 1000), the unit `gaia.acc.*` and `gaia.sensors` now arrive in, so it reacts to
movement as before (the table is in `Gaiasenses-web/docs/musico/README.md`, "Nota
para quem mantém o patch do globo").

## Testing

| Suite | Where | Command |
|---|---|---|
| Firmware logic (packets, TLV, fusion, filters, SC8 framing, DSP) | `firmware/test/` | `pio test -d firmware -e native` |
| Config-app (decoders, FFT, Euler convention, presets) | `config-app/src/**/*.test.ts` | `cd config-app && npm test` |
| Config-app build and smoke test | `config-app/e2e-smoke.mjs` | `cd config-app && npm run build && node e2e-smoke.mjs` |
| Firmware build (no hardware) | `firmware/` | `pio run -d firmware -e esp32dev` |
| Site decoder (same golden vectors) | `Gaiasenses-web/tests/` | `npm test` in that repo |
| `[gaiaball]` decoder and WebSocket core (golden vectors, RFC 6455 handshake and frames) | `pd-gaiaball/tests/` | `make -C pd-gaiaball test` |

The firmware, config-app, site and Pd-external suites share verbatim golden byte vectors from
`firmware/docs/protocol.md`, so the wire contract is pinned across the firmware, the
config-app, the site and the Pd external.

## License

Released under the **GNU General Public License v3.0**; see [`LICENSE`](LICENSE).
