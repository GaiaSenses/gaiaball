# Gaiaball BLE wire protocol v1

All multi-byte integers are **little-endian**; structs are `__attribute__((packed))`.
Protocol version: `1` (first byte of every packet). Source of truth:
`Gaiaball/src/logic/Packets.h`; golden vectors are fixed by `test/test_protocol`.

## BLE UUIDs

Service `19b10000-e8f2-537e-4f6c-d104768a1214`.

| UUID | Dir | Purpose |
|---|---|---|
| `19b10001-e8f2-537e-4f6c-d104768a1214` | notify | IMU packets |
| `19b10002-e8f2-537e-4f6c-d104768a1214` | write | commands |
| `19b10003-e8f2-537e-4f6c-d104768a1214` | notify | CO2 packets |
| `19b10004-e8f2-537e-4f6c-d104768a1214` | read/write | config TLV |
| `19b10005-e8f2-537e-4f6c-d104768a1214` | notify | diagnostics (0x03, 1 Hz), live-sensor (0x04, 5 Hz), raw-audio (0x05, ~21 Hz, **only while TLV 0x1F is on**) and raw-magnetometer (0x06, ~3 Hz while calibrating) packets |

### Connection parameters

On every connect the firmware requests **7.5–15 ms** interval, 0 latency, 4 s
supervision timeout (`NimBLEServer::updateConnParams`). The central may refuse or pick
any value inside the range, so the granted interval is reported in
`DiagPacket.connItvlMsX10` rather than assumed. It is the ceiling on every notify rate:

```
max notifications/s = (packets per connection event) / (connection interval)
```

With the ~3 packets per event an ESP32 typically fills, a 15 ms interval caps the link
at ~200 notifications/s and a 30 ms one at ~100/s — regardless of how fast the firmware
produces data.

## Scales

- Quaternion: `int16 = round(float * 16384)` (Q14), saturating; `value / 16384` on decode.
- Accel: raw counts of the selected IMU (MPU-9250 or ICM-20948; ±4 g, 8192 LSB/g on both), `int16` passthrough (no scaling).
- Gyro: never on the wire. Its full scale is configurable (TLV 0x06) precisely because
  no decoder depends on it.
- Magnetometer (MagChunk only): `int16` in 0.1 µT, saturating.
- Gyro bias (DiagPacket): `int16` in 0.01 °/s.
- Microphone levels (ImuPacket): `int16` in 0.01 dBFS, floor `-12000` (−120.00 dBFS),
  ceiling `6000`. RMS uses the AES17 reference (0 dBFS = RMS of a full-scale sine, i.e.
  `2^23 / sqrt(2)` counts); peak uses the amplitude reference (0 dBFS = a full-scale
  sample, `2^23` counts). The same sine therefore reads 3.01 dB lower on the peak scale.
- Connection interval (DiagPacket): `u16` in 0.1 ms, 0 = unknown.
- Non-finite quaternion input is encoded as identity (`W=16384`) with `quatValid` cleared.

## Packet type ids

`0x01` ImuSample, `0x02` Co2Sample, `0x03` Diagnostics, `0x04` Sensors, `0x05`
AudioChunk, `0x06` MagChunk. The diag characteristic carries 0x03, 0x04, 0x05 and
0x06; receivers must dispatch on the type byte.

## ImuPacket (31 bytes)

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x01) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampUs |
| 8 | 2 | i16 | quatW |
| 10 | 2 | i16 | quatX |
| 12 | 2 | i16 | quatY |
| 14 | 2 | i16 | quatZ |
| 16 | 2 | i16 | accX |
| 18 | 2 | i16 | accY |
| 20 | 2 | i16 | accZ |
| 22 | 1 | u8 | flags |
| 23 | 2 | i16 | audioRmsDbX100[0] (left) |
| 25 | 2 | i16 | audioRmsDbX100[1] (right) |
| 27 | 2 | i16 | audioPeakDbX100[0] (left) |
| 29 | 2 | i16 | audioPeakDbX100[1] (right) |

Flags: bit0 `quatValid`, bit1 `nineAxis`, bit2 `moving`, bit3 `audioValid`.

