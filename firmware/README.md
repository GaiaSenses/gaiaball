# Gaiaball v2 firmware

ESP32 firmware for the Gaiaball: ICM-20948 orientation (DMP quaternions), stereo
INMP441 sound-peak detection, UART CO2 sensor with a sound-triggered virtual CO2 pulse,
two status LEDs, and a NimBLE GATT server streaming binary packets to up to two
centrals.

- Wire protocol (UUIDs, packets, opcodes, config TLV): [`docs/protocol.md`](docs/protocol.md)
- Tasks, cores, priorities, data flow: [`docs/architecture.md`](docs/architecture.md)

## Layout

```
firmware/
  platformio.ini        esp32dev (firmware) + native (unit tests) environments
  Gaiaball/             Arduino sketch folder (also the PlatformIO src_dir)
    Gaiaball.ino        setup(){ app.begin(); }  loop(){ park }
    src/App.*           application core: boot order + App task
    src/services/       hardware/RTOS services (BLE, IMU, audio, CO2, pot, LEDs, NVS, diag)
    src/logic/          pure logic, host-tested
    src/vendor/         SparkFun ICM-20948 v1.3.1, vendored with DMP enabled
  test/                 Unity tests for src/logic (native)
  docs/
```

## Build and flash

Toolchain versions are pinned: **ESP32 Arduino core 2.0.17** and **NimBLE-Arduino
1.4.3**. NimBLE 2.x and core 3.x are not supported (different APIs). The SparkFun
ICM-20948 library is vendored under `src/vendor/` with the DMP already enabled: do
**not** install it from the Library Manager and do **not** edit any library header.

### PlatformIO (recommended)

From the repository root:

```sh
pio run -d firmware -e esp32dev                       # build
pio run -d firmware -e esp32dev -t upload             # build + flash (auto-detects the port)
pio run -d firmware -e esp32dev -t upload --upload-port /dev/ttyUSB0
pio device monitor -d firmware -e esp32dev            # serial log, 115200 baud
pio test -d firmware -e native                        # host unit tests (66 cases)
```

`platformio.ini` pins `espressif32@6.12.0` (Arduino core 2.0.17) and
`h2zero/NimBLE-Arduino@1.4.3`; both are downloaded automatically.

### Arduino IDE 2.x

