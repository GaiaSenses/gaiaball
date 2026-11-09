# Gaiaball v2 firmware architecture

Target: ESP32 (esp32dev), Arduino core 2.0.17, NimBLE-Arduino 1.4.3. All code lives in
namespace `gaia` under `Gaiaball/src/`:

- `logic/` — pure, host-testable code (packets, TLV codec, filters, SC8 CO2 frame
  parser, peak detector, PIN gate, notify throttle). Covered by `pio test -e native`.
  LEDs have NO arbitration layer: each LED pin has exactly one driver
  (sound events → GPIO2, CO2 alert → GPIO4).
- `services/` — hardware/RTOS wrappers, one per peripheral.
- `App.h/.cpp` — owns every service, boots them, runs the App task.
- `Gaiaball.ino` — `setup(){ app.begin(); }`, `loop()` parks forever.

## Boot order (`App::begin`, on the Arduino loop task)

1. `Serial` 115200 + `Logger`
2. `ConfigStore` (NVS load) → `CommandGate::setPin(cfg.pin)` → install onChange (flag only)
3. `LedController` (two distinct pins; GPIO2 driven LOW first: strapping pin)
4. `BleService` (GATT + advertising start early)
5. `ImuService::begin` (I2C + MPU-9250 init, blocks up to ~5 s if the IMU is absent)
6. `PotService::begin`, `AudioService` (peak callback + threshold provider set first)
7. `Co2Service` (UART1)
8. `Diagnostics::begin` (task WDT 10 s, panic)
9. App task created, then `imu`, `audio`, `co2` tasks started

After `setup()` returns, `loop()` blocks in `vTaskDelay(portMAX_DELAY)`.

## Tasks

| Task | Core | Prio | Stack | Wakes on | Owns |
|---|---|---|---|---|---|
| `imu` | 1 | 3 | 6144 | `vTaskDelay` (adaptive: sample period / 4, clamped 2–5 ms — the GY-91 INT pin is not routed on the board) | MPU-9250 (I2C), Madgwick fusion, gyro-bias tracker, mounting/zero state |
| `audio` | 1 | 2 | 4096 | `i2s_read` (80 frames @ 16 kHz = 5 ms; 100 ms timeout) | I2S0, AudioLevelWindow, PeakDetector |
| `app` | 1 | 2 | 6144 | IMU queue (`xQueueReceive`, 20 ms timeout) | LEDs, pot ADC, virtual pulse (`VirtualCo2Pulse` member), NVS saves |
| `co2` | 1 | 1 | 3072 | `vTaskDelay(20 ms)` | UART1 RX (listen-only), SC8 Co2FrameParser |
| NimBLE host | 0 | (lib) | (lib) | BLE events | GATT, CommandGate unlock, config TLV apply |
| Arduino `loopTask` | 1 | 1 | — | never (parked) | — |

Every long-running task (`app`, `imu`, `audio`, `co2`) is registered with the 10 s task
watchdog (panic=true) and feeds it once per loop cycle; every blocking call in those
loops has a finite timeout (`i2s_read`: 100 ms). A stalled task — including a stalled
peripheral behind it — trips the WDT and reboots the ball instead of degrading
silently (it is a sealed sphere in an exhibition: auto-recovery beats a frozen ball).

## Data flow

```
                    core 1                                           core 0
 ┌──────────┐ ImuSample queue (depth 8,  ┌───────────────────────┐   ┌─────────────┐
 │ imu task │──drop-oldest)─────────────>│       app task        │──>│ BLE notify  │ IMU  (char 1)
 │ prio 3   │<─requestZero/Mounting──────│ prio 2                │──>│             │ CO2  (char 3)
 │          │──takeCapturedMounting─────>│  throttle → ImuPacket │──>│             │ Diag (char 5)
 │          │──MagRawSample queue───────>│  CO2 on change / 1 Hz │   └─────────────┘
 │          │  (depth 32, drop-oldest,   │  mag chunk (16/pkt)   │
                                         │  diag 1 Hz            │          │
 ┌──────────┐ onPeak + levels(): atomics │  LED update 20 Hz     │  BleCommand queue
 │audio task│──lastPeakMs_, peakPending_>│  pot poll 10 Hz       │<─(depth 8)──┤
 │ prio 2   │──rms/peak of the 20 ms win>│  → threshold_ atomic  │             │
 │          │<─threshold provider────────│  command dispatch     │  ConfigStore onChange
 └──────────┘  reads App::threshold_     │  config re-apply      │<─configDirty_ flag──┤
 ┌──────────┐ realPpm/frameCount atomics │  mount persist (NVS)  │   NimBLE host task
 │ co2 task │───────────────────────────>│  WDT feed             │   (0x20 unlock inline,
 │ prio 1   │                            └───────────────────────┘    config TLV apply)
 └──────────┘
```

### Cross-task rules

- **Audio → App**: the peak callback runs on the audio task and only stores
  `lastPeakMs_` + sets `peakPending_`. The App task consumes the flag, triggers its own
  `VirtualCo2Pulse` (pure logic, App-task-only member) and drives the sound LED. The
  `Co2Service` is real-sensor-only and has no pulse state.
- **App → Audio**: `PotService` is single-task (App). After each `poll()` the App stores
  `threshold()` in an `std::atomic<float>`; the audio task's threshold provider reads it.
- **Audio → App (envelope)**: the audio task publishes the pooled 20 ms levels (RMS and
  peak per channel) into four `std::atomic<float>`; the App task snapshots them with
  `AudioService::levels()` and appends them to the ImuPacket it was already sending.
  Atomics and not a queue because only the latest value matters: a control signal has
  no use for a backlog, and the IMU notify rate already paces the reads. The audio task
  runs at 200 blocks/s, so the envelope is never more than 5 ms older than the
  orientation sample it travels with.