The microphone envelope is measured in the firmware over the 20 ms window that ended
with this orientation sample, with the peak tracked at the full 16 kHz. While
`audioValid` is clear (no microphone / I²S failure) all four fields read the floor,
−120.00 dBFS. The packet grew from 23 to 31 bytes in firmware v8; see the note under
DiagPacket about append-only growth.

Why here and not in a packet of its own: above ~60 Hz the scarce resource on a BLE link
is the connection-event budget, not bytes. A 31-byte notification costs ~764 µs of air
time against ~700 µs for a 23-byte one, so carrying the envelope along is 9 % more air
time, while a separate notification at the same rate would be 100 % more. One packet per
tick also guarantees that the envelope and the orientation share a timestamp.

## Co2Packet (12 bytes)

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x02) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampMs |
| 8 | 2 | u16 | ppm |
| 10 | 1 | u8 | flags |
| 11 | 1 | u8 | ageS |

Flags: bit0 `valid`, bit1 `stale`, bit2 `virtualActive`.

`ppm` is the virtual pulse peak while a pulse is active; otherwise it is the real
reading when `co2Enabled=1`, or the constant **600** when `co2Enabled=0` — the
legacy firmware's `CO2_NORMAL_VALUE`, kept so a disabled sensor still feeds the
site's Pd patch a plausible baseline. The fallback is never flagged `valid`.

## DiagPacket (34 bytes)

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x03) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | uptimeS |
| 8 | 2 | u16 | imuHzX10 |
| 10 | 2 | u16 | notifyHzX10 |
| 12 | 2 | u16 | fifoOverflows |
| 14 | 2 | u16 | i2sErrors |
| 16 | 2 | u16 | uartFrameErrors |
| 18 | 2 | u16 | freeHeapKb |
| 20 | 2 | u16 | minFreeHeapKb |
| 22 | 1 | u8 | flags |
| 23 | 1 | u8 | ahrsFlags |
| 24 | 1 | u8 | accelErrDeg (0–90, saturating) |
| 25 | 1 | u8 | magErrDeg (0–90, saturating) |
| 26 | 2 | i16 | gyroBiasX100[0] |
| 28 | 2 | i16 | gyroBiasX100[1] |
| 30 | 2 | i16 | gyroBiasX100[2] |
| 32 | 2 | u16 | connItvlMsX10 (granted connection interval, 0.1 ms; 0 = unknown) |

Flags: bit0 `imuOk`, bit1 `audioOk`, bit2 `co2Ok`.

`ahrsFlags` exposes the IMU pipeline's live state, so a bench run can tell a feature
that is working from one that never triggered: bit0 `startup` (gain still ramping),
bit1 `overrange` (gyro range exceeded, filter restarted), bit2 `accelIgnored`
(acceleration rejection acting now), bit3 `magIgnored` (magnetic rejection acting now),
bit4 `biasAtRest`, bit5 `biasAdapting`, bit6 `magCalibrated` (a hard/soft-iron record is
loaded), bit7 `magCapture` (raw magnetometer streaming).

The packet grew from 23 to 32 bytes in firmware v7 and to 34 in v8; the protocol version
byte stays `1` because the earlier bytes are unchanged and every field is append-only. A
decoder that checks an exact length must be updated (the config-app and the Pd bridge
were). `connItvlMsX10` closes the loop on the connection-parameter request: without it
there is no way to tell a granted 7.5 ms interval from a refused one, and that single
number caps every notify rate above ~60 Hz.

## MagChunkPacket (106 bytes, type 0x06, ~3 Hz while calibrating, diag characteristic)

RAW (uncalibrated) magnetometer samples, streamed only between commands `0x15` and
`0x16`. The hard/soft-iron fit runs in the config-app: the ESP32 would need hundreds of
samples in RAM plus a least-squares solve, for no better result.

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x06) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampMs |
| 8 | 1 | u8 | count (0–16) |
| 9 | 1 | u8 | flags |
| 10 | 96 | i16[48] | samples: x,y,z interleaved, 0.1 µT per LSB |

Flags: bit0 `calibrated` (set only if the stream ever carries corrected values; the
calibration capture always clears it). Samples beyond `count` are zero. Values are
already remapped to the gyro/accel chip frame (AK8963 X↔Y swapped, Z inverted).

## SensorsPacket (23 bytes, type 0x04, 5 Hz, diag characteristic)

