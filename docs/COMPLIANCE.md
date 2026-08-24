# Compliance

Everything here is enforced in code, in `lib/core/region.cpp` and
`lib/core/dutycycle.cpp`, and covered by the host tests. It is not advice, and
it is not a substitute for reading your own regulator: national variants exist,
and the responsibility for a transmission is the operator's.

## What the firmware refuses to do

- **Transmit outside 868–923 MHz.** The Cap LoRa-1262 is specified, matched and
  filtered for that range and the supplied antenna is cut for it. No region
  configuration can authorise a channel outside it; `checkChannel()` rejects the
  frequency and says why.
- **Transmit on another network's parameters.** Beacon mode is gated on
  `RadioPreset::transmitAllowed`, which is set only for the two lorascout
  presets. Those use sync word **0x5C**, deliberately distinct from LoRaWAN
  public (0x34), LoRaWAN private (0x12) and Meshtastic (0x2B). Every preset the
  firmware can listen to is receive-only, so a survey can map someone's mesh
  without ever injecting a frame into it.
- **Exceed a power limit.** Limits are stored as EIRP. The conducted power
  handed to the PA is `floor(maxEIRP − antennaGain)`, clamped to the SX1262's
  −9…+22 dBm. It floors rather than rounds, because rounding up authorises up to
  half a dB over the ceiling. Fitting a higher-gain antenna therefore *reduces*
  transmit power; set the gain in Settings.
- **Exceed a duty cycle.** A sliding one-hour budget is checked before every
  frame and only credited after one actually goes out. When the budget is spent
  the beacon stops and the screen shows the hold time.
- **Exceed a dwell limit.** Where a region caps channel occupancy, a frame
  longer than the cap is refused outright at session start rather than at the
  first transmission — no amount of waiting makes it legal.
- **Key the PA into an open port.** On the -1262 the antenna path sits behind a
  PI4IOE5V6408 expander. If it cannot be enabled, the firmware stops with a
  full-screen refusal instead of transmitting.

## Region table

| Region | Band | Module reach | EIRP | Duty | Dwell | LBT | Hop |
|---|---|---|---|---|---|---|---|
| EU868 | 863–870 | 868–870 only | 16.15 dBm (14 ERP), 29.15 in 869.4–869.65 | per sub-band: 1% / 0.1% / 10% | — | no | optional |
| UK868 | 863–870 | 868–870 only | as EU868 | as EU868 | — | no | optional |
| US915 | 902–928 | 902–923 only | 30 dBm | none | 400 ms | no | **required** |
| AU915 | 915–928 | 915–923 only | 30 dBm | none | — | no | optional |
| AS923 | 920–925 | 920–923 only | 16 dBm | 1% | 400 ms | **yes** | optional |
| KR920 | 920–923.3 | full | 14 dBm | none | 400 ms | **yes** | optional |
| IN865 | 865–867 | **none** | — | — | — | — | — |
| RU864 | 864–870 | 868–870 only | 16 dBm | — | — | — | — |
| EU433 | 433 | **none** | — | — | — | — | — |

IN865 and EU433 are listed so the UI can explain why they are unavailable
instead of silently omitting them.

## Two honest caveats

**The US case is not clean.** FCC 15.247 covers frequency-hopping systems
(≤400 ms per channel, ≥50 channels) and wideband digital modulation (≥500 kHz),
and a stationary single-channel 125 kHz LoRa transmitter sits neatly in neither.
The firmware therefore treats hopping as mandatory in US915, hops across a
54-channel plan, and caps occupancy at 400 ms. That is a defensible reading, not
a certified one.

**Hopping needs GNSS time on both ends.** Beacon and rover derive the active
channel from `unixSeconds / 4 % channelCount` — GNSS time being the only clock
two nodes share without ever having met. A rover without a time fix parks on
channel 0 and hears roughly one hop in N; the UI says so rather than letting you
mistake a scheduling artefact for poor coverage.

## Privacy

Listen mode records other people's traffic. By default the logs keep length,
CRC status and a 32-bit FNV-1a hash of the payload — enough to dedupe a frame or
recognise a repeating sender, not enough to reconstruct content. Meshtastic
payloads are encrypted anyway, though its packet headers are not.

Raw payload retention is off by default and has to be turned on deliberately in
Settings, where it is labelled as logging others' data. Whichever way it is set,
`session.json` records the choice, so a survey's provenance is checkable.

## Audit trail

Every session writes `session.json` containing the region, the applied EIRP
against the ceiling, duty fraction, dwell cap, transmission count and total
airtime — written *before* the maps, so a session that dies mid-export still
leaves its transmit log intact.
