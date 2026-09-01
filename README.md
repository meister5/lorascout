# lorascout

[![CI](https://github.com/meister5/lorascout/actions/workflows/ci.yml/badge.svg)](https://github.com/meister5/lorascout/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**A LoRa coverage mapper for the M5Stack Cardputer ADV with the Cap LoRa-1262.**

Walk or drive a route and lorascout records what the SX1262 hears at every GPS
fix along it. You end up with a microSD card holding a CSV, plus GeoJSON, KML
and GPX files you can open directly in geojson.io, Google Earth or QGIS.

The cap carries two radios that are normally used side by side, GPS for position
and LoRa for the mesh. lorascout pairs them instead: the GNSS receiver is there
to record where the LoRa radio was standing when it took each measurement.

> **Cardputer ADV only.** The Cap LoRa-1262 talks over the rear Cap-Bus header,
> which is present on the Cardputer ADV and CardputerZero and **absent on the
> original Cardputer v1.1**. The v1.1 cannot run this firmware, because it
> cannot physically carry the cap.

---

## What it measures

Three modes, answering three different questions.

| Mode | Question | Needs |
|---|---|---|
| **Sweep** | Where is the band quiet, and where is it noisy? | one device |
| **Listen** | Where can I hear this network from? | one device, an existing network |
| **Beacon / Rover** | What is the real range of *this* link? | two devices |

**Sweep** steps the receiver across the band, samples the noise floor at each
channel and logs the result against your position. It transmits nothing. This is
the mode that tells you the industrial estate down the road is sitting on
−92 dBm of noise, which explains why nothing gets through there.

**Listen** parks on a preset (Meshtastic's regional presets, LoRaWAN, or your
own parameters) and logs every frame it decodes, with RSSI, SNR and position. It
also transmits nothing, so you can map a mesh's footprint without joining it or
putting a single frame into it. By default it records a hash of each payload
rather than the payload itself; see [Privacy](#privacy).

**Beacon / Rover** is the controlled experiment. One unit sits still and
transmits a small 19-byte frame on lorascout's own sync word while the other
walks away from it. The rover logs RSSI, SNR, distance, bearing and the excess
path loss over free space at every point, and counts what it missed. This is the
only mode that transmits, and the one bound most tightly by the
[compliance rules](docs/COMPLIANCE.md).

## What comes off the card

Each session gets a directory: `/lorascout/<session-id>/`.

```
session.json     what was configured, and the transmit audit trail
sweep.csv        one row per channel sample      \
packets.csv      one row per received frame       |  canonical, appended live
link.csv         one row per beacon frame          |  and flushed every 5 s
track.csv        one row per GPS fix              /
map.geojson      derived at session close, colour-coded by signal band
map.kml          derived, opens in Google Earth
track.gpx        derived, the route itself
```

The CSVs are the primary record. They are appended as samples arrive and flushed
on a timer, so a flat battery mid-survey costs you the last few seconds rather
than the whole session. The map files are generated once, at session close, from
those CSVs, and can be regenerated later. That is what the "unexported session
found" prompt on the menu offers to do.

Points are coloured by a single shared scale, so the colours on the device and
the colours in QGIS mean the same thing:

| Band | RSSI | Colour |
|---|---|---|
| Excellent | ≥ −80 dBm | green |
| Good | −80 … −95 | light green |
| Fair | −95 … −105 | yellow |
| Weak | −105 … −115 | orange |
| Marginal | < −115 dBm | red |
| None | frame missed | grey |

## Hardware

- **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash, no PSRAM, microSD)
- **Cap LoRa-1262** (SKU U214): Semtech SX1262 + ATGM336H-6N GNSS
- a microSD card, and the two stock antennas

Wiring, the pin map, and the antenna-switch gotcha that silently breaks
transmit are in [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Build and flash

```bash
git clone https://github.com/meister5/lorascout
cd lorascout

# flash straight over USB
pio run -e cardputer-adv -t upload

# or build the release image
tools/package.sh
```

`tools/package.sh` writes `dist/lorascout-app.bin`, the M5Launcher image, which
you copy to SD or upload via WebUI/OTA. It is the only image released, and the
only one the repo root carries.

### M5Launcher

Launcher installs application binaries into an OTA app partition, so use the
**app** binary. A prebuilt copy sits at the repo root, refreshed at each
release, so you do not have to build anything:

<https://raw.githubusercontent.com/meister5/lorascout/main/lorascout-app.bin>

That URL is directly downloadable, which is what an `OTA > Favorites` entry
needs. To install from SD instead:

1. Copy `lorascout-app.bin` to a FAT32 SD card.
2. In Launcher, open `SD`, select the file, choose `Install`.

It also works through `WUI` (browser upload) or as an `OTA > Favorites` entry
pointing at a release asset URL. The build is around 650 kB, well inside a
standard OTA slot, and `tools/package.sh` fails the build if it ever outgrows
one.

lorascout keeps no filesystem partition of its own. Settings live in NVS and
survey data is written to the microSD card, so there is nothing for Launcher to
install alongside the app image and nothing to lose when you switch firmware. It
does not touch the OTA boot partition either, so it cannot brick a Launcher
install; return to Launcher the way Launcher documents for your device.

### Flashing over USB

`pio run -e cardputer-adv -t upload` writes the bootloader, partition table and
app in one go. There is no merged single-file image to hand to M5Burner or to
`esptool` at offset `0x0`, since M5Launcher is the intended install route and
anyone flashing over USB already has the toolchain set up.

Then run the tests, which need nothing but a C++17 compiler:

```bash
make test          # or: pio test -e native
```

The geodesy, the airtime and duty-cycle maths, the region table, the NMEA
parser, the beacon codec and the exporters all live in `lib/core/` and include
no Arduino headers, which is why CI can run them on a plain Ubuntu runner in
seconds and why a bad bearing calculation shows up on a laptop rather than
halfway up a hill.

## First boot

The firmware asks for your **region** before it will do anything else. There is
no default, deliberately: a wrong default is a transmission on the wrong
frequency at the wrong power. If you have fitted a non-stock antenna, set its
gain too. Power limits are stored as EIRP, so a higher-gain antenna makes
lorascout *reduce* transmit power to compensate.

Keys: `;` up, `.` down, `,` left/back, `/` right/select, `` ` `` or `del` to back out,
`Enter` to start or stop a session.

## Duty cycle is the real constraint

Battery and storage are not what limits a survey. In EU868 at SF12 a single
frame occupies the channel for over a second, and a 1% duty cycle means you may
send roughly one frame every two minutes, or about 30 samples an hour, which is
a slow walk between points. At SF7 the same budget buys a sample every five
seconds.

lorascout keeps this visible rather than letting you hit it as an error. The
budget is on screen the whole time as a bar, the countdown to the next legal
transmission sits next to it, and the transmit path refuses rather than warns.
If you want dense samples, choose a fast preset and accept the shorter range.

## Privacy

Listen mode records other people's traffic. What it keeps by default is length,
CRC status and a 32-bit hash of the payload: enough to dedupe a repeated frame
or recognise a persistent sender, not enough to reconstruct content. Raw payload
retention exists, is off by default, and has to be turned on deliberately.
Either way, `session.json` records which it was.

## Layout

```
lib/core/    hardware-free logic: geodesy, airtime, regions, duty cycle,
             NMEA, beacon codec, exporters, session model. Host-tested.
src/hal/     the parts that touch hardware: radio, GNSS, SD, keys, cap
src/app/     modes, UI, settings, the dual-core producer/consumer loop
test/        one directory per suite, ~3100 assertions
docs/        hardware notes, compliance rules, the idea backlog
```

The sampler task is pinned to core 0 and owns the radio, the GNSS and the
duty-cycle accounting. The writer and the UI run on core 1 and own the SD card
and the screen. The two sides communicate through a single queue.

## Legal

See [`docs/COMPLIANCE.md`](docs/COMPLIANCE.md) for what the firmware refuses to
do and why. In brief: it will not transmit outside 868–923 MHz, will not
transmit on another network's sync word, floors rather than rounds its power
calculation, and stops when the duty budget is spent. None of that replaces
knowing your own regulator. Responsibility for a transmission is the operator's.

## Licence

MIT. See [`LICENSE`](LICENSE).