Live audio/potentiometer levels for the config-app's real-time charts.

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x04) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampMs |
| 8 | 4 | u32 | audioRmsL |
| 12 | 4 | u32 | audioRmsR |
| 16 | 4 | u32 | audioThreshold |
| 20 | 2 | u16 | potRaw |
| 22 | 1 | u8 | flags |

Flags: bit0 `audioOk`, bit1 `audioEnabled`, bit2 `potControlsThreshold`, bit3
`audioFiltered` (the RMS values were computed AFTER the HP/LP band-pass).

Units:
- `audioRmsL/R` and `audioThreshold` are RMS values in 24-bit sample counts. The
  INMP441 data arrives on a 32-bit I2S frame and is shifted (`sample >> 8`), so the
  full-scale amplitude is 2^23 = 8388608. dBFS (AES17, 0 dBFS = full-scale sine):
  `dBFS = 20*log10(rms*sqrt(2)/2^23)`. Estimated sound pressure: `SPL ≈ dBFS + 120`
  (INMP441 sensitivity −26 dBFS @ 94 dB SPL, 1 kHz). **This scale assumes the
  32-bit I2S configuration with `>>8`; a 16-bit I2S configuration would change it.**
- `potRaw` is the EMA-filtered 12-bit ADC reading (0..4095) of the threshold
  potentiometer; percent of travel = potRaw / 4095 × 100.
- IMU acceleration (ImuPacket) is in raw counts (MPU-9250 or ICM-20948, per TLV 0x34) at ±4 g full scale:
  g = counts / 8192. With `accelLpAlphaX1000` > 0 the published counts are
  EMA-filtered on-device (same scale).
- The IMU stream carries TREATED data only (mounting + zero + configured filters
  applied); raw-and-treated side by side is intentionally NOT exported, to keep the
  high-rate flow simple. Filters can be toggled live from the config-app.
- The quaternion source is Madgwick fusion on the ESP32 over the selected IMU (MPU-9250
  of the GY-91, or ICM-20948; Quat6 = gyro+accel; Quat9 adds the AK8963 / AK09916
  magnetometer). The wire format is
  identical to the previous DMP-based firmware.
- DiagPacket's `fifoOverflows` slot now carries the IMU I2C read-error count
  (same offset and type; there is no FIFO anymore).

## AudioChunkPacket (208 bytes, type 0x05, ~21 Hz, diag characteristic, opt-in)

Decimated RAW audio for the config-app's spectrum analyzer — **the FFT runs in the
frontend**, the firmware only ships band-limited samples. Mono mix (L+R)/2,
DC-removed, decimated 16 kHz -> 2 kHz by average-of-8 (crude anti-alias, adequate
for a tuning visualizer), int16 = top 16 bits of the 24-bit samples. ~4.3 kB/s.
Requires MTU >= 211.

**Opt-in since firmware v8:** sent only while `audioRawStream` (TLV `0x1F`) is set AND
the diag characteristic has a subscriber. Off by default, because this one stream is
~4.3 kB/s and ~45 ms/s of air time — more than every other packet on the link put
together (see the byte budget in the firmware README). Nothing else depends on it: the
RMS and peak the application needs now arrive measured, on every ImuPacket. Turn it on
to tune the band-pass against the live spectrum, then turn it off.

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x05) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampMs |
| 8 | 2 | u16 | sampleRateHz (2000) |
| 10 | 1 | u8 | count (96) |
| 11 | 1 | u8 | flags (bit0 = band-pass enabled; the samples are raw regardless) |
| 12 | 2 | u16 | hpHz (configured high-pass, echoed for the UI) |
| 14 | 2 | u16 | lpHz (configured low-pass, echoed for the UI) |
| 16 | 192 | i16[96] | samples (little-endian) |

## Command opcodes (characteristic `...0002`, write)

| Opcode | Meaning |
|---|---|
| `0x00` / `0x01` | reserved (legacy LED command, removed — no client ever sent it; ignored, never reuse) |
| `0x10` | zero orientation |
| `0x11` | save config |
| `0x12` | factory reset |
| `0x13` | reboot |
| `0x14` | mounting calibration |
| `0x15` | start raw magnetometer capture (streams 0x06 packets; auto-stops after 180 s) |
| `0x16` | stop raw magnetometer capture |
| `0x20` + ASCII PIN | unlock (PIN bytes follow the opcode) |

