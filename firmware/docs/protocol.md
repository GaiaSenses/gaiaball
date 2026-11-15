# Gaiaball BLE wire protocol v1

All multi-byte integers are **little-endian**; structs are `__attribute__((packed))`.
Protocol version: `1` (first byte of every packet). Source of truth:
`Gaiaball/src/logic/Packets.h`; golden vectors are fixed by `test/test_protocol`.

## BLE UUIDs

Service `19b10000-e8f2-537e-4f6c-d104768a1214`.

| UUID | Dir | Purpose |
|---|---|---|
| `19b10001-e8f2-537e-4f6c-d104768a1214` | notify + read | IMU packets |
| `19b10002-e8f2-537e-4f6c-d104768a1214` | write + write-without-response | commands |
| `19b10003-e8f2-537e-4f6c-d104768a1214` | notify + read | CO2 packets |
| `19b10004-e8f2-537e-4f6c-d104768a1214` | read + write | config TLV |
| `19b10005-e8f2-537e-4f6c-d104768a1214` | notify + read | diagnostics (0x03, 1 Hz), live-sensor (0x04, 5 Hz), raw-audio (0x05, ~21 Hz, **only while TLV 0x1F is on**) and raw-magnetometer (0x06, ~3 Hz while calibrating) packets |
| `19b10006-e8f2-537e-4f6c-d104768a1214` | notify + read | globe-navigation packets (0x07, see [NavPacket](#navpacket-22-bytes-type-0x07-own-characteristic)) |

The navigation characteristic (`…0006`) is **optional for clients**: firmware that
predates it does not expose it, so a client must treat its absence as "navigation not
supported" and keep working with the other five. A read on it returns the last value
the firmware set (an all-zero packet right after boot).

**The firmware never acknowledges what it receives.** A command write (`…0002`) or a
config write (`…0004`) produces no response payload, and a config write that is
rejected (locked connection, invalid TLV) is dropped silently: the ATT write itself still
succeeds. A client therefore reads `…0004` back to see what took effect, and sends
command `0x11` to persist it.
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
AudioChunk, `0x06` MagChunk, `0x07` Nav. The diag characteristic carries 0x03, 0x04,
0x05 and 0x06; receivers must dispatch on the type byte. The nav packet (0x07) has a
characteristic of its own.

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

Flags: bit0 `valid`, bit1 `stale`, bit2 `virtualActive`, bit3 `trigger`.

`trigger` is a level with hysteresis on the **reported** ppm (the virtual pulse counts):
it rises when `ppm > co2TriggerPpm` (TLV 0x29, default 1200) and falls when
`ppm <= co2TriggerPpm - co2TriggerHystPpm` (TLV 0x2A, default 50; a hysteresis not
smaller than the trigger level means "falls at 0"). A `stale` reading forces it off
unless the virtual pulse is active (the pulse then supplies the ppm). A flip changes the
flags, so the packet is sent immediately instead of waiting for the 1 Hz keepalive. The
packet stays 12 bytes and older decoders simply ignore the new bit.

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
already remapped to the gyro/accel chip frame by the configurable magnetometer axis
remap (TLV 0x36; default 8 = `+Y +X -Z`, i.e. the AK8963's X and Y swapped and Z
inverted). The capture (`0x15`) only produces samples in **Quat9** mode
(`orientationMode` = 1): the magnetometer is not read in Quat6.

## NavPacket (22 bytes, type 0x07, own characteristic)

The globe pose, computed **on the ball** from the orientation (mirror or joystick) and
sent on characteristic `19b10006-…` so the site only has to draw it. Needs MTU >= 25
(22-byte payload + 3-byte ATT header).

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | version (1) |
| 1 | 1 | u8 | type (0x07) |
| 2 | 2 | u16 | seq |
| 4 | 4 | u32 | timestampUs (of the IMU sample the pose was computed from) |
| 8 | 1 | u8 | mode (0 off, 1 mirror, 2 joystick) |
| 9 | 1 | u8 | flags |
| 10 | 4 | i32 | latE7 — screen-centre latitude, degrees × 1e7, −90..90 |
| 14 | 4 | i32 | lonE7 — screen-centre longitude, degrees × 1e7, (−180, 180] |
| 18 | 2 | i16 | bearingX100 — Mapbox bearing, degrees × 100, (−180, 180] |
| 20 | 2 | u16 | speedDegSX10 — reported globe speed, deg/s × 10 |

Flags: bit0 `valid` (the pose is live: the IMU orientation is valid), bit1 `settled`,
bit2 `moving`.

- **`settled` is the authority for "stopped".** It is the output of the motion state
  machine below (hysteresis plus hold time).
- `moving` is instantaneous and has no hold time: the **reported** speed is above the
  active stop threshold. It is not the complement of `settled`: in mirror mode the speed
  can sit between the stop and the start threshold while the machine is still settled
  (or still moving), so flags `0x07` (valid + settled + moving) is a legal value. In
  joystick mode the reported speed is 0 while settled, so `moving` is never set then.
- Reported speed: mirror = the measured angular speed of the globe (over the ~100 ms
  window below); joystick = the commanded speed S(θ), and **0 while settled** (the globe
  is not integrated then).
  `u16` saturates at 65535 (6553.5 deg/s).

### Frames and conventions

- Reference frame = the pose at power-on (or at the last zero, command `0x10`): **X = the
  horizontal direction the board's X axis (the nose of the plane in the config-app 3D
  view) pointed to at that moment**; Z = up; **Y = X rotated 90° to the left**
  (right-handed). The ball has no marked "front": the motion is relative to that pose, and
  the side that faced the audience at that moment decides how gestures look on screen
  (the globe is drawn as seen from +X).
- Globe frame E (unit sphere): x → (lat 0, lon 0), y → (lat 0, lon 90° E), z → north pole.
- **Identity orientation shows the reference point** (TLV 0x62, default Campinas/SP,
  lat −22.905556, lon −47.060833) **at the screen centre with north up** (bearing 0).
- Bearing: Mapbox convention, degrees clockwise from north of the direction that points
  UP on the screen, normalised to (−180, 180].
- The `lat ±85.05°` clamp of Mapbox's map centre is a **client** limitation: the packet
  carries the true pose up to ±90° and the client decides how to render near the poles.

### Laws

- **Mirror** (`navMode` 1): the globe orientation G is the ball's orientation q (already
  relative to the zero).
