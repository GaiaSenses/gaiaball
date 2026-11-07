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
| `19b10005-e8f2-537e-4f6c-d104768a1214` | notify | diagnostics (0x03, 1 Hz), live-sensor (0x04, 5 Hz) and raw-audio (0x05, ~21 Hz) packets |

## Scales

- Quaternion: `int16 = round(float * 16384)` (Q14), saturating; `value / 16384` on decode.
- Accel: raw MPU-9250 counts (±4 g, 8192 LSB/g), `int16` passthrough (no scaling).
- Non-finite quaternion input is encoded as identity (`W=16384`) with `quatValid` cleared.

## Packet type ids

`0x01` ImuSample, `0x02` Co2Sample, `0x03` Diagnostics, `0x04` Sensors, `0x05`
AudioChunk. The diag characteristic carries 0x03, 0x04 and 0x05; receivers must
dispatch on the type byte.

## ImuPacket (23 bytes)

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

Flags: bit0 `quatValid`, bit1 `nineAxis`, bit2 `moving`.

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

## DiagPacket (23 bytes)

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

Flags: bit0 `imuOk`, bit1 `audioOk`, bit2 `co2Ok`.

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
- IMU acceleration (ImuPacket) is in raw MPU-9250 counts at ±4 g full scale:
  g = counts / 8192. With `accelLpAlphaX1000` > 0 the published counts are
  EMA-filtered on-device (same scale).
- The IMU stream carries TREATED data only (mounting + zero + configured filters
  applied); raw-and-treated side by side is intentionally NOT exported, to keep the
  high-rate flow simple. Filters can be toggled live from the config-app.
- The quaternion source is Madgwick fusion on the ESP32 over the GY-91's MPU-9250
  (Quat6 = gyro+accel; Quat9 adds the AK8963 magnetometer). The wire format is
  identical to the previous DMP-based firmware.
- DiagPacket's `fifoOverflows` slot now carries the MPU-9250 I2C read-error count
  (same offset and type; there is no FIFO anymore).

## AudioChunkPacket (208 bytes, type 0x05, ~21 Hz, diag characteristic)

Decimated RAW audio for the config-app's spectrum analyzer — **the FFT runs in the
frontend**, the firmware only ships band-limited samples. Mono mix (L+R)/2,
DC-removed, decimated 16 kHz -> 2 kHz by average-of-8 (crude anti-alias, adequate
for a tuning visualizer), int16 = top 16 bits of the 24-bit samples. ~4.4 kB/s.
Requires MTU >= 211. Sent only while the diag characteristic has a subscriber.

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
| 0x02 | u8 | 50 | imuNotifyHz (10–200). Also sets the MPU-9250 sample-rate divisor (1 kHz base), applied live; the notify throttle paces the exact rate |
| 0x03 | u8 | 0 | slerpAmount 0–100 (0=off) |
| 0x04 | u16 | 0 | accelLpAlphaX1000 — EMA on published accel counts (0=off/raw) |
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
| 0x40 | char[≤16] | "Globo_1" | bleName |
| 0x41 | char[4] | "1234" | pin (write-only: never serialized/readable) |
| 0x50 | u8 | 0 | serialLogEnabled — USB serial logging master switch (default OFF: log formatting steals CPU) |

The two LED pins (0x30, 0x31) are additionally validated **as a set**: a write
whose resulting configuration puts both LED functions on the same GPIO is rejected
atomically (one pin = one function). Reserved ids, never to be reused: 0x21 (was
co2ChecksumVariant) and 0x27 (was co2Protocol) — both removed when the SC8-CO2-5K
datasheet fixed the frame format; 0x33 (was statusLedPin; removed with the legacy
LED command).

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

ImuPacket: seq=258, timestampUs=0x0A0B0C0D, q=(1,0,0,0), acc=(1,-2,3), flags=0b011

```
01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 03
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
i2sErrors=2, uartFrameErrors=3, freeHeapKb=200, minFreeHeapKb=150, flags=0b111

```
01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07
```