Unlock is per connection and rate-limited: after 5 consecutive failed attempts the
connection's unlock requests are refused (even with the correct PIN) until 30 s
after the last failure. A successful unlock or a disconnect clears the streak.

## Config TLV (characteristic `...0004`)

A sequence of records `id u8 | len u8 | value (len bytes, little-endian)`.

Reading the TLV requires no PIN **by design**: any connected central may inspect
thresholds, rates, pins and the BLE name (the PIN itself is write-only and never
serialized). Only TLV writes and the sensitive commands are gated by the unlock.

Persistence (command `0x11`) stores this same TLV serialization in NVS — not a struct
dump — and loads it leniently (unknown ids and invalid values are skipped; the
cross-field rules are re-established on defaults), so a config saved by one firmware
version survives schema changes. The PIN and the mounting quaternion have no readable
TLV register and are persisted under their own NVS keys.

| id | type | default | field |
|---|---|---|---|
| 0x01 | u8 | 0 | orientationMode (0=Quat6 relative, 1=Quat9) |
| 0x02 | u8 | 50 | imuNotifyHz (10–200). Also sets the selected IMU's sample-rate divisor (1 kHz base on the MPU-9250, 1125 Hz on the ICM-20948), applied live; the notify throttle paces the exact rate |
| 0x03 | u8 | 0 | slerpAmount 0–100 (0=off) |
| 0x04 | u16 | 0 | accelLpAlphaX1000 — EMA on published accel counts (0=off/raw) |
| 0x05 | u8 | 0 | imuDlpfMode — anti-alias filter of the selected IMU; the index means different cut-offs per chip. 0 = auto (widest DLPF below the output Nyquist: 50 Hz → 20 Hz on the MPU-9250, 11.6 Hz on the ICM-20948: the ICM cut-offs sit just above the 0.47×rate guard, so it lands one index narrower than the MPU at the same rate), 1..7 force the index. MPU-9250 DLPF_CFG: 1=184, 2=92, 3=41, 4=20, 5=10, 6=5, 7=3600 Hz. ICM-20948 GYRO_DLPFCFG (gyro values; the accel cut-offs differ slightly and use the same index): 1=151.8, 2=119.5, 3=51.2, 4=23.9, 5=11.6, 6=5.7, 7=361.4 Hz |
| 0x06 | u8 | 2 | gyroFsSel — gyro full scale: 0=±250, 1=±500, 2=±1000, 3=±2000 dps (131/65.5/32.8/16.4 LSB/dps) |
| 0x07 | u16 | 50 | fusionGainX100 — AHRS feedback gain ×100 (0–500); 0 = gyroscope only, which also disables the rejection features |
| 0x08 | u8 | 10 | accelRejectionDeg — inclination disagreement above which the accelerometer is ignored (0–90; 0=off) |
| 0x09 | u8 | 10 | magRejectionDeg — heading disagreement above which the magnetometer is ignored (0–90; 0=off) |
| 0x0A | u8 | 5 | rejectionTimeoutS — recovery trigger timeout (0–60; 0 disables BOTH rejections) |
| 0x0B | u16 | 200 | biasRestThDpsX100 — rest detected when the rate deviates from its own low-passed value by less than this (1–2000) |
| 0x0C | u16 | 1000 | biasMaxDpsX100 — plausibility bound and clip for the gyro offset (1–5000); 1000 (= 10 °/s) leaves headroom over the zero-rate offset both chips specify (MPU-9250 ±5 °/s) |
| 0x0D | u8 | 4 | biasTauS — bias estimate time constant at rest, seconds (1–60) |
| 0x0E | u8 | 0 | quatSmoothMode — 0=off, 1=fixed slerp (uses 0x03), 2=1-euro filter (uses 0x0F/0x1C) |
| 0x0F | u16 | 100 | oneEuroFcMinX100 — 1-euro cut-off at rest, Hz ×100 (1–5000) |
| 0x10 | u8 | 1 | audioEnabled |
| 0x11 | u16 | 250 | audioFastAlphaX1000 |
| 0x12 | u16 | 995 | audioSlowAlphaX1000 |
| 0x13 | u16 | 300 | debounceMs |
| 0x14 | u8 | 1 | potControlsThreshold |
| 0x15 | u32 | 100000 | thresholdFixed (used when 0x14=0) |
| 0x16 | u8 | 1 | audioBpfEnabled — breath band-pass (HP then LP) before the RMS/peak detector |
| 0x17 | u16 | 10 | audioHpHz (1–7000) |
| 0x18 | u16 | 500 | audioHpQX100 (10–2000; 500 = Q 5.00. Q > 0.707 peaks ~20·log10(Q) dB at the cutoff) |
| 0x19 | u16 | 200 | audioLpHz (1–7000; must stay > audioHpHz — validated on the final state) |
| 0x1A | u16 | 500 | audioLpQX100 (10–2000) |
| 0x1B | u16 | 300 | potEmaAlphaX1000 — potentiometer EMA (300 = legacy 0.3) |
| 0x1C | u16 | 20 | oneEuroBetaX1000 — 1-euro cut-off slope, Hz per dps ×1000 (0–10000) |
| 0x1D | u8 | 0 | accelHampelKX10 — Hampel spike filter on the published accel, k ×10 (0=off, ≤100) |
| 0x1F | u8 | 0 | audioRawStream — raw 2 kHz audio stream on/off (0/1). **Off by default:** ~4.3 kB/s and ~45 ms/s of air time while on. Only the spectrum view needs it |
| 0x1E | i16[12] | identity | magCal — ONE 24-byte record: hard iron x/y/z in 0.1 µT, then the soft-iron matrix row-major in 1/4096. Default = `0,0,0` + `4096,0,0, 0,4096,0, 0,0,4096` |
| 0x20 | u8 | 1 | co2Enabled |
| 0x22 | u16 | 5000 | co2StaleTimeoutMs |
| 0x23 | u16 | 2100 | virtualPeakPpm |
| 0x24 | u16 | 1000 | virtualDelayMs |
| 0x25 | u16 | 17000 | virtualHoldMs |
| 0x26 | u8 | 0 | co2RawLogMode |
| 0x28 | u16 | 1000 | co2EmaAlphaX1000 — EMA over accepted readings (1000 = off) |
| 0x30 | u8 | 2 | soundLedPin (valid output GPIOs only; 6–11 (flash) and nonexistent pins rejected) |
| 0x31 | u8 | 4 | co2LedPin (same validation as 0x30) |
| 0x32 | u16 | 2000 | co2AlertPpm |
| 0x34 | u8 | 0 | imuChip — which IMU is fitted: 0 = unconfigured (IMU off; the factory default, on purpose), 1 = ICM-20948, 2 = GY-91 / MPU-9250. Range 0–2. Persisted in NVS; **takes effect after a reboot** (the driver is chosen once, at boot) |
| 0x35 | u8 | — | imuDetected — **READ-ONLY.** Appended by the firmware on every config read; **absent from NVS**. Bits 0–1 = the `imuChip` value in force *at boot*; bit 2 (`0x04`) = the sensor was actually found. A config write that contains `0x35` fails as a whole, so a client must never include it. Comparing bits 0–1 with `0x34` tells "switched, reboot pending" apart from "sensor missing" |
| 0x36 | u8 | 8 | magAxisRemap — mapping of the magnetometer axes onto the accel/gyro frame, 0–23 (index into a fixed table; default 8 = `+Y +X -Z`, i.e. out.x=+in.y, out.y=+in.x, out.z=-in.z). Applied live. The firmware publishes calibration-capture samples already remapped, so **changing it invalidates an existing 0x1E calibration** (remap first, calibrate after). Order: permutation groups lexicographic (XYZ, XZY, YXZ, YZX, ZXY, ZYX), then the four sign patterns whose product equals the permutation's parity, from `+++` with `+` before `-`, leftmost axis slowest; authority `Gaiaball/src/logic/MagAxisRemap.cpp` |
| 0x40 | char[≤16] | "Globo_1" | bleName |
| 0x41 | char[4] | "1234" | pin (write-only: never serialized/readable) |
| 0x50 | u8 | 0 | serialLogEnabled — USB serial logging master switch (default OFF: log formatting steals CPU) |