- **Joystick** (`navMode` 2): (u, θ) is the shortest axis-angle of q. The commanded speed
  is `S(θ) = joyMaxSpeedDegS · min(θ / joyFullAngleDeg, 1) ^ (joyCurveX10 / 10)` deg/s,
  and G′ = normalize(Δ ⊗ G) with Δ the rotation about u by `S · dt` (dt clamped to
  [0, 0.05] s). The tilt thus steers the globe; the globe does not follow the ball.
- **Motion state machine:** the initial state is settled. Speed ≥ start → moving. While
  moving, speed ≤ stop accumulates low time (reset whenever speed > stop); low time ≥
  `navStopMs` → settled. Mirror uses `navStartDegS` / `navStopDegS` on the measured globe
  speed; joystick uses `joyStartDegSX10` / `joyStopDegSX10` (÷10) on the **commanded**
  speed S(θ), not on G's own speed (G is frozen while settled, so its speed could never
  restart it).
- **Mirror speed window:** the measured speed is the angle between the current G and the
  newest G at least **100 ms** older, divided by the real time between the two samples
  (the firmware keeps a ring of past poses sized for 200 Hz; on the first samples after
  a reset it uses the oldest one it has). Sample to sample, an 8–12 Hz hand tremor reads
  above the stop threshold; over the window it averages out, so a hand-held ball can
  settle. The window also means the speed takes up to 100 ms to fall after the ball
  stops, before the `navStopMs` hold starts.
- **Mirror drift (settled → moving):** while settled, a drift of **≥ 3°** from the pose
  captured when the machine settled (or from the first sample after a reset) also leaves
  settled, whatever the speed, so a slow, deliberate rotation below `navStartDegS` still
  reaches the clients. The pose is recaptured on every settle. The 3° is a fixed
  firmware constant, provisional and tuned on the bench. Joystick mode has no drift rule.

### Rate, keepalive and resets

- A packet is produced every N IMU samples, **N = max(1, round(imuNotifyHz / navNotifyHz))**
  (integer count, so the spacing is regular and the nav rate never exceeds the IMU rate).
  The count runs whether or not anyone is subscribed; packets are only sent to
  subscribers. With the defaults (50 Hz and 50 Hz) N = 1; at imuNotifyHz 100 or 200 and
  navNotifyHz 50, N = 2 or 4. The timestamp is that of the sample behind the pose.
