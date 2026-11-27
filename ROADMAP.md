# Roadmap

A box is ticked only with proof in the car; a PC test is not enough. See the [README](README.md#status) for the
current state on the test car (an F22, 800x480).

## Works in the car

- [x] Android Auto over USB and wireless: picture, sound, controller, keys, volume
- [x] Gauges: dashboards, the gauge looks, places and hand-set ranges, car-data values
- [x] Engine values and fault codes through the diagnostic gateway (with the diagnostic cable in)
- [x] Camera app: a USB grabber's picture

## Open

- [ ] **6WA cluster arrows.** Worked out and logged; the cluster has not drawn them yet. The car has no navigation,
  which the cluster may require. Next: the cluster tests with the longer listen window, and the navigation-link idea.
- [ ] **Engine values without the diagnostic cable.** The MOST tunnel answers the DME; confirm the gauges stay
  current through it on a drive.
- [ ] **The console's 360 camera button.** No module behind it; find whether the press reaches the head unit
  (the *Watch buttons* car test), then open the Camera app with it.
- [ ] **Wireless stability.** A dropped session reconnects without taking the screen; watch for the Bluetooth link
  flapping seen once.

## Ideas, not started

- Sound over Wi-Fi instead of the car's Bluetooth
- Reading the iDrive's current screen directly (parking sensors, calls)
- Album art on BMW's own media screen
- The song title in the 6WA when idle (once the cluster draws)

## Out of scope

- NBT, NBT Evo, ID7 and MGU head units, and G-series cars: other systems, or better served by
  [upstream](https://github.com/Jiraiya43/EntryNav2-Android-Auto).
- Head-up display: the test car has none.
- Non-BMW cars.
