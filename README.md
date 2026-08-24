# lorascout

**A LoRa coverage mapper for the M5Stack Cardputer ADV with the Cap LoRa-1262.**

Walk or drive a route, and lorascout records what the SX1262 actually hears at
every GPS fix along it. It leaves a microSD card holding a CSV you can trust and
a GeoJSON, KML and GPX you can drop straight into geojson.io, Google Earth or
QGIS.

The two radios on this cap are usually used side by side — GPS for position,
LoRa for the mesh. lorascout points them at each other: the GNSS receiver exists
to tell you *where* the LoRa radio was standing when it measured what it
measured.

> **Cardputer ADV only.** The Cap LoRa-1262 talks over the rear Cap-Bus header,
> which is present on the Cardputer ADV and CardputerZero and **absent on the
> original Cardputer v1.1**. The v1.1 cannot run this firmware, because it
> cannot physically carry the cap.

---

## What it measures

Three modes, and they answer three different questions.

| Mode | Question | Needs |
|---|---|---|
| **Sweep** | Where is the band quiet, and where is it noisy? | one device |
| **Listen** | Where can I hear this network from? | one device, an existing network |
| **Beacon / Rover** | What is the real range of *this* link? | two devices |

**Sweep** steps the receiver across the band, samples the noise floor at each
channel and logs the result against your position. It transmits nothing. This is
the mode that tells you the industrial estate down the road is sitting on
−92 dBm of noise, which is why nothing gets through there.

**Listen** parks on a preset — Meshtastic's regional presets, LoRaWAN, or your
own parameters — and logs every frame it decodes, with RSSI, SNR and position.
It also transmits nothing, so you can map a mesh's footprint without ever
joining it or putting a single frame into it. By default it records a hash of
each payload rather than the payload itself; see [Privacy](#privacy).

**Beacon / Rover** is the controlled experiment. One unit sits still and
transmits a small 19-byte frame on lorascout's own sync word; the other walks
away from it. The rover logs RSSI, SNR, distance, bearing and the excess path
loss over free space at every point, and counts what it missed. This is the only
mode that transmits, and it is the one wrapped most tightly in
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
map.kml          derived — opens in Google Earth
track.gpx        derived — the route itself
```

The CSVs are the record. They are appended as samples arrive and flushed on a
timer, so a flat battery in the middle of a survey costs you the last few
seconds, not the whole afternoon. The map files are generated once, at session
close, from those CSVs — and can be regenerated later, which is what the
"unexported session found" prompt on the menu is offering to do.

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

- **M5Stack Cardputer ADV** (ESP32-S3, 8 MB flash, PSRAM, microSD)
- **Cap LoRa-1262** (SKU U214): Semtech SX1262 + ATGM336H-6N GNSS
- a microSD card, and the two stock antennas

Wiring, the pin map, and the antenna-switch gotcha that silently breaks
transmit are in [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Build and flash

```bash
git clone https://github.com/<you>/lorascout
cd lorascout
pio run -e cardputer-adv -t upload
```

Then run the tests, which need nothing but a C++17 compiler:

```bash
make test          # or: pio test -e native
```

Everything worth testing — the geodesy, the airtime and duty-cycle maths, the
region table, the NMEA parser, the beacon codec, the exporters — lives in
`lib/core/` and includes no Arduino headers at all. That is why CI can run it on
a plain Ubuntu runner in seconds, and why a bad bearing calculation gets caught
on a laptop rather than halfway up a hill.

## First boot

The firmware asks for your **region** before it will do anything else. There is
no default, deliberately: a wrong default is a transmission on the wrong
frequency at the wrong power. If you have fitted a non-stock antenna, set its
gain too — power limits are stored as EIRP, so a higher-gain antenna makes
lorascout *reduce* transmit power to compensate.

Keys: `;` up, `.` down, `,` left/back, `/` right/select, `` ` `` or `del` to back out,
`Enter` to start or stop a session.

## Duty cycle is the real constraint

Not battery, not storage. In EU868 at SF12 a single frame occupies the channel
for over a second, and a 1% duty cycle means you may send roughly one frame
every two minutes — perhaps 30 samples an hour, which is a slow walk between
points. At SF7 the same budget buys a sample every five seconds.

lorascout treats this as a first-class fact rather than an error to hit: the
budget is on screen the whole time as a bar, the countdown to the next legal
transmission is shown next to it, and the transmit path refuses rather than
warns. If you want dense samples, choose a fast preset and accept the shorter
range. That trade-off is the survey.

## Privacy

Listen mode records other people's traffic. What it keeps by default is length,
CRC status and a 32-bit hash of the payload — enough to dedupe a repeated frame
or recognise a persistent sender, not enough to reconstruct content. Raw payload
retention exists, is off by default, and has to be turned on deliberately.
Either way, `session.json` records which it was.

## Layout

```
lib/core/    hardware-free logic — geodesy, airtime, regions, duty cycle,
             NMEA, beacon codec, exporters, session model. Host-tested.
src/hal/     the parts that touch hardware — radio, GNSS, SD, keys, cap
src/app/     modes, UI, settings, the dual-core producer/consumer loop
test/        one directory per suite, ~3100 assertions
docs/        hardware notes, compliance rules, the idea backlog
```

The sampler task is pinned to core 0 and owns the radio, the GNSS and the
duty-cycle accounting. The writer and the UI run on core 1 and own the SD card
and the screen. They meet at one queue. Nothing else crosses.

## Legal

See [`docs/COMPLIANCE.md`](docs/COMPLIANCE.md) for what the firmware refuses to
do and why. Short version: it will not transmit outside 868–923 MHz, will not
transmit on another network's sync word, floors rather than rounds its power
calculation, and stops when the duty budget is spent. None of that is a
substitute for knowing your own regulator — the responsibility for a
transmission is the operator's.

## Licence

MIT. See [`LICENSE`](LICENSE).
