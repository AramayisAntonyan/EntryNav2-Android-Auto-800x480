<div align="center">

# EntryNav2 Android Auto: F-series 800x480 fork

**Android Auto over the USB cable on BMW's EntryNav2 (EntryEvo) head unit with the 800x480 screen, in an F-series
car.**

[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-blue)](LICENSE.md)

</div>

> [!NOTE]
> **Forks.** This project is a fork of a fork:
>
> 1. **[Sq00ky/Supra-iDrive6-Android-Auto](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)** (NBT-AAD), Android
>    Auto for iDrive 6 head units, and the F-series 6WA cluster driver;
> 2. **[Jiraiya43/EntryNav2-Android-Auto](https://github.com/Jiraiya43/EntryNav2-Android-Auto)**, which brought it to the
>    EntryNav2 head unit: almost all of the code here is theirs;
> 3. **this fork**, on upstream 0.13.0-rc.1, for one kind of car upstream does not cover yet (below).
>
> It also uses parts of [ID6 apps](https://github.com/inotbarnacle/ID6_apps_prerelease) and
> [AAIdrive](https://github.com/BimmerGestalt/AAIdrive). Every change made here and every licence notice:
> [docs/ORIGINS.md](docs/ORIGINS.md).

> [!WARNING]
> Experimental software that writes to the car's head unit, tried on **one car**. It is not affiliated with BMW or
> Google. Read [Status](#status) and the [disclaimer](#licence-and-disclaimer) first.

## Who this fork is for

Upstream targets the EntryNav2 in G-series cars, wireless first. This fork is for the other end of the range:

| | Upstream | This fork |
|---|---|---|
| Car | G-series (G20 reference), some F-series reported | **F-series** (F22, 2019) only |
| Screen | 1280x480, 800x480 | **800x480** |
| Phone link | wireless (the unit's CarPlay Wi-Fi), USB as an option | **USB cable** (wireless not tried yet) |
| Head-up display | yes, where fitted | none (the F22 has none) |
| Navigation in the car | any | **none** (EntryNav2 without navigation) |
| Cluster arrows | G-series KOMBI over SOME/IP | **6WA cluster over MOST** (HUDTerminal), work in progress |
| Controller | iDrive controller | also the **older 5-key controller** |

If your car is a G-series, use upstream: it is better tested and maintained.

## The test car

Everything in [Status](#status) was seen on one car; nothing else has been tried.

| | |
|---|---|
| Car | BMW 230i Coupé (F22), 2019, manual gearbox, engine software as from the factory (no tune) |
| Head unit | EntryNav2 (*EntryEvo*, Linux), 800x480 screen, **no navigation**, no CarPlay activation, no external Wi-Fi antenna, no touch screen |
| Controller | the older iDrive controller (its keys read in the older protocol) |
| Cluster | **6WA**: analog dials with a colour screen in between, which can show navigation arrows when a BMW navigation drives it |
| Head-up display | none (the F22 has no head-up display) |
| Cameras and sensors | factory rear camera and parking sensors; the windscreen driver-assistance camera |
| Console | a retrofitted M3/M4-style centre console panel: every button works except its camera button (see *Front camera* below) |
| Phone | Android, connected with a USB cable |

## What this fork adds

- **USB takeover from the media player.** On this car the media player claims every phone that switches to Android
  Auto's accessory mode. The service takes the phone back from it (usbfs only, never from a kernel driver).
- **Album covers** on the iDrive entry's preview card.
- **OPTION opens Android Auto's own panel**: *Show map buttons* (Google Maps hides them during a route and this car has
  no touch screen), *Tap the map*, *Android Auto menu*.
- **Out of reverse, Android Auto waits for the camera to close** instead of flickering between the two.
- **Driving mode changes leave Android Auto on screen.** Upstream steps the picture aside so that iDrive's driving
  mode popup shows (G-series); on the F22 the mode shows in the cluster only, so that only stalled the picture.
- **Gauges, a second iDrive app under My Vehicle.** Dashboards of cards (water and oil temperature, torque, rpm, trip
  values and more from the iDrive's car data), a shift light, the engine's fault codes (read only; not working on this
  car yet, see below), and a *Car tests* page with on-screen results for checks at the car.
- **6WA cluster arrows** (NBT-AAD's driver, ported with its guidance rules, junction view, lanes and *directions at any
  distance*). They are worked out from the phone's guidance and logged; sending them to the cluster is off by default
  until the cluster accepts them (see Status).
- **Fragment fix** from NBT-AAD #223: an Android Auto message whose first part was lost is dropped, not joined cut.

The details, file by file: [docs/ORIGINS.md](docs/ORIGINS.md).

## Status

On [the test car](#the-test-car):

| Feature | State |
|---|---|
| Android Auto over USB: picture, sound, controller, keys, volume | **works** in the car |
| Album covers | **works** in the car |
| Gauges app (cards of the iDrive's car data) | **works** in the car |
| OPTION panel, return from reverse | built and tested on a PC, car check pending |
| Engine values and fault codes from the DME | **does not work yet** (below) |
| Arrows in the 6WA cluster | **does not work yet** (below) |

Every change is tested on a PC, and the ARM programs run under QEMU against the car's own libraries. Only a car test
counts as proof.

### New in this version, not yet tried in the car

This version was installed on the test car once, but **none of the following has been used or checked in the car
yet**. They pass the PC and QEMU tests only.

- **Upstream 0.13.0-rc.1 as the base**: a frozen picture recovered by asking the phone for a full frame, the volume pill
  showing the right source and the mute, MAP opening Android Auto's navigation, Wi-Fi log fixes.
- **Driving mode changes leave Android Auto on screen** (they used to stall the picture for a moment).
- **The rest of NBT-AAD's 6WA guidance rules**: when a maneuver shows (by the phone's seconds: 50 s ahead, main prompt
  at 20 s; off-ramps and forks from 2 km), renewed steps, no departure arrow at 0 m, lanes (off by default), short
  distance text. Still log only, as the cluster draws nothing yet.
- **Android Auto fragment fix** (NBT-AAD #223): a message whose first part was lost is dropped instead of being shown
  cut.
- **Sending the 6WA calls to the cluster** (`kombi-6wa-live`, off by default; only parked, for tests).
- **New Car tests** (Gauges, *OPTION*, *Car tests*): *Cluster: settings read* (read only), *Engine via MOST tunnel*
  (DME reads only), *Listen to the diagnosis block 20 s*; and over SSH only, `car-test --nav-draw` (the head unit
  presented as the car's navigation, then a test drawing).

### What does not work

**Not working yet (being worked on):**

- **Arrows in the 6WA cluster.** The arrows are worked out from the phone's guidance and written to the log, but the
  cluster shows nothing: it answers the head unit's read requests, and ignores every drawing call so far (any message
  layout, any channel). Sending is off by default. Once, after a burst of test calls, the cluster's media line
  disappeared until the next restart, so keep `kombi-6wa-live` off while driving. Next try: presenting the head unit
  as the car's navigation first (this car has none, and the cluster may wait for one).
- **Engine values and fault codes from the DME** (boost, intake air, oil pressure, the fault code list). The car's
  diagnostic gateway answers the head unit's ping but ignores its diagnostic connections; it accepts them only from
  the OBD socket (E-Sys, ISTA). The Gauges app therefore shows only the iDrive's own car data (temperatures, torque,
  rpm, trip). Next try: the head unit's diagnosis tunnel over MOST, as ID6 apps do.
- **Knob centre press on Android Auto's media screen** acts on the button Android Auto has selected, which is often
  "previous track": the track restarts or goes back. The head unit sends a plain "select"; which button gets it is
  Android Auto's choice.
- **Album covers** show only on this project's iDrive entry card, not on BMW's own media screen or its side panel.

**Not tried in the car yet:**

- **Wireless Android Auto.** Upstream's wireless mode is still in the code. Other owners report that it can work
  without CarPlay activation and without the external antenna, as long as the phone is close enough to reach the
  unit's Wi-Fi (an F20 with the 800x480 screen, upstream #45). To try it: untick USB in the Android Auto menu.
- The OPTION panel and the return from reverse; the trip values' car data fields (average consumption and speed);
  where the Gauges app lands in My Vehicle.

**Not available on this car:**

- **Car GPS**: with no navigation, the head unit has no position to give, so Android Auto uses the phone's own GPS
  (upstream's fallback, automatic).
- **Head-up display and upstream's cluster arrows**: the F22 has no head-up display, and upstream's arrows go over
  SOME/IP to a G-series cluster, which F-series cars do not have (hence the 6WA work above).
- **Touch**: no touch screen. Google Maps hides its map buttons once a route runs; the OPTION panel's *Show map
  buttons* and *Tap the map* stand in for a touch.
- **Front camera.** The retrofitted console has a camera button, but the car has no front camera, so the button does
  nothing. The plan is an aftermarket front camera through a USB video adapter, shown by the head unit on that button
  or as an entry next to BMW's own camera view. Nothing of it works yet: only `uvc-probe`, a read-only developer tool
  that checks whether the head unit recognises such an adapter, exists so far (not part of a release).

**Head unit limits worth knowing:** `reboot` over SSH does nothing on this unit. Restart it by holding the volume
knob about 30 s; when that does not work, disconnecting the battery does.

## Requirements

- An F-series BMW with an **EntryNav2** head unit (*EntryEVO* inside its system) and the **800x480** screen.
- A USB cable from the car's USB port to the phone, and an Android phone with Android Auto.
- A PC with an SSH client and an **ENET cable** (OBD to Ethernet), and the head unit's **root password** (not provided
  here).

NBT, NBT Evo, ID7 and MGU head units run other systems: this project cannot run on them.

## Installing

The installation is upstream's manual procedure over SSH: [docs/INSTALLATION.md](docs/INSTALLATION.md) (in French;
the commands speak for themselves). In short: copy `entrynav2-android-auto-<version>-armhf.tar` to the unit's `/tmp`,
run the read-only checks, install the two folders (`/opt/aa`, `/opt/hmi/data/ro/Plugins/EntryAad`) and, for this
fork, the Gauges app (`/opt/hmi/data/ro/Plugins/CarGauges`), then restart the unit. Build the archive yourself
([Building](#building-from-source)).

### This fork's settings

Each is a file in `/var/local/data/AA`; without the file the feature is off (or at its default).

| File | Effect |
|---|---|
| `usb` | the USB port Android Auto uses (upstream's setting, e.g. `3-1`) |
| `usb-takeover` | take the phone back from the media player |
| `option-key` | what *Show map buttons* does: `nudge` (default), `tap`, or a key code |
| `kombi-6wa` | work out the 6WA arrows and log them |
| `kombi-6wa-live` | also send them to the cluster (experimental: only parked, for tests) |
| `kombi-6wa-approach` | when a maneuver shows: `time [warn s [main s]]` (default 50 s / 20 s), `distance`, `off` |
| `kombi-6wa-lanes`, `kombi-6wa-junction-off` | lanes on; the junction view off |
| `kombi-anydist` | show the next maneuver at any distance (also in the Android Auto menu) |
| `gauges-off` | hide the Gauges app |
| `gauges-dme`, `gateway-ip` | engine values from the DME, and the gateway's address (not working on this car yet) |

Logs: `/tmp/entry-aa-service.log` (service), `/var/local/data/AA/app.log` (iDrive apps),
`/var/local/data/AA/car-tests.log` (Car tests page). More in [docs/MAINTENANCE.md](docs/MAINTENANCE.md) (upstream's,
in French).

## Building from source

You need Docker, and for the ARM build **a copy of the car's system** (`HU_ROOTFS`). That copy holds BMW files: it
never goes into git. Without it, the PC tests still run.

```sh
make check                                              # style, content checks, PC tests
docker build -t entrynav2-tc:4.7 toolchain              # once: GCC 4.7 for ARMv7 hard-float
bash toolchain/tc.sh make -j4 all hmiapp test-arm-all   # ARM programs, linked against the car's libraries
python3 -I tests/run_qemu.py                            # ARM tests under QEMU with the car's libraries
make verify_binary verify_wireless                      # record the programs that passed
python3 tools/package_release.py                        # artifacts/entrynav2-android-auto-<version>-armhf.tar
```

## Repository layout

```text
core/        Android Auto protocol: session, TLS, fragments, car description
platform/    EntryNav2 specifics: video, layers, input, GPS, microphone, USB, cluster (hudterm*, hud6wa, kombi_*)
hmiapp/      the iDrive apps loaded by HmiMain3D: EntryAad (Android Auto) and CarGauges (Gauges)
tools/       supervisor, car-test, gauge-collector, release packaging and checks
tests/       PC tests and ARM tests under QEMU
toolchain/   Docker image of the cross compiler
docs/        upstream's documentation (French) and this fork's ORIGINS
```

## Credits

This fork exists only because of the people below, who did the hard work and shared it. Thank you.

- **[Jiraiya43/EntryNav2-Android-Auto](https://github.com/Jiraiya43/EntryNav2-Android-Auto)** by Jiraiya43: the whole
  base of this fork, from the Android Auto protocol to the iDrive app. Almost every line here is theirs. If you want
  to support the work, the upstream author accepts [GitHub Sponsors](https://github.com/sponsors/Jiraiya43); this fork
  asks for nothing.
- **[Sq00ky/Supra-iDrive6-Android-Auto](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)** (NBT-AAD) by nanaisu
  (Sq00ky), the project EntryNav2 Android Auto started from: its F-series 6WA cluster driver (MOST HUDTerminal) is
  ported here with tom-pryor's guidance rules and junction view (#157, #162), *directions at any distance* (#205),
  and pstryju's fragment reassembly fix (#223).
- **[inotbarnacle/ID6_apps_prerelease](https://github.com/inotbarnacle/ID6_apps_prerelease)** (ID6 apps) by
  inotbarnacle: the Gauges app follows its BoostDeck and ShiftDeck (cards, colour palette, pre-drawn bars switched
  by image id, a worker process for diagnostics), uses its DME identifiers, and the MOST diagnosis tunnel test follows
  its MOST fallback's message format.
- **[BimmerGestalt/AAIdrive](https://github.com/BimmerGestalt/AAIdrive)** by the BimmerGestalt team: the Gauges app
  reads the iDrive's car data (CDS) at the JSON paths its `CDSMetrics.kt` uses.

Licences and Required Notices of each: [docs/ORIGINS.md](docs/ORIGINS.md).

## Licence and disclaimer

© 2026 Jiraiya43, under the [PolyForm Noncommercial License 1.0.0](LICENSE.md): any noncommercial use, including
modifying and sharing, as long as the licence goes with every copy. Commercial use is not allowed.

The software comes as is, without any warranty. Installing it modifies the head unit's system and may affect your
warranty. Use it only on a car you own or with its owner's consent, with the car parked during installation and
tests, and never operate the head unit's shell while driving.

BMW and iDrive are trademarks of BMW AG; Android and Android Auto are trademarks of Google LLC. This project is not
affiliated with, endorsed or sponsored by either.
