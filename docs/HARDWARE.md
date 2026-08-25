# Hardware notes — Cardputer ADV + Cap LoRa-1262

Source: [M5Stack Cap LoRa-1262 docs](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
and the [Cap LoRa868 / LoRa-1262 Arduino tutorial](https://docs.m5stack.com/en/arduino/projects/cap/cap_lora868).
SKU U214. The cap plugs into the ADV's rear 2×7 Cap-Bus header.

## Radio — Semtech SX1262 (SPI)

| Item | Value |
|---|---|
| Frequency range | 868 – 923 MHz |
| Modulations | LoRa, FSK, GFSK, MSK, GMSK, OOK |
| Max TX power | +22 dBm |
| RX sensitivity | −147 dBm (LoRa, low data rate) |
| Max bitrate | 300 kbps |
| Antenna | external RP-SMA, 108 × 9.3 mm, 3 dBi (shielded module) |
| TX current | ~163 mA @ 5 V |

## GNSS — ATGM336H-6N (AT6668 core, UART)

| Item | Value |
|---|---|
| Constellations | GPS, QZSS, BD2, BD3, Galileo, GLONASS |
| Bands | BDS B1I+B1C · GPS/QZSS/SBAS L1 · GAL E1 · GLO R1 |
| Channels | 50 |
| Accuracy | < 1.5 m CEP50 |
| Update rate | up to **10 Hz** |
| Protocol | NMEA 0183 4.1 (also CASIC binary) |
| Default UART | **115200** 8N1 |
| Sensitivity | tracking −162 dBm · acquisition −160 dBm · cold start −148 dBm |
| TTFF | cold 23 s · hot 1 s |
| Antenna | built-in ceramic patch |

Idle draw of the whole cap is ~33 mA; add the SX1262 TX burst on top.

## Pin map (Cardputer ADV)

LoRa (SPI):

| Signal | NSS | MOSI | MISO | SCK | IRQ (DIO1) | RST | BUSY |
|---|---|---|---|---|---|---|---|
| GPIO | **G5** | G14 | G39 | G40 | **G4** | **G3** | **G6** |

GNSS (UART): Cardputer **G13 → GPS-RX**, **G15 ← GPS-TX**.

I²C (shared with the HY2.0-4P Grove port): **G8 = SDA**, **G9 = SCL**.

Cap-Bus header order (left 1–7, right 8–14): `GPS_TX, GPS_RX, SCL, SDA, 5V_OUT,
GND, 5V_IN` | `LoRa_RST, LoRa_IRQ, LoRa_BUSY, LoRa_SCK, LoRa_MOSI, LoRa_MISO,
LoRa_NSS`.

## The gotcha: the antenna switch

The **-1262** cap (unlike the older Cap LoRa868) puts an **FM8625H antenna
switch** behind a **PI4IOE5V6408 I²C IO expander at 0x43**. `P0` drives
`SX_ANT_SW`. If you never touch it, the radio initialises fine and transmits
into nothing.

```cpp
#include "utility/PI4IOE5V6408_Class.hpp"
m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);

m5::In_I2C.begin();
if (ioe.begin()) {              // present == Cap LoRa-1262, absent == Cap LoRa868
    ioe.setDirection(0, true);        // P0 as output
    ioe.setHighImpedance(0, false);   // let it actually drive
    ioe.digitalWrite(0, true);        // antenna path on
}
```

That `ioe.begin()` probe is also the clean way to auto-detect which cap is
attached.

## The other gotcha: the SPI bus is the microSD bus

On the ADV the Cap-Bus SPI pins **are** the microSD pins — SCK 40, MOSI 14,
MISO 39 — with separate chip selects (LoRa NSS 5, card CS 12). Bring the host
up once, on those pins, before either driver runs; `SPI.begin()` is a no-op
afterwards, so whichever peripheral initialises first is the one that decides
the pin map.

## And a third: do not call `setTCXO(0)` after `begin()`

RadioLib's `begin()` already negotiates the reference. It configures DIO3 for a
TCXO and, if the chip answers with `XOSC_START_ERR` — an XTAL module told it has
one — drops to 0 V and reconfigures itself. `setTCXO(0)` is not a hint to that
process: it is `reset(true)`, a hard chip reset that discards frequency,
bandwidth, spreading factor and sync word and leaves the radio on RadioLib's
434 MHz defaults.

## Libraries

- **M5Unified** + **M5GFX** — board init, display, keyboard.
- **RadioLib** — SX1262 driver. Construct it with the bus named:
  `SX1262 radio = new Module(GPIO_NUM_5, GPIO_NUM_4, GPIO_NUM_3, GPIO_NUM_6, SPI);`
  (NSS, IRQ, RST, BUSY, bus) and call `SPI.begin(40, 39, 14, -1)` yourself first.
  RadioLib does **not** learn the SPI pins from M5Unified. The four-argument
  constructor makes it call `SPI.begin()` with no arguments, which lands on the
  ESP32-S3 variant defaults — SCK 12, MISO 13, MOSI 11 — so the SX1262 never
  answers and `begin()` returns `RADIOLIB_ERR_CHIP_NOT_FOUND` (-2). Passing the
  bus object also stops RadioLib calling `SPI.end()` on a failed probe, which
  would tear down the bus the microSD slot shares.
- **TinyGPSPlus** — ⚠️ use the **M5Stack fork from GitHub**, not the copy in the
  Arduino Library Manager.

Working starting points to crib from:
[cyrus07424/CardputerAdv-playground](https://github.com/cyrus07424/CardputerAdv-playground)
(`GpsInfoDemo`, `LoraChatDemo`, `DataHarvester`) and
[fotografm/cardputer-gps](https://github.com/fotografm/cardputer-gps).

## Regulatory

868 MHz is the EU ISM band; 915 MHz is US ISM. Duty-cycle limits apply in EU
(typically 1% on 868.0–868.6). APRS-style transmissions are amateur-band
traffic in most jurisdictions and need a licence. Pick the band at build time
and make it loud in the UI which one is compiled in.
