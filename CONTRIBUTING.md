# Contributing

Most useful, in order:

1. **A compatibility report** from another car: the install check ([docs/INSTALL.md](docs/INSTALL.md)) changes
   nothing. Open a *Compatibility report* issue with what it prints.
2. **A bug report**, with the version, the car's software (`head -n 1 /etc/version`) and, if you can, the logs from
   the maintenance profile ([docs/SETTINGS.md](docs/SETTINGS.md#logs)).
3. **A pull request**, ideally after an issue about it.

## What never goes into git

- **The car's root password.** It lives in `.secrets/hu-password.txt` (git-ignored); `tools/verify_repo.py` searches
  every pushed commit for it, to be sure it leaked nowhere.
- **BMW files**: the copy of the car's system, its libraries, logs and captures. Tests that need them read them from
  `~/entrynav2/rootfs`, outside the repository. The only exception is the short fixtures in `tests/fixtures/`, whose
  origin is noted in [tests/fixtures/README.md](tests/fixtures/README.md).
- **Personal data**: the car's VIN, Bluetooth and Wi-Fi addresses, GPS positions.
- **Compiled programs.**

`.gitignore` allows only what it lists. After a clone, turn the push check on:

```sh
git config core.hooksPath .githooks
```

## Car safety

- Wireless by default; the USB cable only with the `usb` file, which the menu box sets. Bluetooth is always handed
  back to iDrive. No BMW service is restarted and no BMW file is changed; two folders are added, always removable.
- Diagnostics read only (UDS 0x22, OBD 0x01/0x19). The one exception is clearing the fault codes, which the user
  starts from the Gauges app and which asks first.
- Any new write to the car's network is stated in the pull request and the changelog.
- Never regenerate a frozen fixture to make a test pass: the golden tests compare every byte sent to the phone and
  the cluster. A fixture changes only with an intended change, explained in the commit.

## Say what is proven

Always separate "tried in the car" from "tested on a PC". Nothing counts until a car has shown it. Any command meant
for the car is first tried with the car's BusyBox 1.21 under QEMU.

## Tests and style

| Level | Command | Needs |
|---|---|---|
| PC, as CI | `make check` | `g++`, `libssl-dev`, `libdbus-1-dev`, `protobuf-compiler`, clang-format 18.1.8, ruff 0.6.9 |
| ARM | `bash toolchain/tc.sh make -j4 all hmiapp test-arm-all` | Docker, a copy of the car's system |
| ARM under QEMU | `python3 -I tests/run_qemu.py` | `qemu-user`, the copy |

- C and C++: GCC 4.7 in `-std=gnu++0x` (no C++14), `.clang-format`. Python: 3.9+, a `main()`, `pathlib`. Shell:
  POSIX `sh` for BusyBox 1.21. Format with `python tools/format_sources.py` and `ruff check .`.
- Comments and logs in English, one tag per module (`[Video]`, `[KombiNav]`…). Each source file starts with the two
  header lines (copyright and `SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).

## Commits and releases

- One branch per topic off `main`: `fix/…`, `feat/…`, `docs/…`. Messages in [Conventional
  Commits](https://www.conventionalcommits.org/): `type(scope): description`, 72 characters at most. One commit is
  one coherent change that builds and passes `make check`.
- Releasing: run the checks and the ARM build, update `VERSION` and `CHANGELOG.md`, run
  `python3 tools/package_release.py`, then tag `vX.Y.Z` and publish the archive and its `.sha256`. A version never
  tried in a car is marked *pre-release*.