- **Keepalive:** a packet with `valid = 0` and every pose field 0 (`mode` = the current
  mode) is sent at **1 Hz** when `navMode = 0`, or when no IMU sample arrived for
  more than 500 ms (no IMU configured, sensor stalled). It also covers "no sample since
  boot".
- **Live parameters:** the joystick law (0x63–0x65) and the settle thresholds and hold
  time (0x66–0x6A) apply live and do **not** move the globe (a sensitivity slider must
  not snap it back).
- **Reset to the reference point** (G = identity, state settled, first sample after it
  has dt = 0): only on a `navMode` change, a `navRef` change, or a change of the IMU's
  reference — zero (`0x10`), mounting calibration (`0x14`), or switching Quat6 ↔ Quat9
  (`orientationMode`). **The zero is not persisted, by design**: the ball is powered on
  resting on a flat surface and that boot pose is the reference (in Quat6 the heading is
  the heading at boot). Zeroing only re-references without a reboot.

Golden vectors (hex, byte order as transmitted):

A — seq 258, timestampUs 12345678, mode 1, flags 0x03 (valid + settled),
latE7 −229055560, lonE7 −470608330, bearingX100 4500, speedDegSX10 123

```
01 07 02 01 4E 61 BC 00 01 03 B8 E3 58 F2 36 16 F3 E3 94 11 7B 00
```

B — format extremes, not a reachable state (every field at a limit of its type or range):
seq 65535, timestampUs 4294967295, mode 2, flags 0x06 (settled + moving),
latE7 900000000, lonE7 −1800000000, bearingX100 −18000, speedDegSX10 65535

```
01 07 FF FF FF FF FF FF 02 06 00 E9 A4 35 00 2E B6 94 B0 B9 FF FF
```

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
for a tuning visualizer), int16 = top 16 bits of the 24-bit samples. 208 B × 20.8
packets/s ≈ 4.3 kB/s.
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
| `0x10` | zero orientation (also resets the navigation globe; **not** persisted across power cycles) |
| `0x11` | save config |
| `0x12` | factory reset |
| `0x13` | reboot |
| `0x14` | mounting calibration (also clears the zero and resets the navigation globe) |
| `0x15` | start raw magnetometer capture (streams 0x06 packets; auto-stops after 180 s; samples only in Quat9 mode) |
| `0x16` | stop raw magnetometer capture |
| `0x20` + ASCII PIN | unlock (PIN bytes follow the opcode) |

Unlock is per connection and rate-limited: after 5 consecutive failed attempts the
connection's unlock requests are refused (even with the correct PIN) until 30 s
after the last failure. A successful unlock or a disconnect clears the streak.

