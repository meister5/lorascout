# Project ideas — Cardputer ADV + Cap LoRa-1262

Researched 2026-08-24 via Exa web search + GitHub repo/code search.

## What already exists (so we don't rebuild it)

The Cardputer ADV + Cap LoRa-1262 combination is **already crowded** in a few
specific lanes. Checked on GitHub:

| Lane | Existing work |
|---|---|
| Meshtastic / MeshCore clients | `d4rkmen/plai`, `anton-vinogradov/meshtastic-adv`, `MultiMote/meshcore-cardputer-adv`, `hdcasey/meshcore-cardputer-adv`, `sosprz/meshcore-cardputer-adv`, `Stachugit/MeshCore-Cardputer-ADV` |
| Private LoRa messenger | `baltamir1978/fantashtic` |
| APRS position beacon | `emanuelelaface/Cardputer-APRS` |
| Plain GPS display / passthrough | `4n6jones-commits/CardputerGPS`, `fotografm/cardputer-gps` |
| GPX / lap / speed logger | `fdantasb/gps-tracker` (10 Hz, race + trip modes) |
| Astronomy from GNSS position | `nongxl/SkyCompass_CardputerADV` |
| Pentest firmware bundles | `GeneralDussDuss/poseidon`, `mrnet15/RavenRF`, `TenoTrash/PoisonMesh` |

**The gap:** almost everything above uses the two radios *side by side* — GPS
tells you where you are, LoRa carries a message. Nothing much uses them
**against each other**, where position is what makes the radio data mean
something (or vice versa). That's where the interesting projects are.

---

## Ranked ideas

### 1. LoRa coverage mapper / RF field survey ⭐ recommended

Walk or drive a route while a fixed beacon transmits on a schedule. The
handheld logs every received packet as `(lat, lon, alt, RSSI, SNR, SF, freq,
packet-loss)` and renders a live "hot/cold" trail on screen. Export GeoJSON /
KML / CSV to microSD, drop it on a map, and you have an actual measured
coverage footprint.

*Why it's good:* genuinely needs both chips — a coverage map is meaningless
without position, and position is boring without RF. It's a tool people
deploying LoRa/Meshtastic actually want and currently do by hand with a phone
and a spreadsheet. Also naturally bidirectional: run two units and log the
uplink and downlink asymmetry.

*Extras:* SF/BW sweep at each point to find the cheapest setting that still
closes the link; link-budget estimate vs. measured, to spot obstructions;
"where did I lose the node" markers.

### 2. Recovery beacon + direction finder (rocketry / HAB / drone)

Two roles in one firmware. **Beacon mode:** strip the device to GNSS + periodic
LoRa position burst, longest-range SF, sleep between. **Chase mode:** the
handheld takes those bursts, computes bearing + distance from its own fix, and
draws a big arrow with a distance readout — plus last-known-position dead
reckoning and a breadcrumb of everywhere the beacon has been.

*Why it's good:* the classic high-altitude-balloon/model-rocket recovery
problem, and the Cardputer is a near-perfect ground station (screen, keyboard,
battery). +22 dBm at SF12 is a real range budget. The "arrow + metres" UI is
the whole product and nobody has built it well here.

### 3. Off-grid group tracker ("where is everyone")

Each node beacons a compressed position on a shared channel; the screen shows a
radar/compass view of every peer with bearing, distance, altitude delta, and
staleness. No messaging (Meshtastic already does that) — pure spatial
awareness for hiking parties, ski groups, search teams, event marshals.

*Design note:* pack positions tight (~10 bytes/fix with a delta encoding)
and use GNSS time to slot transmissions so nodes don't collide — see idea 6.

### 4. Geofence tripwire network

Drop cheap sensor nodes; each knows its own coordinates and a radius. When
something crosses (PIR/reed/whatever on the Grove port), it sends a short alert
over LoRa. The handheld shows which fence broke and how far away it is, with a
bearing. Also works inverted: the *handheld* holds the fences and alerts when
**you** cross one — geofenced reminders, restricted-area warnings, "you've
drifted off the route".

### 5. GNSS jamming & spoofing detector

Log per-satellite C/N₀, satellites-seen vs. satellites-used, HDOP, and sudden
position/time jumps. Flag the signatures: uniform C/N₀ collapse (jamming),
implausibly *strong* uniform C/N₀ with a teleporting fix (spoofing), clock
steps. Optionally cross-check position against a trusted LoRa peer's fix —
two independent receivers disagreeing is hard evidence.

*Why it's good:* a security/research angle nobody in this hardware ecosystem
has taken, and the cap's 50-channel multi-constellation receiver gives enough
raw data to make it real.

### 6. GNSS-disciplined TDMA experiment

Use the GNSS 1 PPS / time solution to give every node a synchronised clock,
then slot LoRa transmissions so a channel that collapses under ALOHA contention
stays clean with 20+ nodes. Measure the difference against unslotted.

*Why it's good:* the most technically interesting one. It's a real protocol
result, not just an app, and it's the enabling layer under ideas 3 and 4. Also
the highest risk — PPS routing on the Cap-Bus needs verifying.

### 7. Off-grid geocaching / treasure hunt

Hidden nodes broadcast riddles or hints; proximity is revealed by RSSI ("warmer
/ colder") *plus* the GNSS distance, and the cache only unlocks its payload
when you're within N metres. Turns the two radios into a game mechanic —
approachable, demo-friendly, good for events and teaching.

### 8. Breadcrumb & return-to-base navigator

Log a track at 1–10 Hz, then reverse it: an arrow home, distance remaining,
elapsed/estimated time, and a periodic LoRa "I'm okay, here's my position"
ping to a base-camp node so somebody else can watch the same track. Solid,
useful, low-risk — but the closest to existing GPX-logger work, so it's the
least differentiated on its own. Better as a mode inside idea 2 or 3.

---

## Suggested first move

Ideas 1 and 2 share almost all their plumbing: radio bring-up, GNSS parse,
packet format, microSD logging, bearing/distance math. Build that common core
once, ship **idea 1** as the flagship, and **idea 2** falls out nearly free as
a second mode. Idea 5 needs only the GNSS half and can land any time after.
