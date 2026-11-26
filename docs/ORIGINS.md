# Origins of this fork

This project is a fork of **EntryNav2 (EntryEvo) Android Auto** (formerly entry-aad) for EntryNav2 head units with the
small **800x480** screen, run on an F-series BMW (F22, 2019) over the USB cable (wireless not tried there yet). Almost everything is
upstream's work; this fork adds the few things that car needed on top of it.

## Base: upstream main after 0.13.0-rc.10 (`c74e674`, 2026-11-26)

- Project: [Jiraiya43/EntryNav2-Android-Auto](https://github.com/Jiraiya43/EntryNav2-Android-Auto) (formerly
  `Jiraiya43/entry-aad`), PolyForm Noncommercial 1.0.0.
- This branch started from upstream's `v0.13.0-rc.1` (commit `2e0d55f`). Upstream's changes since then, up to its
  `main` at `c74e674` (after `v0.13.0-rc.10`), came in as one commit: upstream's diff, not its history. Among them:
  parking assistance steps Android Auto aside and gives it back (#93, #156); the session ends cleanly when the phone,
  TLS or the video decoder fails (#146, #148, #158, #225-#228); the Wi-Fi link's reliability (Wi-Fi video priority
  #131, the phone called back after a lost wireless session #135, U-APSD off #160, the beacon rebuilt once #164, the
  Bluetooth link dropped by the stack #166, wireless pictures spaced #149, wide wireless pictures #190, strict TCP
  listening and write delays #232/#233, SPP retries #97/#209); logs kept over sleep and copied to a USB stick (#162,
  #163); an update from a USB stick (#178); the car's series and driver's side from the coding (#157, #169); F-series
  cluster arrows over MOST (#170, #172, #177, #239, #246); a code audit's fixes (#230-#248).
- Upstream took two of this fork's changes: fragments without a FIRST are dropped (#115) and, out of reverse, Android
  Auto waits for the camera to close (#114): upstream's code is used for both.
- Required Notice: Copyright (c) 2026 Jiraiya43 - entry-aad

The fork first started from entry-aad 0.11.0-rc.1 and added its own USB mode, 800x480 support and older-controller
reading, then its own pings on a USB link. Upstream has since added all four in its own way (USB cable mode with a menu
checkbox, screen size from the compositor, the older controller when the newer one stays silent, and its own ping to a
phone silent for 2 s), so on this base the fork uses upstream's versions and keeps only what they do not cover.

## Changes made here

- **USB takeover from the media player** (`/var/local/data/AA/usb-takeover`, off without the file). On this car the
  media player's CarLife module (`vmostAuxInUSB`) claims every phone that switches to accessory mode, so the
  accessory is taken back from it with a disconnect-and-claim limited to usbfs: never from a kernel driver.
- **Album covers.** The service keeps the track and cover Android Auto sends, and the iDrive entry shows the cover on
  its preview card. The iDrive app also logs a few CDS values (phone and entertainment source) when they change.
- **6WA junction view and directions at any distance** (NBT-AAD #162 and #205). From 300 m to a maneuver the 6WA
  arrow becomes the maneuver's roads with the route over them (layers 7/8), its tail shorter in 4 stages, a
  roundabout's exit number in the ring (`platform/hudterm_junction.hpp`, `hudterm_digits.hpp`;
  `/var/local/data/AA/kombi-6wa-junction-off` keeps the arrow). The Android Auto menu's *Directions at any distance*
  (`/var/local/data/AA/kombi-anydist`) shows the next maneuver as soon as the phone sends it.
- **The rest of NBT-AAD's 6WA guidance rules** (tom-pryor, #157 and #162; `platform/hud6wa.cpp`), in place of the
  plain 1-mile approach: a maneuver shows at 50 s to it by the phone's seconds, off-ramps and forks also from 2 km;
  at the main prompt (20 s, or 1 km for those) the junction view comes in, or the panel is switched on again if the
  driver dismissed it; once shown it stays until the next one; a phone without seconds gets the 1-mile rule (logged
  once a session). Also: a next step that only looks like the one shown (same type and road) is a new maneuver, a
  DEPART at 0 m is not drawn, a changed roundabout exit number is a new maneuver (also read from the older
  NextTurnEvent), the distance text is kept short and a first maneuver waits 3 s for its distance, the clear always
  swaps the buffers back, and the phone's lanes can go to layers 9/10 (`/var/local/data/AA/kombi-6wa-lanes`; off,
  as in NBT-AAD: where they show is a guess). `/var/local/data/AA/kombi-6wa-approach` picks the rule: `time [warn s
  [main s]]`, `distance` or `off`. Not ported, as they concern NBT-AAD's own transport or the factory navigation this
  car does not have: the batch queue per slot, the yield to a factory route and its clean-up, the shadow marker.
- **OPTION opens Android Auto's options panel.** The iDrive opens an option panel for every OPTION press, so the
  Android Auto page now has its own: *Show map buttons* (Google Maps hides them once a route runs, and this car has no
  touch screen; one knob step and back by default, or a tap or a key code from `/var/local/data/AA/option-key`), *Tap
  the map*, *Android Auto menu*. Android Auto steps aside while the panel shows and comes back after a choice or BACK.
- **Driving mode changes leave Android Auto on screen.** Upstream steps the picture aside 3.5 s per change so that
  iDrive's driving mode popup shows (G-series). On the F22 the mode shows in the cluster only and iDrive opens
  nothing, so the step-aside only stalled the picture (one press sends two values, each restarting it). The
  service's driving mode peek and the popup peek's exception are removed, and the iDrive app no longer reports a
  change (upstream reads the coding's driving mode popup setting instead, #112; a car without that setting would
  still step aside); real iDrive popups keep their step-aside.
- **Crash guard: crashes only.** A short run counts as a crash only when iDrive restarted while the unit kept running
  or the unit stored a new core dump (a restart of the unit by coding, the volume knob or ignition is not one); kept
  in upstream's date file, so that 3 crashes within 15 minutes turn the app off until they are older (#212, #213).
- **iDrive ids.** Upstream's menu items (keep logs, copy logs, update) took actions 27-29 and model 41: this fork's
  options panel moved to actions 47-49 and *Directions at any distance* to model 44.
- **Engine and oil temperature logged** from CDS `engine.temperature` (`{"temperature":{"engine":…,"oil":…}}`), for
  cluster gauges later.
- **Gauges under My Vehicle** (`opt/hmi/data/ro/Plugins/CarGauges`, a second iDrive app; `/var/local/data/AA/gauges-off`
  turns it off). Water and oil temperature, torque and rpm from the iDrive's car data, as bars with their values, in one
  column for the 800x480 screen; its entry has a car icon, which small iDrive screens otherwise lack there. Layout and
  approach after ID6 apps' BoostDeck (MIT); CDS fields as AAIdrive reads them (MIT).
- **Gauges app under My Vehicle** (`opt/hmi/data/ro/Plugins/CarGauges`; `/var/local/data/AA/gauges-off` turns it
  off). The entry opens the dashboard; OPTION holds the other views, *Gauge settings* and *Diagnostics*. Each gauge has
  a look (*Gauge styles*): a card (icon, name, value, bar), a dial (a half circle in the style of BMW's clusters: one rim
  line turning in at its ends, notches inside it, the F87 M2's slotted needle), an arc (the F-series fuel gauge's
  shallow arc under its pivot), a wide bar, wide dial or wide arc over two cells,
  or a tall bar over two cells (BoostDeck's vertical bar); bars are the current M5's wedge of slanted segments. No card
  background; red needles and white bars; the pictures sent by the app from its own pack (iDrive refuses a large
  image archive); filled with any value ticked in
  the three *Choose* pages: 62 values from the iDrive's car data, standard OBD and BMW engine values (`platform/gauge_catalogue.hpp`,
  from `hmiapp/gauge_catalogue.py`); a shift light; the engine's fault codes, read only. DME values come from
  `gauge-collector` (HSFZ through the diagnostic gateway, read services only), whenever an engine gauge shows
  (`/var/local/data/AA/gauges-dme-off` turns them off); the gateway's address is this car's (the installer writes
  it) or found by its announcement. The entry has a car icon and a preview card with the car and trip lines; the
  owner's own photo is used when `.local/gauges_card_549x484.png` exists (`tools/card_from_photo.py`, kept out of git).
- **`uvc-probe`**, a read-only developer tool for a USB video adapter (a front camera), not part of a release.
- **Camera app under My Vehicle** (`opt/hmi/data/ro/Plugins/AuxCamera`; `/var/local/data/AA/camera-off` turns it
  off). An EasyCap grabber's picture (MacroSilicon 534d:0021, UVC MJPEG 480x320 / 640x480 / 720x480) on one page, a
  few pictures a second while it shows, sent through an raImageModel by path as Android Auto's album cover; a line
  when there is no adapter or no signal. No uvcvideo in the kernel: user-space UVC over usbfs
  (`platform/uvc_usbfs.cpp`, shared with `uvc-probe --usbfs-grab`), only ever that vendor:product id.
- **Arrows for the F-series 6WA cluster, log only** (`/var/local/data/AA/kombi-6wa`, off without the file). Upstream's
  arrows speak SOME/IP to a G-series KOMBI, which an F-series car does not have. With the file, the phone's next
  maneuver is worked out for the 6WA's MOST HUDTerminal instead (picture, street, distance and unit, NBT-AAD's driver
  ported in `platform/hudterm*.hpp` and `platform/hud6wa.*`) and every call is written to the service log. On the
  car, the HUDTerminal (FBlock `0x82`, device `0101`) answers this head unit's reads through vMOST (its function list;
  ErrorAck for a wrong op type), but no method call (op 0, 2 or 6, any payload layout, channels 0, 1 and 4) has been
  answered or drawn anything yet. `car-test` (Gauges, *Car tests*) holds the on-car tests, including `--raw` calls.
- **Directions at any distance restarts a running Android Auto**, as the USB cable checkbox does, so the change
  applies at once (after NBT-AAD's restart on leaving Settings when a checkbox changed, rhmiapp-68).
- **Engine fault codes: descriptions and clearing** (Gauges, *Engine fault codes*). Each code shows with a short
  description (`platform/dme_codes.hpp`, ID6 apps' `dme_descriptions.h`, MIT, unchanged). *Clear the codes...* opens a
  page that says what clearing does; its one button runs `gauge-collector --clear`: UDS `14 FF FF FF` to the DME, the
  extended session (`10 03`, then back to `10 01`) only after a refusal for the session, then the codes read again
  (as ID6 apps' and EntryNav-Apps' Engine Fault Codes do). `hsfz::clearFaultMemory` is the only write in the
  diagnostics code; `hsfz::exchange` still refuses everything but reads. The owner's choice (2026-11-19).
- **Firewall rules for the diagnostic gateway** (`etc/firewall/entry-aad.rules` in the package, to `/etc/firewall/`).
  The head unit's own firewall (OUTPUT and INPUT policies DROP) drops HSFZ and the gateway search on `vbr0`, which
  left every connection to the gateway in SYN_SENT on this car. The three rules are CallMeLewis/EntryNav-Apps'
  (MIT), found on the same head unit; BMW's `iptables-firewall` runs the file at start-up.

## Reviewed, not ported (2026-11-19)

NBT-AAD `87fd962..a868ff5` and its open branches, and ID6 apps up to `d0df14a`, were read for anything that fits
this car. Left out, and why:

- NBT-AAD's cluster type setting (J29 / 6WA / 6WB): this fork drives the 6WA only.
- NBT-AAD's track title in the cluster (`feat/native-audio-kombi-media`): SOME/IP to the Supra's G-series cluster; the
  6WA is on MOST.
- NBT-AAD's camera views by iDrive context (`feat/camera-ctx`): for a button-opened 360 / Top View, with NBT
  context numbers; the F22 has the rear camera and parking view only, and this app already steps aside for any
  iDrive screen that takes the focus.
- NBT-AAD's USB start-up reset fix and longer retry gaps (v1.9.92): written for the NBT's QNX USB stack. This
  fork's USB loop already treats an accessory it switched itself as its own (no reset); the gaps were tuned on one
  phone model and stay a fallback if a phone needs them here.
- NBT-AAD's Wi-Fi Direct passphrase and group name, Bluetooth SPP re-registration, navigation prompts over the car's
  audio, NBTApps packages, translations: NBT (QNX) wireless and audio paths, or their packaging.
- ID6 apps' BEAMer (exterior light patterns) and the MOST fallback for Bootmod3 and exhaust controls: they write to
  the car; this fork only reads. Road Jet (a game): not taken. Saved gauge preferences and polling only visible
  gauges: the Gauges app already does both.

## Referenced

| Project | Licence | How it is used |
|---|---|---|
| [Sq00ky/Supra-iDrive6-Android-Auto](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto) (NBT-AAD, by nanaisu) | PolyForm Noncommercial 1.0.0 | The origin of entry-aad. Its 6WA cluster driver (MOST HUDTerminal, FBlock `0x82`, tested by its author on an F20 with an NBT Evo) is ported here: the protocol, the maneuver pictures, the 6WA layout and its batches of calls, adapted to this project's phone messages. Its MOST transport is not: this head unit has its own vMOST client. Its programs target NBT Evo (QNX) and cannot run on EntryNav2 (Linux). |
| [inotbarnacle/ID6_apps_prerelease](https://github.com/inotbarnacle/ID6_apps_prerelease) (ID6 apps) | MIT | The Gauges app's page layout (a bar image over a text label per gauge, bars pre-drawn and switched by image id) and its tall bar over two cells follow its BoostDeck, written anew for EntryNav2's Linux at 800x480. Its BoostDeck DME identifiers (boost and other engine values) are used in the Gauges catalogue; its DME fault code descriptions are `platform/dme_codes.hpp`. car-test's MOST diagnosis tunnel test follows its MOST fallback's message format (FBlock 06.F0, function 050, routine 0F0B, source 63), written anew for vMOST. |
| [CallMeLewis/EntryNav-Apps](https://github.com/CallMeLewis/EntryNav-Apps) | MIT | The three firewall rules that open the diagnostic gateway's ports on EntryNav2 (its `install/firewall.rules`), and the fault code clear's sequence of its Engine Fault Codes (itself from ID6 apps). |
| [jpt/barlow](https://github.com/jpt/barlow) (Barlow) | SIL Open Font License 1.1 | The figures on the Gauges app's dials and bar scales: rendered once from Barlow Semi Condensed SemiBold (`tools/make_gauge_figures.py`) into `hmiapp/res/gauge_figures.png`. The font itself is not part of the project. |
| [google/material-design-icons](https://github.com/google/material-design-icons) (Material Symbols) | Apache License 2.0 | The gauges' icons (thermometer, oil, turbo fan, ...): rendered once from Material Symbols Outlined into `hmiapp/res/gauge_icons.png`. |
| [BimmerGestalt/AAIdrive](https://github.com/BimmerGestalt/AAIdrive) | MIT | The CDS JSON paths of the Gauges catalogue (`hmiapp/gauge_catalogue.py`), as its `cds/CDSMetrics.kt` reads them, e.g. `engine.temperature`. |
| [JayJay3071/nbt-android-auto](https://github.com/JayJay3071/nbt-android-auto) | - | Read only: its `docs/CLUSTER-HUDTERMINAL.md` describes the HUDTerminal functions the on-car cluster tests call. No code taken. |
| [NBT-AAD pull request #264](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/pull/264) (FreZZZeR) | - | Read only: how a 6WA answers the drawing calls (ImageGetSize 176x126, copies of its answers to 52.00). Not ported while it is open. |

Required Notice from the origin project, kept as its licence asks:

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

## Setting up this car (F22, 800x480, USB)

As in upstream's [installation guide](INSTALLATION.md) and [USB notes](USB.md) (in French), plus the takeover file.
Over SSH, Android Auto stopped from its menu:

```sh
echo 3-1 > /var/local/data/AA/usb          # USB cable mode on the phone's port (the menu checkbox: every port)
touch /var/local/data/AA/usb-takeover      # take the phone back from the media player's CarLife module
touch /var/local/data/AA/kombi-6wa         # arrows for the 6WA cluster: log only for now (lines tagged Hud6wa)
```

Sound stays on the phone's Bluetooth link: select the iDrive's **Bluetooth audio** source.

## Private data

Not in git: the car's password, its address and SSH host key, logs and copies of BMW files. They stay in local-only
folders, which `.gitignore` excludes and `tools/verify_repo.py` refuses on every push.