1. **Boards Manager**: install **esp32 by Espressif Systems, version 2.0.17**
   (Boards Manager URL
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`).
2. **Library Manager**: install **NimBLE-Arduino by h2zero, version 1.4.3** (pick 1.4.3
   in the version dropdown, not the latest 2.x).
3. Open `firmware/Gaiaball/Gaiaball.ino`. The IDE compiles `src/` recursively; no other
   library is needed.
4. Board: **ESP32 Dev Module** (defaults are fine). Select the port, then Upload.
5. Serial Monitor at **115200** baud.

### arduino-cli

```sh
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib install NimBLE-Arduino@1.4.3
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Gaiaball
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/Gaiaball
```

### Resource usage (final build, 2026-11-06)

| Toolchain | Flash (of 1 310 720 B) | Static RAM (of 327 680 B) |
|---|---|---|
| PlatformIO `esp32dev` | 695 625 B (53.1 %) | 39 028 B (11.9 %) |
| arduino-cli `esp32:esp32:esp32` | 684 709 B (52 %) | 38 916 B (11 %) |

Known build warnings (pre-existing, harmless, from the legacy IDF drivers kept on
purpose for core 2.0.17): `'I2S_COMM_FORMAT_I2S' is deprecated` (AudioService) and
`'ADC_ATTEN_DB_11' is deprecated` (PotService).

## Pinout

Verified against the KiCAD board design (`Globo_GaiaSenses_PCi`); the full netlist
cross-check lives in [docs/hardware-pinout.md](docs/hardware-pinout.md).

| Function | GPIO | Notes |
|---|---|---|
| IMU I2C SDA | 21 | ICM-20948, 400 kHz, AD0 low (board header U1; NCS unconnected → I2C) |
| IMU I2C SCL | 22 | |
| I2S WS (LRCLK) | 25 | two INMP441 (L/R), 16 kHz, 32-bit stereo (board M1/M2) |
| I2S SCK (BCLK) | 26 | |
| I2S SD (data in) | 33 | |
| CO2 UART RX | 16 | UART1, 9600 8N1 (Senseair S8 TXD → 16) |
| CO2 UART TX | 17 | (GPIO17 → S8 RXD; required — the S8 is polled over Modbus) |
| Threshold potentiometer | 34 | ADC1 channel 6, 11 dB (board RV1 via R3) |
| Sound LED | 2 | board LED D2 ("LED mic"); config TLV `0x30` (reboot to apply). Strapping pin, held LOW at boot |
| CO2 alert LED | 4 | board LED D1 ("LED co2"); config TLV `0x31` (reboot to apply) |
| Status LED (BLE command) | 23 | **NEW — physical board change required** (see below); config TLV `0x33` (reboot to apply) |

### ⚠️ Physical board change: status LED on GPIO23

The legacy firmware drove **three different functions on GPIO2** (BLE LED command,
sound LED, and a bogus SPI chip-select): a design error — one pin must carry one
function. The board has only two LEDs (D2 = GPIO2, D1 = GPIO4), so the BLE-controlled
LED (command `0x00`/`0x01`, used by the Gaiasenses site) gets its **own, new LED**:

- **Add to the board: GPIO23 → 220 Ω resistor → LED anode; LED cathode → GND**
  (same topology as D1/D2 on the KiCAD design).
- GPIO23 is free on the board (header J3 pin 2, unconnected in the netlist) and has
  no strapping role.
- Until the LED is added, command `0x00`/`0x01` still works — it just drives an
  unconnected pin. The sound and CO2 LEDs are unaffected.
- The three LED pins are enforced distinct: a config write that lands two LED
  functions on the same GPIO is rejected atomically.

## Runtime behavior

- Advertises as **`Globo_1`** (config TLV `0x40`, applied after reboot); accepts two
  simultaneous centrals.
- IMU packets at `imuNotifyHz` (default 50 Hz), CO2 packets on every ppm/flag change plus
  a 1 Hz keepalive, diagnostics at 1 Hz. Notifications are only sent to subscribers.
- Sound LED (GPIO2): 100 ms flash per detected sound peak, held on while the virtual CO2
  pulse is active. Nothing else drives it.
- CO2 LED (GPIO4): on while the real (non-stale) reading exceeds `co2AlertPpm` (default 2000).
- Status LED (GPIO23, new): driven only by the legacy BLE command `0x01`/`0x00`.
  Subsystem failures are reported in the diagnostics packet (config-app), not on an LED.
- CO2 sensor: **Senseair S8** (per the KiCAD board), polled over Modbus every 2 s
  (`FE 04 00 03 00 01 D5 C5`); TLV `0x27` selects the legacy passive 0x42 0x4D frame
  parser instead, for bench experiments with other sensors.
- A sound peak triggers the virtual CO2 pulse: after `virtualDelayMs` (1 s) the reported
  ppm becomes `virtualPeakPpm` (2100) for `virtualHoldMs` (17 s).

## PIN and commands

Sensitive operations require a per-connection unlock with a 4-digit PIN. **Default PIN:
`1234`.** Unlock by writing `0x20` followed by the ASCII PIN to the command
characteristic (`19b10002-…`), e.g. `20 31 32 33 34`. Five consecutive wrong attempts lock
that connection's unlock for 30 s. Unlock lasts until disconnect.

| Opcode | Action | PIN required |
|---|---|---|
| `0x00` / `0x01` | status LED off / on (legacy BLE LED command, GPIO23) | no |
| `0x10` | zero orientation (current pose becomes identity) | yes |
| `0x11` | save the current config to NVS | yes |
| `0x12` | factory reset (defaults, incl. PIN `1234` and mounting) + reboot | yes |
| `0x13` | reboot | yes |
| `0x14` | mounting calibration (captured and saved to NVS automatically) | yes |
| `0x20` + PIN | unlock this connection | — |

Locked or unknown commands are ignored and logged on the serial console.

### Changing the PIN

1. Connect and unlock with the current PIN (`20 31 32 33 34` for the default).
2. Write the config characteristic (`19b10004-…`) with TLV id `0x41`, length 4, four
   ASCII digits, e.g. PIN `5678`: `41 04 35 36 37 38`. The new PIN takes effect
   immediately (existing unlocks stay valid until disconnect).
3. Send command `0x11` to persist it. Without it the old PIN returns after a reboot.

The PIN is write-only: it never appears in config reads. If it is lost, erase the flash
(`pio run -d firmware -e esp32dev -t erase`, then re-flash) to return to `1234`.

## Config quick reference

Config is read/written as TLV records on characteristic `19b10004-…` (writes require an
unlocked connection; invalid payloads are rejected whole). Changes apply live and are
persisted only by command `0x11`. Full table with types: [`docs/protocol.md`](docs/protocol.md).

| id | field | default |
|---|---|---|
| `0x01` | orientationMode (0 Quat6 relative, 1 Quat9) | 0 |
| `0x02` | imuNotifyHz (10–60) | 50 |
| `0x03` | slerpAmount (0–100, 0 off) | 0 |
| `0x10`–`0x15` | audio: enabled, fast/slow alpha ×1000, debounceMs, potControlsThreshold, thresholdFixed | 1, 250, 995, 300, 1, 100000 |
| `0x20`–`0x26` | CO2: enabled, checksum variant, stale timeout, virtual peak/delay/hold, raw log | 1, 0, 5000, 2100, 1000, 17000, 0 |
| `0x27` | CO2 protocol: 0 = Senseair S8 Modbus (poll), 1 = passive 0x42 0x4D frames | 0 |
| `0x30`–`0x33` | sound LED pin, CO2 LED pin, co2AlertPpm, status LED pin (pins pairwise distinct) | 2, 4, 2000, 23 |
| `0x40` | bleName (≤16 chars) | `Globo_1` |
| `0x41` | PIN (4 ASCII digits, write-only) | `1234` |

## Serial log

115200 baud, leveled (`E/W/I/D`), rate-limited to 20 lines/s. Boot prints each service's
status; failures (IMU absent, I2S init, NVS) are logged and the rest of the firmware keeps
running; subsystem health is reported in the diagnostics packet (there is no error LED).