Commands get no response either (see the note under the UUID table): an unlock is
confirmed by a subsequent config write taking effect, and a saved config by reading it
back after a reboot.

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
| 0x29 | u16 | 1200 | co2TriggerPpm — level at which the Co2Packet `trigger` flag rises (400–10000) |
| 0x2A | u16 | 50 | co2TriggerHystPpm — the flag falls at or below `co2TriggerPpm − this` (0–2000) |
| 0x30 | u8 | 2 | soundLedPin (valid output GPIOs only; 6–11 (flash) and nonexistent pins rejected) |
| 0x31 | u8 | 4 | co2LedPin (same validation as 0x30) |
| 0x32 | u16 | 2000 | co2AlertPpm |
| 0x34 | u8 | 0 | imuChip — which IMU is fitted: 0 = unconfigured (IMU off; the factory default, on purpose), 1 = ICM-20948, 2 = GY-91 / MPU-9250. Range 0–2. Persisted in NVS; **takes effect after a reboot** (the driver is chosen once, at boot) |
| 0x35 | u8 | — | imuDetected — **READ-ONLY.** Appended by the firmware on every config read; **absent from NVS**. Bits 0–1 = the `imuChip` value in force *at boot*; bit 2 (`0x04`) = the sensor was actually found; bit 3 (`0x08`) = its magnetometer answered (clear in Quat9 means the fusion is running 6-axis). A config write that contains `0x35` fails as a whole, so a client must never include it. Comparing bits 0–1 with `0x34` tells "switched, reboot pending" apart from "sensor missing" |
| 0x36 | u8 | 8 | magAxisRemap — mapping of the magnetometer axes onto the accel/gyro frame, 0–23 (index into a fixed table; default 8 = `+Y +X -Z`, i.e. out.x=+in.y, out.y=+in.x, out.z=-in.z). Applied live. The firmware publishes calibration-capture samples already remapped, so **changing it invalidates an existing 0x1E calibration** (remap first, calibrate after). Order: permutation groups lexicographic (XYZ, XZY, YXZ, YZX, ZXY, ZYX), then the four sign patterns whose product equals the permutation's parity, from `+++` with `+` before `-`, leftmost axis slowest; authority `Gaiaball/src/logic/MagAxisRemap.cpp` |
| 0x40 | char[≤16] | "Globo_1" | bleName |
| 0x41 | char[4] | "1234" | pin (write-only: never serialized/readable) |
| 0x50 | u8 | 0 | serialLogEnabled — USB serial logging master switch (default OFF: log formatting steals CPU) |
| 0x60 | u8 | 1 | navMode — 0 = off, 1 = mirror, 2 = joystick (0–2). A change resets the globe |
| 0x61 | u8 | 50 | navNotifyHz — NavPacket rate (10–200); the effective rate is imuNotifyHz / N, see [NavPacket](#navpacket-22-bytes-type-0x07-own-characteristic) |
| 0x62 | 8 bytes | −22905556, −47060833 | navRef — reference point shown at identity: `i32 latE6` (−90000000..90000000) then `i32 lonE6` (−180000000..180000000), degrees × 1e6 (default Campinas/SP). A change resets the globe |
| 0x63 | u16 | 90 | joyMaxSpeedDegS — joystick speed at full tilt, deg/s (1–720) |
| 0x64 | u8 | 60 | joyFullAngleDeg — tilt that commands full speed (5–90) |
| 0x65 | u8 | 30 | joyCurveX10 — response exponent × 10 (10–40; 30 = cubic) |
| 0x66 | u8 | 20 | navStartDegS — mirror: globe speed that starts motion, deg/s (1–200) |
| 0x67 | u8 | 5 | navStopDegS — mirror: speed at or below which settling counts, deg/s (1–100; must stay < 0x66) |
| 0x68 | u16 | 420 | navStopMs — time continuously below the stop speed to declare settled (50–5000) |
| 0x69 | u16 | 10 | joyStartDegSX10 — joystick: commanded speed that starts motion, deg/s × 10 (1–2000) |
| 0x6A | u16 | 5 | joyStopDegSX10 — joystick: commanded speed that settles, deg/s × 10 (1–2000; must stay < 0x69) |

The magnetometer calibration (0x1E) is the only multi-value register: writes must be
exactly 24 bytes, and a record whose soft-iron **diagonal** contains a zero is rejected
(it would silently zero the magnetometer and make Quat9 unusable).

A full config read is now ~240 bytes (239 of serialized registers plus the 3-byte
read-only `0x35`), so a central that cannot do a long attribute read
will truncate it; Web Bluetooth and NimBLE handle this transparently.

The two LED pins (0x30, 0x31) are additionally validated **as a set**: a write
whose resulting configuration puts both LED functions on the same GPIO is rejected
atomically (one pin = one function). Reserved ids, never to be reused: 0x21 (was
co2ChecksumVariant) and 0x27 (was co2Protocol) — both removed when the SC8-CO2-5K
datasheet fixed the frame format; 0x33 (was statusLedPin; removed with the legacy
LED command). `0x35` is a read-only status register, not configuration: it has no NVS key
and no write path.

Two more cross-field rules apply to the **resulting** state of a write, exactly like
`audioLpHz > audioHpHz`: `navStopDegS` (0x67) < `navStartDegS` (0x66), and
`joyStopDegSX10` (0x6A) < `joyStartDegSX10` (0x69). A write that would leave either pair
inverted or equal is rejected whole, so changing one half of a pair alone must respect the
stored value of the other. `navRef` (0x62) must be exactly 8 bytes with both coordinates
inside their range. All navigation registers are persisted by `0x11` like the rest; a
config loaded from NVS that breaks a pair falls back to that pair's defaults.

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

The two NavPacket vectors are listed in the [NavPacket](#navpacket-22-bytes-type-0x07-own-characteristic) section.

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
