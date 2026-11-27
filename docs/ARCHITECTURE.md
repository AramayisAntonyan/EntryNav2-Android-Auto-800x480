# Architecture

Everything runs on the head unit: one ARMv7 machine under Linux (kernel 3.12, glibc 2.15). Three iDrive apps are
loaded by the unit's HMI process, and the Android Auto session runs as two programs beside them.

## The programs

| Program | Where | Role |
|---|---|---|
| `libEntryAad.so` | `/opt/hmi/data/ro/Plugins/EntryAad` | The Android Auto iDrive app: the *Media* entry, its menu, the screen hand-over, the crash guard |
| `libCarGauges.so` | `/opt/hmi/data/ro/Plugins/CarGauges` | The Gauges app under *My Vehicle*: dashboards, settings, fault codes, car tests |
| `libAuxCamera.so` | `/opt/hmi/data/ro/Plugins/AuxCamera` | The Camera app under *My Vehicle*: a USB video grabber's picture |
| `aa-supervisor` | `/opt/aa` | Prepares Bluetooth and Wi-Fi, starts the service, hands Bluetooth back at the end; `--compat` dry-runs the start for the install check |
| `entry-aad` | `/opt/aa` | The Android Auto service: the phone link, the protocol, the video and audio |
| `gauge-collector`, `car-test` | `CarGauges/` | Helpers the Gauges app runs: engine values (read only), and the on-car tests |

## How they fit together

- **Android Auto.** `aa-supervisor` brings up the phone link (the unit's Wi-Fi access point, or the USB cable in
  accessory mode) and runs `entry-aad`, which speaks Android Auto's protocol over TLS and draws the phone's screen on
  a layer of the iDrive. `libEntryAad.so` owns the *Media* entry and tells the service when to come to the front or
  step aside (reverse camera, driving mode, calls). The two talk over a local Unix socket.
- **Gauges.** `libCarGauges.so` draws the dashboards from pictures it carries. Car data (temperatures, rpm, fuel,
  trip values) comes from the iDrive's own data service (CDS). Engine values (boost, OBD, fuel quality) come from
  `gauge-collector`, which reads the engine computer through the diagnostic gateway or the head unit's MOST tunnel,
  read services only.
- **Cluster arrows.** When the arrows are on, the phone's next maneuver is turned into the 6WA cluster's MOST
  HUDTerminal calls (`platform/hud6wa`, `hudterm*`) and logged; sending them to the cluster is experimental.

## The source tree

| Folder | What |
|---|---|
| `core/` | Android Auto protocol: session, TLS, fragments, car description |
| `platform/` | EntryNav2 specifics: video, input, USB, Wi-Fi, the cluster, diagnostics (`hsfz`, `most_diag`) |
| `hmiapp/` | the three iDrive apps and the pictures they draw |
| `tools/` | `aa-supervisor`, `car-test`, `gauge-collector`, `uvc-probe`, the installer and the release packaging |
| `tests/` | PC tests, and ARM tests run under QEMU against the car's own libraries |
| `toolchain/` | the cross-compiler's Docker image |
| `docs/` | this documentation |
