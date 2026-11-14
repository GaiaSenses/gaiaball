# Gaiaball v2 firmware

ESP32 firmware for the Gaiaball: IMU orientation (ICM-20948 or GY-91 / MPU-9250, chosen in the config-app) fused on the MCU,
stereo INMP441 sound-peak detection, UART CO2 sensor with a sound-triggered virtual CO2
pulse, two status LEDs, and a NimBLE GATT server streaming binary packets to up to two
centrals.

- Why each processing stage exists, per sensor: [Signal processing chains](#signal-processing-chains)
- What each packet costs on the air, per item and in total: [BLE byte budget](#ble-byte-budget)

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
  test/                 Unity tests for src/logic (native)
  docs/
```

## Build and flash

Toolchain versions are pinned: **ESP32 Arduino core 2.0.17** and **NimBLE-Arduino
1.4.3**. NimBLE 2.x and core 3.x are not supported (different APIs). The IMU drivers
(ICM-20948 + AK09916, or GY-91's MPU-9250 + AK8963) are register-level code in
`src/services/Icm20948.*` and `src/services/Mpu9250.*` behind one `ImuDriver` interface
— no external IMU library, no DMP; the fusion runs on the ESP32 (`src/logic/Ahrs.*`,
host-tested). The driver is picked at boot from TLV `0x34`; a factory-fresh ball leaves
the IMU off until it is configured.

### PlatformIO (recommended)

From the repository root:

```sh
pio run -d firmware -e esp32dev                       # build
pio run -d firmware -e esp32dev -t upload             # build + flash (auto-detects the port)
pio run -d firmware -e esp32dev -t upload --upload-port /dev/ttyUSB0
pio device monitor -d firmware -e esp32dev            # serial log, 115200 baud
pio test -d firmware -e native                        # host unit tests (131 cases)
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

### Resource usage (build of 2026-11-09)

| Toolchain | Flash (of 1 310 720 B) | Static RAM (of 327 680 B) |
|---|---|---|
| PlatformIO `esp32dev` | 814 085 B (62.1 %) | 51 372 B (15.7 %) |
| arduino-cli `esp32:esp32:esp32` | 803 653 B (61 %) | 51 268 B (15 %) |

Static RAM went *down* with the v8 audio changes: the I2S block shrank from 256 to 80
frames, so both the DMA buffers and the task's sample buffer are smaller.

Known build warnings (pre-existing, harmless, from the legacy IDF drivers kept on
purpose for core 2.0.17): `'I2S_COMM_FORMAT_I2S' is deprecated` (AudioService) and
`'ADC_ATTEN_DB_11' is deprecated` (PotService).

Maintenance note: `driver/i2s.h` (AudioService) and `driver/adc.h` (PotService) are
IDF 4.x APIs, removed in IDF 5 / Arduino core 3.x. The `espressif32@6.12.0` pin
freezes this safely; an eventual core upgrade requires porting to `esp_driver_i2s`
(std mode) and `esp_adc` (oneshot) — nothing else in the firmware depends on IDF 4.

## Pinout

Verified against the KiCAD board design (`Globo_GaiaSenses_PCi`); the full netlist
cross-check lives in [docs/hardware-pinout.md](docs/hardware-pinout.md).

| Function | GPIO | Notes |
|---|---|---|
| IMU I2C SDA | 21 | IMU module: GY-ICM20948V2 (ICM-20948 + AK09916) or GY-91 (MPU-9250 + AK8963 + BMP280), 400 kHz, AD0/SDO low (board header U1) |
| IMU I2C SCL | 22 | |
| I2S WS (LRCLK) | 25 | two INMP441 (L/R), 16 kHz, 32-bit stereo (board M1/M2) |
| I2S SCK (BCLK) | 26 | |
| I2S SD (data in) | 33 | |
| CO2 UART RX | 16 | UART1, 9600 8N1, listen-only (SC8-CO2-5K TXD → 16) |
| CO2 UART TX | 17 | routed on the board but NOT opened: any query halts the SC8's active output until power-up |
| Threshold potentiometer | 34 | ADC1 channel 6, 11 dB (board RV1 via R3) |
| Sound LED | 2 | board LED D2 ("LED mic"); config TLV `0x30` (reboot to apply). Strapping pin, held LOW at boot |
| CO2 alert LED | 4 | board LED D1 ("LED co2"); config TLV `0x31` (reboot to apply) |

### One pin, one function

The legacy firmware drove **three different functions on GPIO2** (BLE LED command,
sound LED, and a bogus SPI chip-select): a design error — one pin must carry one
function. The board has exactly two LEDs (D2 = GPIO2, D1 = GPIO4), and each now has
exactly one driver. The legacy BLE LED command (`0x00`/`0x01`) was **removed**: no
client ever sent it (the site declares the characteristic but never writes it), so
the opcodes are reserved and ignored. The two LED pins are enforced distinct: a
config write that lands both LED functions on the same GPIO is rejected atomically.

## Runtime behavior

- Advertises as **`Globo_1`** (config TLV `0x40`, applied after reboot); accepts two
  simultaneous centrals.
- IMU packets at `imuNotifyHz` (default 50 Hz, configurable 10–200 Hz — the sample rate
  of the selected IMU, ICM-20948 or MPU-9250, follows), **each one carrying the microphone envelope** (RMS and peak per
  channel, in dBFS); CO2 packets on every ppm/flag change plus a 1 Hz keepalive;
  diagnostics at 1 Hz; live sensor levels (SensorsPacket) at 5 Hz; and raw decimated
  audio (2 kHz int16, for the config-app's frontend FFT) at ~21 pkt/s on the diag
  characteristic **only while `audioRawStream` (TLV `0x1F`) is on — off by default**.
  Notifications are only sent to subscribers. See [BLE byte budget](#ble-byte-budget).
- **Globe navigation** is computed on the ball (`src/logic/NavEngine.*`): mirror (the
  globe is the orientation) or joystick (the tilt steers the globe), with a
  moving/settled state machine, sent as a 22-byte NavPacket on its own characteristic
  (`19b10006-…`, optional for clients) every N IMU samples,
  N = max(1, round(`imuNotifyHz` / `navNotifyHz`)), plus a 1 Hz `valid = 0` keepalive
  when navigation is off or the IMU is silent for more than 500 ms. Frames, laws and
  resets: [`docs/protocol.md`](docs/protocol.md#navpacket-22-bytes-type-0x07-own-characteristic).
  The zero (`0x10`) is not persisted across power cycles. The CO2 packet also carries a
  `trigger` flag (TLV `0x29`/`0x2A`, level with hysteresis).
- On every connect the firmware requests a **7.5–15 ms connection interval** (0 latency,
  4 s supervision timeout). This is what makes rates above ~60 Hz reachable at all: a
  notification can only leave inside a connection event, so the ceiling is (packets per
  event) / (interval). The central may refuse, so the granted interval is reported in
  the diag packet and shown in the config-app's metrics panel.
- Per-sensor filters, all configurable from the config-app and persisted: the IMU
  anti-alias filter and gyro range, the AHRS gain and its acceleration/magnetic
  rejection, the gyro-bias tracker, orientation smoothing (fixed slerp or the 1-euro
  filter), the accel spike filter and EMA, the breath band-pass on the mics (HP 10 Hz /
  LP 200 Hz, Q 5.00 default — tune against the live spectrum), the potentiometer EMA and
  the CO2 EMA. See [Signal processing chains](#signal-processing-chains).
- Wi-Fi is explicitly stopped at boot (BLE/Wi-Fi coexistence would wreck notify pacing).
- USB serial logging is OFF by default (TLV `0x50`); the boot sequence (including the
  I2C identity probe below) always prints, the switch takes over afterwards.
- The IMU is **either** a `GY-ICM20948V2` module (ICM-20948 + AK09916 mag; the ball in
  hand) **or** a GY-91 (MPU-9250 + AK8963 mag + BMP280 barometer; future units); the
  operator picks the chip in the config-app (TLV `0x34`, reboot to apply, default
  "unconfigured" = IMU off). The drivers are register-level
  (`src/services/Icm20948.*`, `src/services/Mpu9250.*`); the fusion is the revised
  Madgwick filter (`src/logic/Ahrs.*`) with acceleration and magnetic rejection, fed by
  a gyro-bias tracker whose rest detector works at any zero-rate offset
  (`src/logic/GyroBias.*`). The BMP280 (I2C 0x76) is present on the module but not read
  by this firmware. The earlier claim that every ball carries a GY-91 was wrong: see
  [`docs/hardware-pinout.md`](docs/hardware-pinout.md) finding 2 for the evidence.
- The magnetometer needs a one-off hard/soft-iron calibration before Quat9 is worth
  using: command `0x15` streams raw samples, the config-app fits the ellipsoid and
  writes the result back as TLV `0x1E`.
- Sound LED (GPIO2): 100 ms flash per detected sound peak, held on while the virtual CO2
  pulse is active. Nothing else drives it.
- CO2 LED (GPIO4): on while the real (non-stale) reading exceeds `co2AlertPpm` (default 2000).
- Subsystem failures are reported in the diagnostics packet (config-app), not on an LED.
- CO2 sensor: **SC8-CO2-5K** ("YYS SC8", per the board and the user-confirmed part).
  It streams one 16-byte `42 4D` frame per second on its own; the firmware listens
  only (never transmits — a query would halt the stream until power-up) and resyncs
  on the header after any byte loss.
- A sound peak triggers the virtual CO2 pulse: after `virtualDelayMs` (1 s) the reported
  ppm becomes `virtualPeakPpm` (2100) for `virtualHoldMs` (17 s).

## BLE byte budget

Everything here is arithmetic over the packet sizes in
[`docs/protocol.md`](docs/protocol.md) and the BLE 4.2 link-layer framing — **nothing in
this section has been measured on hardware yet** (see [`../docs/bench-validation.md`](../docs/bench-validation.md)).

Air time per notification, LE 1M PHY, unencrypted link (the PIN is an application-level
gate, there is no pairing), counting the central's mandatory empty PDU and both
inter-frame spaces:

```
LL packet bytes = 1 (preamble) + 4 (access address) + 2 (LL header)
                + payload + 3 (CRC) + 7 (L2CAP 4 + ATT 3)   =  P + 17
air time        = (P + 17) x 8 us  +  80 us (empty PDU)  +  300 us (2 x T_IFS)
```

So a 23-byte packet costs ~700 µs and a 208-byte one ~2180 µs. **Packet slots, not
bytes, are the scarce resource**: an 8-byte field added to an existing packet costs
64 µs, while the same 8 bytes in a packet of their own cost a full ~660 µs.

### What each v8 item costs or saves

Default configuration (`imuNotifyHz` = 50, raw stream off):

| # | Item | Bytes on the wire | Payload/s | Air time/s | CPU |
|---|---|---|---|---|---|
| 1 | RMS per channel on every ImuPacket | **+4 B/packet** (2 × i16, 0.01 dBFS) | +200 B/s | +1.6 ms | ~0 (it was already being computed) |
| 2 | Peak per channel, tracked at 16 kHz | **+4 B/packet** (2 × i16, 0.01 dBFS) | +200 B/s | +1.6 ms | +1 compare per sample per channel ≈ 0.01 % of a core |
| 3 | Granted connection interval in the DiagPacket | **+2 B/packet** (u16, 0.1 ms) | +2 B/s | +0.02 ms | 0 |
| 4 | Raw 2 kHz stream now opt-in (TLV `0x1F`) | **−208 B/packet × −20.8 packets/s** | **−4333 B/s** | **−45.4 ms** | −1 decimator feed per sample ≈ −0.1 % of a core |
| 5 | `double` → `float` in the RMS accumulators | 0 | 0 | 0 | **−1.7 % of a core** (the LX6 FPU is single-precision; every double op was emulated) |
| | **Total** | ImuPacket 23 → 31 B, DiagPacket 32 → 34 B | **−3931 B/s (−70 %)** | **−42.2 ms (−49 %)** | ≈ −1.8 % of one core |

### Globe navigation (v9): the NavPacket

The NavPacket is a packet of its own (own characteristic), so it pays the full per-slot
cost: 22 B payload, `(22 + 17) x 8 + 80 + 300` = **692 µs** per notification. At the
default `navNotifyHz` = 50 (N = 1 at `imuNotifyHz` = 50) that is
50 x 22 = **1100 B/s** and 50 x 692 µs = **34.6 ms/s** of air time, +50 packets/s. It is
sent only while a central subscribes to `…0006`, and the rate does not depend on
`imuNotifyHz`: at 100 or 200 Hz the divisor is 2 or 4 and the nav stream stays at 50 Hz.
The 1 Hz keepalive (navigation off or IMU silent) is 22 B/s and 0.7 ms/s. The Co2Packet
trigger flag is one bit of an existing byte: 0 extra bytes.

These figures are arithmetic from the formula above, **not measured on hardware**.

Item 1 and item 2 together are the 8 bytes appended to the ImuPacket; they are listed
apart because they answer different questions (energy over a window vs. the transient
inside it), and because only the peak needed new per-sample work.

### Link totals

| Scenario | Packets/s | Payload | Air time | Duty |
|---|---|---|---|---|
| v7 (50 Hz, stream always on) | 77.8 | 5642 B/s | 85.3 ms/s | 8.5 % |
| **v8 default (50 Hz, stream off)** | **57** | **1711 B/s** | **43.1 ms/s** | **4.3 %** |
| v8 at 100 Hz, stream off | 107 | 3261 B/s | 81.3 ms/s | 8.1 % |
| v8 at 200 Hz, stream off | 207 | 6361 B/s | 157.7 ms/s | 15.8 % |
| v8 at 100 Hz while tuning (stream on) | 128 | 7594 B/s | 126.7 ms/s | 12.7 % |
| **v9 default (50 Hz, stream off, nav 50 Hz)** | **107** | **2811 B/s** | **77.7 ms/s** | **7.8 %** |
| v9 at 100 Hz, stream off, nav 50 Hz | 157 | 4361 B/s | 115.9 ms/s | 11.6 % |
| v9 at 200 Hz, stream off, nav 50 Hz | 257 | 7461 B/s | 192.3 ms/s | 19.2 % |
| v9 at 100 Hz while tuning (stream on, nav 50 Hz) | 178 | 8694 B/s | 161.3 ms/s | 16.1 % |

The v9 rows are the v8 rows plus the NavPacket at 50 Hz (+50 packets/s, +1100 B/s,
+34.6 ms/s). Breakdown of the v9 default (v8 plus the NavPacket row), per packet type:

| Packet | Payload | Rate | Payload/s | Air time/s |
|---|---|---|---|---|
| ImuPacket (orientation + envelope) | 31 B | 50 Hz | 1550 B/s | 38.2 ms |
| SensorsPacket (threshold, pot, health) | 23 B | 5 Hz | 115 B/s | 3.5 ms |
| DiagPacket | 34 B | 1 Hz | 34 B/s | 0.8 ms |
| Co2Packet | 12 B | 1 Hz | 12 B/s | 0.6 ms |
| NavPacket (v9, own characteristic) | 22 B | 50 Hz | 1100 B/s | 34.6 ms |
| AudioChunkPacket (opt-in) | 208 B | 0 (20.8 Hz when on) | 0 (4333 B/s) | 0 (45.4 ms) |
| **Total (v8 was 1711 B/s, 43.1 ms)** | | | **2811 B/s** | **77.7 ms** |

### Why the spectrum is not computed here

A 512-point spectrum of the 2 kHz stream is 256 bins. At one byte per bin that is
256 B per channel per frame — **more** than the 192 bytes of audio it was derived from,
and 520 B per stereo frame does not fit the 247-byte MTU. Sending spectra at the
resolution the config-app shows today would cost ~2.6× the air time of sending the audio
itself, and at 100 Hz the frames would be 96 % overlapped: the same 256 ms window
retransmitted 25 times. The FFT itself is affordable (~37 kcycles per frame per channel,
≈ 3 % of a core at 100 Hz stereo), so this is a bandwidth and information decision, not
a CPU one. If coarse bands ever turn out to be what the application wants, a bank of
~24 biquads costs ~0.3 % of a core and has no window latency, where a 256 ms FFT window
delays the result by ~128 ms.

## Signal processing chains

Every stage below exists because a raw sensor reading, sent straight to the wire, is
wrong in a specific and predictable way. Each diagram is followed by what the stage
defends against and what it buys; the order of the stages is itself part of the design
and is noted where it matters.

### Orientation (ICM-20948 or MPU-9250, same chain)

```
         ┌── ICM-20948 + AK09916  or  MPU-9250 + AK8963, polled over I2C ──────┐
         │  [1] on-chip DLPF — cut-off matched to the configured output rate    │
         └───────┬────────────────────┬───────────────────────┬─────────────────┘
            gyro (dps)           accel (g)                mag (uT)
                 │                    │                        │
      [2] gyro bias tracker          │             [3] hard/soft-iron calibration
          rest-gated EMA             │                 m_c = S (m_u - h)
                 │                    │                        │
                 └──────────┬─────────┴────────────┬───────────┘
                            │                      │
                 [4] AHRS fusion — revised Madgwick complementary filter
                     · acceleration rejection   · magnetic rejection
                     · startup gain ramp        · overrange recovery
                            │
                 [5] frame: conj(qMount) then conj(qZero)
                            │
                 [6] output smoothing — off | fixed slerp | 1-euro
                            │
                     ImuPacket.quat ──────────────────────────────> BLE (notify)

   Parallel path. The published accelerometer is a signal for the patch; the fusion
   always reads the UNFILTERED accelerometer, so filtering here cannot affect orientation:

     accel counts ──> [7] Hampel spike filter ──> [8] EMA ──> ImuPacket.acc
```

| # | Stage | What goes wrong without it | What it buys |
|---|---|---|---|
| 1 | On-chip DLPF, auto-matched to the rate (`imuDlpfMode`) | The internal rate is 1 kHz and `SMPLRT_DIV` only **decimates** it — no averaging. Anything the DLPF passes above half the output rate folds back into the band and is then indistinguishable from real motion. | Vibration and tapping stop appearing as slow tremble. At 50 Hz the auto mode picks 20 Hz instead of the old fixed 41 Hz, which was above Nyquist. |
| — | Gyro full scale (`gyroFsSel`) | ±2000 dps quantises to 0.061 °/s per bit, and that quantisation is integrated straight into orientation drift. | ±1000 dps halves the noise floor while still leaving ~2.8 rev/s of headroom for a spun ball; the gyro never reaches the wire, so no decoder cares. |
| 2 | Gyro bias tracker (`src/logic/GyroBias.*`) | The zero-rate offset is integrated: 1 °/s is 60° of heading per minute, and in Quat6 nothing pulls the heading back. The MPU-9250 is specified at ±5 °/s. | Rest is detected from the **deviation** of the rate around its own mean (offset-independent), gated by a plausibility bound and the accelerometer. The previous design tested the *corrected* rate against 2 °/s, so a unit whose real offset exceeded that never looked still and never got corrected — the worse the part, the less the correction worked. |
| 3 | Hard/soft-iron calibration (`src/logic/MagCalibration.h`) | The speaker magnet, battery and board currents add a constant field (tens of µT against an earth field of 25–65 µT) and surrounding ferrous material stretches the field sphere into an ellipsoid. | Heading errors of tens of degrees disappear. Without it, 9-axis can be **worse** than magnetometer-free fusion. The fit itself runs in the config-app (command `0x15` streams raw samples). |
| 4 | AHRS fusion (`src/logic/Ahrs.*`) | Gyro-only integration drifts without bound; accelerometer-only tilt is unusable during motion; magnetometer-only heading is unusable near metal. | A complementary filter whose corner frequency is the `gain`. The rejection features make it survive the two things a *ball* does constantly: being accelerated (the accelerometer stops pointing down) and being carried past metal. Prolonged disagreement triggers recovery, so a long disturbance cannot strand the filter on the gyroscope. The startup ramp converges from an unknown orientation in about a second. |
| 5 | Mounting quaternion, then zero | The chip sits at a small angle to the board, the board at an angle to the ball, and the ball rests however it rests. All three appear as a permanent tilt. | `0x14` absorbs the whole chain into `qMount` (persisted in NVS); `0x10` sets the session datum. Roll/pitch survive a reboot; in Quat6 the heading datum is arbitrary by construction, which is what `0x10` is for. |
| 6 | Output smoothing (`src/logic/QuatSmoother.*`) | The fused quaternion still carries the gyro noise floor: a fine tremble in a 3D view, a dithering control signal in a sound patch. | The legacy fixed slerp costs ~190 ms of lag at its heaviest. The 1-euro filter moves its cut-off with the angular speed — hard filtering while the ball is still, almost none while it moves — so the tremble goes without the rubbery response. |
| 7 | Hampel spike filter (`Hampel5`, `src/logic/Filters.h`) | A dropped or corrupted I2C read is one wild sample. An EMA does not remove it; it smears it across the following samples. | The newest sample is compared with the median of the last five and replaced only if it deviates by more than k·MAD. **Zero added latency** (trailing window), and a genuine step becomes the new normal within three samples. It runs **before** the EMA on purpose: after smoothing, a spike is no longer detectable. |
| 8 | Accel EMA (`accelLpAlphaX1000`) | ADC and mechanical noise make the published acceleration jittery. | A one-pole low-pass with a known time constant (α = 0.2 at 50 Hz ≈ 90 ms, f_c ≈ 1.8 Hz). Off by default: the site pipeline filters again, and filtering twice only doubles the latency. |

The chain is identical for both chips: everything from stage [2] onwards runs on the
ESP32 and never learns which sensor produced the numbers. The drivers (`ImuDriver`)
hide the following differences, which only matter when reading a register trace or
choosing a DLPF index by hand:

| | MPU-9250 (GY-91) | ICM-20948 (GY-ICM20948V2) |
|---|---|---|
| Identity | `WHO_AM_I` `0x71` at `0x75` | `WHO_AM_I` `0xEA` at `0x00` (register `0x75` is reserved and reads a meaningless `0x48`) |
| Register map | flat, one bank | **banked** (4 banks sharing addresses `0x00..0x7E`; `REG_BANK_SEL` `0x7F` is the only register visible in all of them); a read in the wrong bank returns plausible garbage |
| Rate dividers | one `SMPLRT_DIV`, 1 kHz base | **separate** gyro (8-bit) and accel (12-bit) dividers, 1125 Hz base |
| DLPF index 1..7 (gyro) | 184 / 92 / 41 / 20 / 10 / 5 / 3600 Hz | 151.8 / 119.5 / 51.2 / 23.9 / 11.6 / 5.7 / 361.4 Hz (accel cut-offs differ slightly; same index is written to both) |
| Auto DLPF at 50 Hz | 20 Hz (mode 4) | 11.6 Hz (mode 5) |
| Auto DLPF, same configured rate | picks index *n* | lands one index narrower than the MPU for most rates (e.g. 50 Hz: 11.6 Hz vs 20 Hz), because the ICM cut-offs (51.2 / 23.9 / 11.6 ...) sit just above the `0.47 × rate` guard the unchanged auto rule applies (at 50 Hz the limit is 23.5 Hz, and 23.9 Hz does not fit). Forcing index 4 (23.9 Hz) is possible and is a bench decision |
| Magnetometer | AK8963 at `0x0C` via I2C bypass; 16-bit with per-axis factory ASA trim; 0.15 µT/LSB after trim; little-endian | AK09916 at `0x0C` via pass-through; 16-bit, fixed 0.15 µT/LSB, **no** ASA fuse ROM; `WIA1` `0x48` and `WIA2` `0x09` (the AK8963 also reports `0x48` in `WIA1`, so only the pair tells them apart) |
| Magnetometer axes | AK8963 frame | different from the accel/gyro frame on both chips; the operator-selected `magAxisRemap` (TLV `0x36`) puts it right |
| Data ready | `INT_STATUS` bit 0 | `INT_STATUS_1` bit 0 (clear-on-read behaviour not confirmed from the vendored register map: see `docs/bench-validation.md`) |

The scales on the wire are the same for both: accelerometer +-4 g at 8192 LSB/g, gyro
full scale selectable with the same four codes (TLV `0x06`).

### Sound (2× INMP441)

```
   2x INMP441 ──> I2S, 32-bit stereo @ 16 kHz, 80-frame blocks (5 ms)
        │
   [1] >>8 to 24-bit samples, DC removal
        │
        ├──> [2] breath band-pass: HP biquad then LP biquad
        │          │
        │     [3] per-block sum of squares + max |sample| (16 kHz, float)
        │          │
        │     [4] AudioLevelWindow: 20 ms pooled energy, fresh every block
        │          │
        │          ├──> [5] RMS + peak per channel ──> every ImuPacket (dBFS x100)
        │          │                                   and the 5 Hz SensorsPacket
        │          │
        │          └──> [6] adaptive peak detector (every 3rd block = 15 ms):
        │                   fast EMA vs slow EMA + threshold, hysteresis, debounce
        │                   │
        │                   └──> sound event ──> LED flash + virtual CO2 pulse
        │
        └──> [7] decimate 16 kHz -> 2 kHz (average of 8) ──> AudioChunkPacket
                   OPT-IN (TLV 0x1F, off by default); the FFT runs in the browser
```

| # | Stage | What goes wrong without it | What it buys |
|---|---|---|---|
| 1 | 24-bit extraction + DC removal | The INMP441 pushes 24-bit data inside 32-bit slots and carries a DC offset that would bias every RMS value. | A zero-mean signal, so RMS measures sound and not the offset. The 5 ms block exists because the envelope published on the orientation packet can never be fresher than one block, and that packet goes out at up to 200 Hz. |
| 2 | Breath band-pass (`audioBpfEnabled`, HP/LP + Q) | Handling noise and table thumps live below the breath band; speech and room tone above it. Both fire the detector. | Only the breath band reaches the detector. The cut-offs are tuned against the live spectrum in the config-app, which is what the opt-in raw stream is for. |
| 3 | Sum of squares + peak, in `float` | Instantaneous amplitude swings sample to sample; a threshold on it would chatter. A peak measured on the decimated stream (as the browser used to) is averaged over 8 samples, so it under-reads every transient. | Energy and the true 16 kHz peak of the same signal, so peak/RMS is a meaningful crest factor. `float`, not `double`: the Xtensa LX6 FPU is single-precision, so every double operation here was emulated in software — more cycles than the rest of the audio front end put together, for an error of 1e-4 dB. |
| 4 | Pooled 20 ms window (`src/logic/AudioFeatures.*`) | At 200 Hz a block is 5 ms — shorter than one period of the low end of the breath band (10 ms at 100 Hz) — so a single block measures instantaneous amplitude, not energy, and the value wobbles with the waveform. | A 20 ms energy window that still refreshes every block (75 % overlap): a stable number at a fast rate, instead of having to choose. |
| 5 | Envelope on the wire (`audioRmsDbX100`, `audioPeakDbX100`) | The envelope used to reach the host at 5 Hz (SensorsPacket) or had to be re-derived in the browser from the decimated stream — a 200 ms-old number, or a wrong one. | The signal the application actually uses as a control arrives at the orientation rate (up to 200 Hz), with the same timestamp, for 8 bytes on a packet that was already being sent. |
| 6 | Adaptive peak detector (`src/logic/PeakDetector.*`) | A fixed threshold drifts out of calibration with the room: too high in a quiet hall, constantly triggered in a noisy one. | The fast envelope is compared against a slow envelope (the room's own noise floor) plus the threshold, so the detector adapts to the room. Hysteresis and debounce turn one long breath into exactly one event. It runs every 3rd block (15 ms) on purpose: its EMA coefficients are per update and were tuned against the old 16 ms block, so keeping the cadence keeps the stored alphas meaningful. |
| 7 | Decimation to 2 kHz, opt-in (`audioRawStream`) | Streaming 16 kHz audio over BLE would eat the notify budget the orientation stream needs. Streaming even the decimated version *all the time* costs more air time than every other packet put together. | ~4.3 kB/s instead of ~32 kB/s while it is on, and nothing at all while it is off — which is the default, because only the spectrum view needs it. Average-of-8 is the anti-alias filter for that decimation. |

### CO2 (SC8-CO2-5K)

```
   SC8-CO2-5K ──> UART1 9600 8N1, listen-only (never transmit: a query mutes the sensor)
        │
   [1] frame sync on 0x42 0x4D + checksum
        │
   [2] stale timeout (co2StaleTimeoutMs)
        │
   [3] EMA over ACCEPTED readings (co2EmaAlphaX1000)
        │
   [4] virtual pulse override (sound-triggered)
        │
   Co2Packet ──> BLE
```

| # | Stage | What goes wrong without it | What it buys |
|---|---|---|---|
| 1 | Frame sync + checksum | A byte lost on the UART shifts every following field; the ppm value becomes nonsense that still looks plausible. | Resync on the header after any loss, and a corrupted frame is dropped instead of reported. |
| 2 | Stale timeout | A sensor that stopped answering would keep a stale number on screen for ever. | The reading is flagged `stale`, so the UI and the patch can tell "old" from "low". |
| 3 | EMA over accepted readings | The sensor's own quantisation steps show up as jumps in a value people read as a slow trend. | A smooth trend. Off by default (1000), because the legacy behaviour is the reference for the patch. |
| 4 | Virtual pulse | The real sensor's T90 is minutes: a breath would reach the composition long after the person stopped blowing. | A plausible ppm curve arrives ~1 s after the breath, which is what makes the instrument playable. The real reading returns when the pulse ends. |

### Threshold potentiometer

```
   RV1 ──> ADC1 ch6, 11 dB attenuation, read at 10 Hz
        │
   [1] EMA (potEmaAlphaX1000)
        │
   threshold for the sound peak detector (when potControlsThreshold = 1)
```

The ESP32's SAR ADC is noisy by a few LSB, and that noise would modulate the detector
threshold continuously. One EMA (default α = 0.3, the legacy value) is enough: the knob
is turned by hand, so there is nothing fast to preserve.

## PIN and commands

Sensitive operations require a per-connection unlock with a 4-digit PIN. **Default PIN:
`1234`.** Unlock by writing `0x20` followed by the ASCII PIN to the command
characteristic (`19b10002-…`), e.g. `20 31 32 33 34`. Five consecutive wrong attempts lock
that connection's unlock for 30 s. Unlock lasts until disconnect.

| Opcode | Action | PIN required |
|---|---|---|
| `0x00` / `0x01` | reserved (legacy LED command, removed — ignored) | — |
| `0x10` | zero orientation (current pose becomes identity) | yes |
| `0x11` | save the current config to NVS | yes |
| `0x12` | factory reset (defaults, incl. PIN `1234` and mounting) + reboot | yes |
| `0x13` | reboot | yes |
| `0x14` | mounting calibration (captured and saved to NVS automatically) | yes |
| `0x15` | start raw magnetometer capture (streams MagChunk packets; auto-stops after 180 s) | yes |
| `0x16` | stop raw magnetometer capture | yes |
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
| `0x02` | imuNotifyHz (10–200; drives the selected IMU's sample rate live) | 50 |
| `0x03` | slerpAmount (0–100, 0 off; used when quatSmoothMode = 1) | 0 |
| `0x04` | accelLpAlphaX1000 (0 off) | 0 |
| `0x05` | imuDlpfMode (0 = auto from the rate; 1–7 force DLPF_CFG) | 0 |
| `0x06` | gyroFsSel (0 = ±250, 1 = ±500, 2 = ±1000, 3 = ±2000 dps) | 2 |
| `0x07` | fusionGainX100 (0–500) | 50 |
| `0x08`–`0x0A` | accelRejectionDeg, magRejectionDeg, rejectionTimeoutS (0 = off) | 10, 10, 5 |
| `0x0B`–`0x0D` | biasRestThDpsX100, biasMaxDpsX100, biasTauS | 200, 1000, 4 |
| `0x0E`–`0x0F` | quatSmoothMode (0 off / 1 slerp / 2 one-euro), oneEuroFcMinX100 | 0, 100 |
| `0x1C`–`0x1D` | oneEuroBetaX1000, accelHampelKX10 (0 off) | 20, 0 |
| `0x1E` | magCal — 24-byte hard/soft-iron record (see protocol.md) | identity |
| `0x10`–`0x15` | audio: enabled, fast/slow alpha ×1000, debounceMs, potControlsThreshold, thresholdFixed | 1, 250, 995, 300, 1, 100000 |
| `0x16`–`0x1A` | breath band-pass: enabled, HP Hz, HP Q×100, LP Hz, LP Q×100 (LP > HP enforced) | 1, 10, 500, 200, 500 |
| `0x1F` | audioRawStream — raw 2 kHz stream on/off (costs ~4.3 kB/s; only the spectrum view needs it) | 0 |
| `0x1B` | potEmaAlphaX1000 | 300 |
| `0x20`, `0x22`–`0x26` | CO2: enabled, stale timeout, virtual peak/delay/hold, raw log | 1, 5000, 2100, 1000, 17000, 0 |
| `0x28` | co2EmaAlphaX1000 (1000 = off) | 1000 |
| `0x29`–`0x2A` | CO2 trigger flag: co2TriggerPpm (400–10000), co2TriggerHystPpm (0–2000) | 1200, 50 |
| `0x30`–`0x32` | sound LED pin, CO2 LED pin (pins distinct), co2AlertPpm | 2, 4, 2000 |
| `0x34` | imuChip (0 unconfigured / IMU off, 1 ICM-20948, 2 GY-91 / MPU-9250; reboot to apply) | 0 |
| `0x35` | imuDetected — **read-only**, appended on config reads; never write it | — |
| `0x36` | magAxisRemap (0–23, applied live; changing it invalidates a `0x1E` calibration) | 8 |
| `0x40` | bleName (≤16 chars) | `Globo_1` |
| `0x41` | PIN (4 ASCII digits, write-only) | `1234` |
| `0x50` | serialLogEnabled | 0 |
| `0x60` | navMode (0 off, 1 mirror, 2 joystick; a change resets the globe) | 1 |
| `0x61` | navNotifyHz (10–200) | 50 |
| `0x62` | navRef — 8 bytes: i32 lat ×1e6, i32 lon ×1e6 (a change resets the globe) | −22905556, −47060833 (Campinas) |
| `0x63`–`0x65` | joystick: joyMaxSpeedDegS (1–720), joyFullAngleDeg (5–90), joyCurveX10 (10–40) | 90, 60, 30 |
| `0x66`–`0x68` | mirror settle: navStartDegS (1–200), navStopDegS (1–100, < start), navStopMs (50–5000) | 20, 5, 420 |
| `0x69`–`0x6A` | joystick settle: joyStartDegSX10 (1–2000), joyStopDegSX10 (1–2000, < start) | 10, 5 |

Reserved ids (removed, never reused): `0x21` (ex co2ChecksumVariant), `0x27`
(ex co2Protocol) — gone since the SC8-CO2-5K datasheet fixed the frame format —
and `0x33` (ex statusLedPin).

## Serial log

115200 baud, leveled (`E/W/I/D`), rate-limited to 20 lines/s. Boot prints each service's
status; failures (IMU absent, I2S init, NVS) are logged and the rest of the firmware keeps
running; subsystem health is reported in the diagnostics packet (there is no error LED).
