# Changelog

Notable changes to this fork. Format after [Keep a Changelog](https://keepachangelog.com/1.1.0/), versions after
[SemVer](https://semver.org/). Each entry says whether it was **tried in the car** or only **tested on a PC** (the PC
tests, and the ARM programs under QEMU against the car's libraries).

This fork builds on [upstream](https://github.com/Jiraiya43/EntryNav2-Android-Auto)'s main after 0.13.0-rc.10. Only what the fork
adds or changes is listed here; upstream's own history is in its repository.

## [Unreleased]

### 2026-11-27: upstream up to its main after 0.13.0-rc.10, gauge alerts (installed on the test car)

- **Upstream's changes** since 0.13.0-rc.1, among them the Wi-Fi link's reliability (video priority, the phone called
  back after a lost wireless session, power saving off, Bluetooth and SPP retries), parking assistance stepping
  Android Auto aside, clean session ends, logs kept over sleep and copied to a USB stick from the menu, an update from
  a USB stick, and a code audit's fixes. This fork's changes kept on top (docs/ORIGINS.md).
- **Gauge alerts**: a value per gauge (on its *Gauge ranges* page) past which it flashes red; its red zone starts
  there. Tried in the car: it works, but should stand out more (next change).
- **The first dashboard** without settings: Boost, Water, Intake air, Module voltage, Fuel quality, Fuel level, Oil
  pressure. PC only (the test car keeps its own layout).
- **USB-stick updates** take only archives signed with this fork's key, not upstream's.
- **Upstream's MOST cluster arrows** run only with `kombi-most-on`. Tried in the car: the cluster answered none of
  their requests, nothing drawn ([README](README.md#status)).
- **The song title** in the 6WA's street line while idle (NBT-AAD #288), opt-in with `kombi-6wa-media`. PC only.

### Before 2026-11-27

Not published as a release yet. Tried on the test car (F22, EntryNav2, 800x480):

### Added
- **Gauges app** under *My Vehicle*: three dashboards of 2×4 cells, each gauge a card, dial, fuel-gauge arc, wide
  bar/dial/arc or tall bar, drawn in the current M5 cluster style. *Gauge settings* chooses the gauges (by source),
  their looks, their places, and their min/max set by hand (drawn on the head unit). A shift light, the engine's
  fault codes with descriptions and a clear that asks first, Live values, and a *Car tests* page.
- **Engine values** from the DME through the diagnostic gateway (HSFZ), or through the head unit's MOST tunnel when
  the gateway does not answer. Read services only; the firewall rules the gateway needs ship with the release.
- **Camera app** under *My Vehicle*: a USB video grabber's picture (EasyCap), full screen, through usbfs.
- **6WA cluster arrows** (experimental, off by default): the phone's guidance worked out for the cluster's MOST
  HUDTerminal and logged; sending to the cluster is opt-in and parked-only.
- **Album covers** on the Android Auto entry card; **OPTION** opens Android Auto's own panel.

### Changed
- USB takeover from the media player; driving-mode changes and reverse no longer stall the picture; a dropped
  wireless session reconnects without taking the screen.
- The documentation is the project's own, in English ([README](README.md), [docs/](docs/)); the installer is a single
  `install.sh` with checks, backup, verified copy and restore.

### Open
- Arrows in the 6WA cluster are logged but not drawn yet; the console's 360 camera button; see the
  [roadmap](ROADMAP.md).
