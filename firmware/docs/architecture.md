# Gaiaball v2 firmware architecture

Target: ESP32 (esp32dev), Arduino core 2.0.17, NimBLE-Arduino 1.4.3. All code lives in
namespace `gaia` under `Gaiaball/src/`:

- `logic/` — pure, host-testable code (packets, TLV codec, filters, CO2 parser, peak
  detector, PIN gate, S8 Modbus client, notify throttle). Covered by `pio test -e native`.
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
5. `ImuService::begin` (I2C + DMP init, blocks up to ~5 s if the IMU is absent)
6. `PotService::begin`, `AudioService` (peak callback + threshold provider set first)
7. `Co2Service` (UART1)
8. `Diagnostics::begin` (task WDT 10 s, panic)
9. App task created, then `imu`, `audio`, `co2` tasks started

After `setup()` returns, `loop()` blocks in `vTaskDelay(portMAX_DELAY)`.

## Tasks

| Task | Core | Prio | Stack | Wakes on | Owns |
|---|---|---|---|---|---|
| `imu` | 1 | 3 | 6144 | `vTaskDelay(5 ms)` | ICM-20948 (I2C), mounting/zero state |
| `audio` | 1 | 2 | 4096 | `i2s_read` (256 frames @ 16 kHz = 16 ms) | I2S0, PeakDetector |
| `app` | 1 | 2 | 6144 | IMU queue (`xQueueReceive`, 20 ms timeout) | LEDs, pot ADC, virtual pulse, NVS saves, WDT |
| `co2` | 1 | 1 | 3072 | `vTaskDelay(20 ms)` | UART1, S8 Modbus poll (2 s) or Co2FrameParser |
| NimBLE host | 0 | (lib) | (lib) | BLE events | GATT, CommandGate unlock, config TLV apply |
| Arduino `loopTask` | 1 | 1 | — | never (parked) | — |

Only the App task is registered with the task watchdog. A stalled IMU task starves the
App task's queue only down to its 20 ms timeout, so the App keeps feeding; a stalled App
task trips the WDT (10 s → panic → reboot).

## Data flow

```
                    core 1                                           core 0
 ┌──────────┐ ImuSample queue (depth 8,  ┌───────────────────────┐   ┌─────────────┐
 │ imu task │──drop-oldest)─────────────>│       app task        │──>│ BLE notify  │ IMU  (char 1)
 │ prio 3   │<─requestZero/Mounting──────│ prio 2                │──>│             │ CO2  (char 3)
 │          │──takeCapturedMounting─────>│  throttle → ImuPacket │──>│             │ Diag (char 5)
 └──────────┘   (mutex handoff)          │  CO2 on change / 1 Hz │   └─────────────┘
                                         │  diag 1 Hz            │          │
 ┌──────────┐ onPeak: atomics only       │  LED update 20 Hz     │  BleCommand queue
 │audio task│──lastPeakMs_, peakPending_>│  pot poll 10 Hz       │<─(depth 8)──┤
 │ prio 2   │<─threshold provider────────│  → threshold_ atomic  │             │
 └──────────┘  reads App::threshold_     │  command dispatch     │  ConfigStore onChange
                                         │  config re-apply      │<─configDirty_ flag──┤
 ┌──────────┐ realPpm/frameCount atomics │  mount persist (NVS)  │   NimBLE host task
 │ co2 task │───────────────────────────>│  WDT feed             │   (0x20 unlock inline,
 │ prio 1   │                            └───────────────────────┘    config TLV apply)
 └──────────┘
```

### Cross-task rules

- **Audio → App**: the peak callback runs on the audio task and only stores
  `lastPeakMs_` + sets `peakPending_`. The App task consumes the flag, calls
  `Co2Service::triggerVirtualPulse` (App-task-only) and drives the sound LED.
- **App → Audio**: `PotService` is single-task (App). After each `poll()` the App stores
  `threshold()` in an `std::atomic<float>`; the audio task's threshold provider reads it.
- **BLE → App (commands)**: every command except `0x20` (unlock, inline on the host task)
  goes through the BleCommand queue. Gated opcodes are authorized with
  `BleCommand::unlockedAtEnqueue`, never by re-querying the gate.
- **BLE → App (config)**: `ConfigStore::applyTlv` runs on the host task and fires onChange
  there; the handler only sets `configDirty_`. The App task then re-applies the config:
  `CommandGate::setPin`, `applyConfig` on IMU/audio/CO2/pot, `NotifyThrottle::setRateHz`
  (only if `imuNotifyHz` actually changed, since it restarts the schedule), cached
  `co2AlertPpm`. Config writes are not persisted until command `0x11`.
- **IMU → App (mounting)**: `0x14` sets a request flag; the IMU task captures the mounting
  quaternion and hands it over; the App task calls `ConfigStore::setMountQuat` + `save()`.

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
  drain command queue (non-blocking)
  persist captured mounting (if any)
  WDT feed
}
```

No `delay()` and no heap allocation in steady state; all periods are wrap-safe
`millis()` differences.