The magnetometer calibration (0x1E) is the only multi-value register: writes must be
exactly 24 bytes, and a record whose soft-iron **diagonal** contains a zero is rejected
(it would silently zero the magnetometer and make Quat9 unusable).

A full config read is now ~190 bytes, so a central that cannot do a long attribute read
will truncate it; Web Bluetooth and NimBLE handle this transparently.

The two LED pins (0x30, 0x31) are additionally validated **as a set**: a write
whose resulting configuration puts both LED functions on the same GPIO is rejected
atomically (one pin = one function). Reserved ids, never to be reused: 0x21 (was
co2ChecksumVariant) and 0x27 (was co2Protocol) — both removed when the SC8-CO2-5K
datasheet fixed the frame format; 0x33 (was statusLedPin; removed with the legacy
LED command). `0x35` is a read-only status register, not configuration: it has no NVS key
and no write path.

## CO2 sensor protocol (UART1, 9600 8N1, listen-only)

The board's sensor is an **SC8-CO2-5K** (Shenzhen Shenchen Technology, often listed
as "YYS SC8"; KiCAD value `Sensor_CO2_SC8`): an NDIR sensor, 400–5000 ppm, that
**actively outputs one frame per second** after power-on. Per its datasheet
(SC8-CO2-5K V1.4, section 8), sending the sensor ANY query stops the active output
until the next power cycle — so the firmware opens UART1 **without a TX pin** and
never transmits.

