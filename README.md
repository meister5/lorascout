# lorascout

[![CI](https://github.com/meister5/lorascout/actions/workflows/ci.yml/badge.svg)](https://github.com/meister5/lorascout/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

LoRa coverage mapper for the M5Stack Cardputer ADV with the Cap LoRa-1262: it logs what the SX1262 hears at every GPS fix.

## Features

- Sweep mode: steps across the band and logs the noise floor per channel. Transmits nothing.
- Listen mode: logs every frame decoded on a preset (Meshtastic, LoRaWAN or custom) with RSSI, SNR and position. Transmits nothing.
- Beacon and Rover modes: one unit transmits a 19-byte frame, the other logs RSSI, SNR, distance, bearing and missed frames. Only these transmit.
- Duty-cycle budget and next-legal-transmit countdown on screen; the transmit path refuses when the budget is spent.
- Output on microSD: CSV logs plus GeoJSON, KML and GPX, colour-coded by signal band.
- Listen mode stores a payload hash by default, not the payload.

## Hardware

- M5Stack Cardputer ADV (ESP32-S3FN8, 8 MB flash, no PSRAM) and a microSD card
- Cap LoRa-1262 (SX1262 + ATGM336H-6N GNSS) with its antennas

Cardputer ADV only. The cap uses the rear Cap-Bus header, which the original Cardputer v1.1 does not have. Pin map and notes: [docs/HARDWARE.md](docs/HARDWARE.md).

## Install

Prebuilt image: [lorascout-app.bin](https://raw.githubusercontent.com/meister5/lorascout/main/lorascout-app.bin) (M5Launcher app binary).

1. Copy `lorascout-app.bin` to a FAT32 SD card.
2. In M5Launcher open `SD`, select the file, choose `Install`.

The raw URL also works as an `OTA > Favorites` entry, and the file can be uploaded through the launcher's WebUI.

## Usage

On first boot you must pick a region. There is no default, because a wrong default is a transmission on the wrong frequency at the wrong power. Set your antenna gain in Settings; power limits are stored as EIRP, so a higher-gain antenna lowers transmit power.

| Key | Action |
|---|---|
| `;` `.` | Up, down |
| `,` `/` | Left, right (change a value) |
| `Enter` | Select, start or stop a session |
| `` ` `` or `del` | Back |

Main menu: Sweep, Listen, Rover, Beacon, Settings.

Each session writes to `/lorascout/<session-id>/` on the card:

| File | Content |
|---|---|
| `session.json` | Configuration and transmit audit trail |
| `sweep.csv`, `packets.csv`, `link.csv`, `track.csv` | Live logs: channel samples, received frames, beacon frames, GPS fixes |
| `points.geojson`, `points.kml` | Map files, written at session close |
| `track.gpx` | The route |

The menu offers to regenerate the map files for sessions that were not exported.

Legal and RF: the firmware will not transmit outside 868-923 MHz, will not transmit on another network's sync word (the beacon uses 0x5C), floors its power calculation and stops when the duty budget is spent. Listen mode records other people's traffic. None of this replaces knowing your regulator; the operator is responsible for every transmission. Details: [docs/COMPLIANCE.md](docs/COMPLIANCE.md).

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e cardputer-adv -t upload   # build and flash over USB
tools/package.sh                     # release image in dist/lorascout-app.bin
make test                            # host tests for lib/core (C++17 compiler only)
```

## License

MIT. See [LICENSE](LICENSE).
