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
| `19b10005-e8f2-537e-4f6c-d104768a1214` | notify | diagnostics packets |

## Scales

- Quaternion: `int16 = round(float * 16384)` (Q14), saturating; `value / 16384` on decode.
- Accel: raw DMP counts, `int16` passthrough (no scaling).
- Non-finite quaternion input is encoded as identity (`W=16384`) with `quatValid` cleared.

## Packet type ids

`0x01` ImuSample, `0x02` Co2Sample, `0x03` Diagnostics.

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

## Command opcodes (characteristic `...0002`, write)

| Opcode | Meaning |
|---|---|
| `0x00` / `0x01` | legacy LED off / on |
| `0x10` | zero orientation |
| `0x11` | save config |
| `0x12` | factory reset |
| `0x13` | reboot |
| `0x14` | mounting calibration |
| `0x20` + ASCII PIN | unlock (PIN bytes follow the opcode) |

## Config TLV (characteristic `...0004`)

A sequence of records `id u8 | len u8 | value (len bytes, little-endian)`.

| id | type | default | field |
|---|---|---|---|
| 0x01 | u8 | 0 | orientationMode (0=Quat6 relative, 1=Quat9) |
| 0x02 | u8 | 50 | imuNotifyHz (10–60) |
| 0x03 | u8 | 0 | slerpAmount 0–100 (0=off) |
| 0x10 | u8 | 1 | audioEnabled |
| 0x11 | u16 | 250 | audioFastAlphaX1000 |
| 0x12 | u16 | 995 | audioSlowAlphaX1000 |
| 0x13 | u16 | 300 | debounceMs |
| 0x14 | u8 | 1 | potControlsThreshold |
| 0x15 | u32 | 100000 | thresholdFixed (used when 0x14=0) |
| 0x20 | u8 | 1 | co2Enabled |
| 0x21 | u8 | 0 | co2ChecksumVariant (0=sum bytes 0..13; 1=legacy sum 0..14) |
| 0x22 | u16 | 5000 | co2StaleTimeoutMs |
| 0x23 | u16 | 2100 | virtualPeakPpm |
| 0x24 | u16 | 1000 | virtualDelayMs |
| 0x25 | u16 | 17000 | virtualHoldMs |
| 0x26 | u8 | 0 | co2RawLogMode |
| 0x30 | u8 | 2 | soundLedPin |
| 0x31 | u8 | 4 | co2LedPin |
| 0x32 | u16 | 2000 | co2AlertPpm |
| 0x40 | char[≤16] | "Globo_1" | bleName |
| 0x41 | char[4] | "1234" | pin (write-only: never serialized/readable) |

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

DiagPacket: seq=1, uptimeS=0x00010203, imuHzX10=500, notifyHzX10=500, fifoOverflows=1,
i2sErrors=2, uartFrameErrors=3, freeHeapKb=200, minFreeHeapKb=150, flags=0b111

```
01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07
```
