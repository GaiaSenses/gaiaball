# Hardware pinout — verified against the KiCAD board design

Source: the Gaiaball PCB project `Globo_GaiaSenses_PCi` (KiCAD), files
`Globo_GaiaSenses_PCi.kicad_sch`, `Globo_GaiaSenses_PCi.kicad_pcb` and
`Globo_GaiaSenses_PCi_2.kicad_pcb` (both PCB revisions carry the **same netlist**;
they differ only in layout). The tables below were extracted from the PCB pad→net
assignments on 2026-11-06.

## Netlist (component → ESP32 GPIO)

| Ref | Component | Pin / function | Net | ESP32 GPIO |
|---|---|---|---|---|
| U4 | ESP32-DEVKITC-32E | — | — | — |
| U1 | IMU: **GY-91** (MPU-9250 + AK8963 + BMP280) or **GY-ICM20948V2** (ICM-20948 + AK09916) on the 10-pin header (KiCAD symbol `Sensor_Motion:MPU-9250-IMU`; see finding 2) | SCL | `/GPIO22` | **22** |
| U1 | | SDA | `/GPIO21` | **21** |
| U1 | | EDA, ECL, AD0, INT, **NCS**, FSYNC | unconnected | — |
| U2 | CO2 sensor — **SC8-CO2-5K** (Shenzhen Shenchen Technology, "YYS SC8"; KiCAD value `Sensor_CO2_SC8`, drawn on an S8-style footprint `Senseair_S8_Down`) | +5V / GND | `+5V` / `GND` | — |
| U2 | | UART_RXD (pad 6) | `/GPIO17` | **17** (wired, but UNUSED: the firmware never transmits — see finding 3) |
| U2 | | UART_TXD (pad 7) | `/GPIO16` | **16** (SC8 TX → ESP RX) |
| U2 | | PWM, NC pins | unconnected | — |
| M1 | INMP441 microphone (L/R = 3V3 → right) | SD / WS / SCK | `/GPIO33` `/GPIO25` `/GPIO26` | **33 / 25 / 26** |
| M2 | INMP441 microphone (L/R = GND → left) | same I2S bus as M1 | same nets | same |
| D1 | LED "co2" (via R1 220 Ω) | anode net | `/GPIO04` | **4** |
| D2 | LED "mic" (via R2 220 Ω) | anode net | `/GPIO02` | **2** |
| RV1 | Potentiometer 50 kΩ (wiper via R3 220 Ω) | wiper | `/GPIO34` | **34** (ADC1_CH6, input-only) |

Free GPIOs broken out on the headers and unconnected on the board: 0, 5, 12, 13, 14,
15, 18, 19, **23**, 27, 32, 35 (plus TXD0/RXD0 and the SD_* flash pins, which must
not be used).

## Findings that drove firmware decisions

1. **GPIO2 carried three firmware functions, one physical function.** The legacy
   firmware used GPIO2 as the BLE LED command output, the sound LED, *and* a dead
   SPI chip-select (`CS_PIN 2`, left over from the SparkFun example — `USE_SPI` is
   commented out, so it never ran, but it cemented the pin confusion). On the board,
   GPIO2 is only D2 ("LED mic"). Fix: one pin per function — sound LED = GPIO2 (D2),
   CO2 LED = GPIO4 (D1). The BLE LED command itself turned out to be dead weight: no
   client ever sends it (the Gaiasenses site declares the characteristic but never
   writes it), so it was removed rather than given a pin. The firmware enforces that
   the two LED pins are distinct.
2. **The IMU comes in two variants, and the firmware drives both.** The earlier text
   here called the IMU a GY-91 (MPU-9250 + AK8963 + BMP280) "confirmed by the user"; that
   was **wrong for the ball in hand**. The KiCAD symbol (`MPU-9250-IMU`, datasheet
   property MPU-9250A) describes the GY-91 footprint the board was designed around, and
   a GY-91 is what future production units are expected to carry, but the existing
   ball carries a **`GY-ICM20948V2`** module (ICM-20948 + AK09916 magnetometer).
   Evidence, 2026-11-12: reading `WHO_AM_I` the MPU-9250 way (register `0x75`, expected
   `0x71`) returned a stable `0x48`. Register `0x75` is *reserved* on the ICM-20948, so
   the `0x48` was not an answer: it is a meaningless constant that merely resembles one
   (it is also the AK09916/AK8963 magnetometer's WIA1 value, which made it look
   plausible). On the ICM-20948 `WHO_AM_I` lives at register `0x00` (bank 0) and reads
   `0xEA`. That is consistent with the legacy firmware, which used the SparkFun
   ICM-20948 library and visibly moved on this ball.

   | Variant | Chips | WHO_AM_I | Where |
   |---|---|---|---|
   | `GY-ICM20948V2` | ICM-20948 + AK09916 | `0xEA` at `0x00` (bank 0) | the existing ball |
   | `GY-91` | MPU-9250 + AK8963 (+ BMP280 at 0x76, unread) | `0x71` at `0x75` | future production |

   One firmware drives both. The two are **not** told apart by auto-detection: the
   operator selects the chip in the config-app (TLV `0x34`, `imuChip`), and a
   factory-fresh ball leaves the IMU **off** (`0` = unconfigured) until someone does.
   Changing the selection takes effect after a reboot. Wiring is the same I2C-only
   arrangement for both (NCS unconnected, SCL/SDA on 22/21, AD0/SDO low -> 0x68); the
   magnetometer sits at 0x0C on both (pass-through / bypass).
3. **The CO2 sensor is an SC8-CO2-5K** (Shenzhen Shenchen Technology, often listed
   as "YYS SC8") — user-confirmed hardware. The schematic's `Sensor_CO2_SC8` value is
   literal; the `Senseair_S8_Down` footprint name misled an earlier revision of this
   document into identifying the part as a Senseair S8 (the SC8 shares the S8's
   land pattern). The SC8 **actively streams** one 16-byte `42 4D` frame per second
   (9600 8N1), and its datasheet (SC8-CO2-5K V1.4) states that receiving ANY query
   stops the active output until the next power cycle — so the v2 firmware opens
   UART1 **listen-only** (no TX pin, even though GPIO17 is routed on the board) and
   parses the stream with a resyncing byte parser; see `docs/protocol.md`. The
   legacy firmware's frame layout and checksum (sum of bytes 0..14 == byte 15)
   match this datasheet; its real CO2 defects were the fixed-window read with no
   resynchronization (one lost byte desynced it until reboot) and the absence of
   staleness tracking.
4. **GPIO34 is input-only** (fine for the potentiometer ADC; never assign it as an
   LED pin — the firmware's GPIO whitelist already excludes it).
5. Both mics share one I2S bus in stereo (M1 right, M2 left), matching the firmware's
   16 kHz 32-bit stereo capture.

## Strapping-pin note

GPIO2 must be LOW/floating at reset for the UART download mode. D2's LED+resistor to
GND is compatible with flashing; the firmware additionally drives GPIO2 LOW before
`pinMode(OUTPUT)` at boot. Do not move any LED to GPIO0/GPIO12/GPIO15 without
reviewing the boot-strap consequences (the config whitelist allows them for
flexibility, but GPIO23/18/19/27 are safer choices).
