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
| U1 | IMU breakout (10-pin header, ICM-20948) | SCL | `/GPIO22` | **22** |
| U1 | | SDA | `/GPIO21` | **21** |
| U1 | | EDA, ECL, AD0, INT, **NCS**, FSYNC | unconnected | — |
| U2 | CO2 sensor — value `Sensor_CO2_SC8`, footprint `Senseair_S8_Down` (**Senseair S8**) | +5V / GND | `+5V` / `GND` | — |
| U2 | | UART_RXD (pad 6) | `/GPIO17` | **17** (ESP TX → S8 RX) |
| U2 | | UART_TXD (pad 7) | `/GPIO16` | **16** (S8 TX → ESP RX) |
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
   CO2 LED = GPIO4 (D1), and the BLE LED command moved to a **new status LED on
   GPIO23** (board change: GPIO23 → 220 Ω → LED → GND; GPIO23 is free and has no
   strapping role). The firmware enforces that the three LED pins are pairwise
   distinct.
2. **The IMU is wired for I2C only.** U1's NCS pin is unconnected and SCL/SDA go to
   GPIO22/21 — the board matches the legacy firmware's active I2C path (AD0 low) and
   rules out ever enabling the vestigial SPI path. The v2 firmware uses I2C on 21/22
   at 400 kHz and carries no SPI code.
3. **The CO2 sensor is a Senseair S8** (the schematic's "SC8"). The S8 is a Modbus
   RTU slave: it **never transmits unsolicited**, so the legacy firmware's passive
   frame listener could never receive anything from it — real CO2 never updated,
   independently of the checksum bug. The v2 firmware polls it every 2 s
   (`co2Protocol = 0`, TLV 0x27); see `docs/protocol.md` for the frames.
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