- **BLE → App (link state)**: `BleService::connIntervalMsX10()` asks the NimBLE host for
  the live connection descriptor (safe from any task; it takes the host lock) and the
  App passes the value to `Diagnostics::collect`. The firmware requests 7.5–15 ms on
  connect, but the central decides, so the granted value is reported rather than
  assumed — it is the ceiling on every notify rate.
- **BLE → App (commands)**: every command except `0x20` (unlock, inline on the host task)
  goes through the BleCommand queue. Gated opcodes are authorized with
  `BleCommand::unlockedAtEnqueue`, never by re-querying the gate.
- **BLE → App (config)**: `ConfigStore::applyTlv` runs on the host task and fires onChange
  there; the handler only sets `configDirty_`. The App task then re-applies the config:
  `applyConfig` on IMU/audio/CO2/pot plus `App::applySnapshot` — the ONE place that
  copies config scalars into the App-task mirrors and derived settings (gate PIN,
  throttle rate only on an actual `imuNotifyHz` change, virtual-pulse parameters).
  Config writes are not persisted until command `0x11`; persistence is the TLV
  serialization (stable ids, lenient load), not a struct blob, so schema bumps keep
  the user's stored config.
- **IMU → App (mounting)**: `0x14` sets a request flag; the IMU task captures the mounting
  quaternion and hands it over; the App task calls `ConfigStore::setMountQuat` + `save()`.
- **App → IMU (settings)**: the IMU pipeline has six configurable stages, so
  `ImuService::applyConfig` translates the whole config into ONE `ImuSettings` struct
  (`imuSettingsFrom`, a pure function) and hands it over under a short critical section.
  The IMU task picks it up at the top of its cycle and reprograms only what changed
  (gyro range, rate and DLPF are register writes; the rest is filter state). A dozen
  independent atomics would let a config write leave the task running half of the old
  setup and half of the new one.
- **IMU → App (magnetometer capture)**: `0x15` starts the capture; the IMU task pushes
  each RAW magnetometer triplet into a depth-32 queue (drop-oldest, so a stalled reader
  cannot stall the capture) and the App task batches 16 per `MagChunkPacket`. The
  capture stops on `0x16` or after 180 s, whichever comes first. Nothing is fitted on
  the device: the config-app solves the ellipsoid and writes back TLV `0x1E`.

### App task loop

```
for (;;) {
  xQueueReceive(imuQueue, 20 ms)  → throttle(imuNotifyHz) → makeImuPacket → notifyImu
  config dirty?                   → re-apply (see above)
  peak pending?                   → triggerVirtualPulse
  every 100 ms                    → pot.poll, publish threshold atomic
  every  50 ms                    → sound LED = peak<100 ms || virtual pulse active
                                    CO2 notify if ppm/flags changed or 1 s since last
                                    led.update (CO2 LED from the real, non-stale ppm)
  every  200 ms                   → sensors packet (audio RMS, threshold, pot) on diag char
  every loop (chunk every 48 ms)  → raw-audio chunk (2 kHz int16) on diag char, if subscribed
  every 1000 ms                   → diag collect + notifyDiag

## Per-core execution detail (zoom into the firmware)

```
CORE 0 (PRO_CPU) - radio only              CORE 1 (APP_CPU) - acquisition + app logic
┌───────────────────────────────┐          ┌─────────────────────────────────────────────┐
│ NimBLE host task              │          │ imu task · prio 3 · adaptive 2-5 ms tick    │
│  GATT server, 5 chars         │          │  INT_STATUS ready? → read accel/gyro(+mag)  │
│  notify → up to 2 centrals    │          │  → gyro-bias tracker → Madgwick (6/9 axis)  │
│  RX cmd write ────────────────┼─queue──▶ │  → mount⁻¹·q → zero⁻¹ → slerp → queue(8)    │
│  RX config TLV → ConfigStore  │          ├─────────────────────────────────────────────┤
│   (mutex) → dirty flag        │          │ audio task · prio 2 · blocks on i2s_read    │
├───────────────────────────────┤          │  block DC-removal → mono → decimator 2 kHz  │
│ BLE controller + radio ISRs   │          │  → chunk 48 ms ─▶ app (mux)                 │
└───────────────────────────────┘          │  → HP/LP biquads → RMS L/R → peak detector  │
        ▲        │                         ├─────────────────────────────────────────────┤
        │ notify │ commands                │ co2 task · prio 1 · 20 ms tick              │
        │        ▼                         │  drain RX → SC8 frame parse → EMA → ppm    │
┌───────┴───────────────┐                  ├─────────────────────────────────────────────┤
│ Site GaiaSenses       │                  │ app task · prio 2 · paced by IMU queue      │
│ config-app (bancada)  │                  │  1 queue → ImuPacket → notify (throttle)    │
└───────────────────────┘                  │  2 config dirty? re-apply to services       │
                                           │  3 pending peak? trigger virtual CO2        │
                                           │  4 100 ms: pot poll → threshold atomic      │
                                           │  5  50 ms: LEDs + CO2 notify (change/1 Hz)  │
                                           │  6 200 ms: SensorsPacket                    │
                                           │  7 chunk ready? AudioChunkPacket (~21 Hz)   │
                                           │  8   1 s: DiagPacket                        │
                                           │  9 drain cmd queue · persist mount · WDT    │
                                           └─────────────────────────────────────────────┘
```
  drain command queue (non-blocking)
  persist captured mounting (if any)
  WDT feed
}
```

No `delay()` and no heap allocation in steady state; all periods are wrap-safe
`millis()` differences.
