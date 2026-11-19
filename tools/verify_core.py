# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Check that LICENSE.md carries the project's notice and its credits, and that every source file has its header."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NOTICE = "> Required Notice: Copyright (c) 2026 Jiraiya43 - EntryNav2 (EntryEvo) Android Auto"
CREDITS = "## Credits"
HEADER = "SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0"
SOURCE_DIRS = ("core", "platform", "hmiapp", "tools", "tests")
SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".py", ".sh"}
# Data this project did not write: these files say where they come from instead of carrying the project's notice.
THIRD_PARTY = {"core/include/dhu_sdp.h", "core/include/hu_certificate.h"}
ORIGIN = "Not written by this project"
# Ported from MIT-licensed projects: they keep the MIT licence and name their source in their header.
PORTED_MIT = {
    "platform/dme_codes.hpp",
    "platform/dme_gauges.hpp",
    "platform/hsfz.cpp",
    "platform/hsfz.hpp",
    "tools/gauge_collector.cpp",
}
MIT_HEADER = "SPDX-License-Identifier: MIT"
PORTED = "Ported from "


def source_files():
    """The source files git tracks or would add."""
    listing = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *SOURCE_DIRS],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout
    for name in sorted({item.decode() for item in listing.split(bytes(1)) if item}):
        path = ROOT / name
        if path.suffix.lower() in SUFFIXES and path.is_file() and "fixtures" not in path.parts:
            yield name, path


def main():
    licence = (ROOT / "LICENSE.md").read_text(encoding="utf-8")
    if NOTICE not in licence:
        raise SystemExit("FAIL: LICENSE.md does not carry the project's notice")
    if CREDITS not in licence or "https://github.com/" not in licence.split(CREDITS, 1)[-1]:
        raise SystemExit("FAIL: LICENSE.md lost its credits")
    missing = [
        name
        for name, path in source_files()
        if name not in THIRD_PARTY | PORTED_MIT
        and HEADER not in path.read_text(encoding="utf-8", errors="replace")[:600]
    ]
    if missing:
        raise SystemExit("FAIL: no licence header in " + ", ".join(missing))
    unsigned = [name for name in sorted(THIRD_PARTY) if ORIGIN not in (ROOT / name).read_text(encoding="utf-8")[:600]]
    if unsigned:
        raise SystemExit("FAIL: no origin stated in " + ", ".join(unsigned))
    ported = [
        name
        for name in sorted(PORTED_MIT)
        if MIT_HEADER not in (ROOT / name).read_text(encoding="utf-8")[:600]
        or PORTED not in (ROOT / name).read_text(encoding="utf-8")[:1500]
    ]
    if ported:
        raise SystemExit("FAIL: no MIT header or source stated in " + ", ".join(ported))
    print("PASS: LICENSE.md in order with its credits, every source file has its licence header")


if __name__ == "__main__":
    main()