A copy of the datasheet is kept at
[`datasheets/SC8-CO2-5K-v1.4.pdf`](datasheets/SC8-CO2-5K-v1.4.pdf) (source: the
OpenEnergyMonitor community upload, mirrored here because the original link may rot).

Active frame (16 bytes):

- `BYTE0 BYTE1` = `0x42 0x4D` (header).
- CO2 ppm = `BYTE6`·256 + `BYTE7` (big-endian).
- `BYTE15` = low byte of (`BYTE0` + `BYTE1` + … + `BYTE14`) — checksum.
- Golden vector (datasheet example): `42 4D 0C 51 09 A2 07 2B 01 35 05 81 20 08 00 AD`
  → 0x072B = **1835 ppm**.

The parser is byte-at-a-time and resyncs on the `42 4D` header after any corruption
or byte loss (the legacy firmware's CO2 path died permanently after one lost byte,
because it read fixed 16-byte windows with no realignment). `co2RawLogMode`
hex-dumps every received byte for bench captures.

## Euler convention (decoder side only, never on the wire)

Matches the legacy firmware: `qw = w`, `qx = y`, `qy = x`, `qz = -z`, then the
standard aerospace roll/pitch/yaw formulas on (qw, qx, qy, qz).

## Golden vectors (hex, byte order as transmitted)

ImuPacket: seq=258, timestampUs=0x0A0B0C0D, q=(1,0,0,0), acc=(1,-2,3), flags=0b1011
(quatValid+nineAxis+audioValid), rms=(0.00, −20.00) dBFS, peak=(0.00, −6.02) dBFS

```
01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 0B 00 00 30 F8 00 00 A6 FD
```

Co2Packet: seq=258, timestampMs=0x0A0B0C0D, ppm=1000, flags=0b101 (valid+virtualActive), ageS=7

```
01 02 02 01 0D 0C 0B 0A E8 03 05 07
```

SensorsPacket: seq=7, timestampMs=123456, rmsL=12345, rmsR=23456, threshold=100000,
potRaw=2048, flags=0b111

```
01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 07
```

DiagPacket: seq=1, uptimeS=0x00010203, imuHzX10=500, notifyHzX10=500, fifoOverflows=1,
i2sErrors=2, uartFrameErrors=3, freeHeapKb=200, minFreeHeapKb=150, flags=0b111,
ahrsFlags=0x11 (startup+biasAtRest), accelErrDeg=3, magErrDeg=12,
gyroBiasX100=(150,-75,0), connItvlMsX10=150 (15.0 ms)

```
01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07 11 03 0C 96 00 B5 FF 00 00 96 00
```

MagChunkPacket: seq=9, timestampMs=0x01020304, count=2, flags=0, samples=(10,-20,30) and
(10.1,-20.1,30.1) µT (the remaining 84 bytes are zero)

```
01 06 09 00 04 03 02 01 02 00 64 00 38 FF 2C 01 65 00 37 FF 2D 01 …00
```
