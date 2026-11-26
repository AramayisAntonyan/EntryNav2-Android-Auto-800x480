#!/usr/bin/env python3
# Copyright (c) 2026 Jiraiya43
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Every value the Gauges app can show, in one list: the iDrive's car data (CDS), standard OBD current data from the
engine computer, and BMW engine values (BoostDeck's DIDs). Writes platform/gauge_catalogue.hpp (the table the app and
gauge-collector use); hmiapp/make_gauges_xml.py builds the "Choose gauges" list from the same entries.

Sources:
  CDS:  JSON paths as AAIdrive reads them (github.com/BimmerGestalt/AAIdrive, cds/CDSMetrics.kt, MIT); names present on
        this car (research/cds-properties.txt). The key is looked for anywhere in the property's text.
  OBD:  SAE J1979 service 01 PIDs and their formulas (A, B: the answer's data bytes). Which ones the DME supports is
        asked once (01 00 / 20 / 40 / 60) by gauge-collector --supported; the others stay hidden.
  DID:  ID6 apps' BoostDeck (MIT), measured on a B58; this car's B46/B48 may differ (gauge-collector --probe).

Order matters: the owner's ticks are saved by position (gauges-choice), so a new value goes at the end of its list
and lists are only appended to once installed.

Usage: gauge_catalogue.py <platform/gauge_catalogue.hpp>
"""

import sys
from pathlib import Path

# (title, unit, min, max, decimals)
# CDS: property, json key, invalid (>= this is "no value"; 0: none), on by default
CDS = [
    ("Water", "C", 40, 130, 0, "engine.temperature", "engine", 255, True),
    ("Oil", "C", 40, 150, 0, "engine.temperature", "oil", 255, False),
    ("RPM", "rpm", 0, 7000, 0, "engine.RPMSpeed", "RPMSpeed", 0, False),
    ("Torque", "Nm", 0, 450, 0, "engine.torque", "torque", 0, False),
    ("Speed", "km/h", 0, 250, 0, "driving.speedActual", "speedActual", 0, False),
    ("Speed shown", "km/h", 0, 250, 0, "driving.speedDisplayed", "speedDisplayed", 0, False),
    ("Accelerator", "%", 0, 100, 0, "driving.acceleratorPedal", "position", 0, False),
    ("Clutch", "%", 0, 100, 0, "driving.clutchPedal", "position", 0, False),
    ("Lateral accel.", "m/s2", -12, 12, 1, "driving.acceleration", "lateral", 65000, False),
    ("Longitudinal accel.", "m/s2", -12, 12, 1, "driving.acceleration", "longitudinal", 65000, False),
    ("Steering angle", "deg", -540, 540, 0, "driving.steeringWheel", "angle", 0, False),
    ("Fuel level", "l", 0, 52, 0, "sensors.fuel", "tanklevel", 0, True),
    ("Range", "km", 0, 900, 0, "sensors.fuel", "range", 0, False),
    ("Battery", "%", 0, 100, 0, "sensors.battery", "battery", 255, False),
    ("Battery temp.", "C", -20, 60, 0, "sensors.batteryTemp", "batteryTemp", 255, False),
    ("Outside temp.", "C", -30, 50, 0, "sensors.temperatureExterior", "temperatureExterior", 0, False),
    ("Inside temp.", "C", 0, 50, 0, "sensors.temperatureInterior", "temperatureInterior", 0, False),
    ("A/C exchanger", "C", -10, 60, 0, "climate.ACSystemTemperatures", "heatExchanger", 0, False),
    ("A/C evaporator", "C", -10, 40, 0, "climate.ACSystemTemperatures", "evaporator", 0, False),
    ("A/C compressor", "%", 0, 100, 0, "climate.airConditionerCompressor", "actualPower", 255, False),
    ("Altitude (GPS)", "m", 0, 3000, 0, "navigation.GPSExtendedInfo", "altitude", 0, False),
    ("Heading (GPS)", "deg", 0, 360, 0, "navigation.GPSExtendedInfo", "heading", 0, False),
    ("Odometer", "km", 0, 400000, 0, "driving.odometer", "odometer", 0, False),
    ("Avg consumption", "l/100km", 0, 20, 1, "driving.averageConsumption", "averageConsumption1", 0, False),
    ("Avg speed", "km/h", 0, 150, 0, "driving.averageSpeed", "averageSpeed1", 0, False),
]

# OBD: pid, bytes, formula kind (see formula() in gauge_catalogue.hpp), a, b
OBD = [
    ("Engine load", "%", 0, 100, 0, 0x04, 1, "A*a", 100 / 255, 0),
    ("Coolant (OBD)", "C", -40, 130, 0, 0x05, 1, "A+b", 1, -40),
    ("Short fuel trim", "%", -25, 25, 1, 0x06, 1, "A*a+b", 100 / 128, -100),
    ("Long fuel trim", "%", -25, 25, 1, 0x07, 1, "A*a+b", 100 / 128, -100),
    ("Intake pressure", "kPa", 0, 255, 0, 0x0B, 1, "A*a", 1, 0),
    ("RPM (OBD)", "rpm", 0, 7000, 0, 0x0C, 2, "AB*a", 0.25, 0),
    ("Speed (OBD)", "km/h", 0, 250, 0, 0x0D, 1, "A*a", 1, 0),
    ("Timing advance", "deg", -20, 50, 1, 0x0E, 1, "A*a+b", 0.5, -64),
    ("Intake air (OBD)", "C", -30, 80, 0, 0x0F, 1, "A+b", 1, -40),
    ("Air flow (MAF)", "g/s", 0, 250, 1, 0x10, 2, "AB*a", 0.01, 0),
    ("Throttle", "%", 0, 100, 0, 0x11, 1, "A*a", 100 / 255, 0),
    ("Run time", "s", 0, 7200, 0, 0x1F, 2, "AB*a", 1, 0),
    ("Rail pressure (OBD)", "bar", 0, 300, 0, 0x23, 2, "AB*a", 0.1, 0),
    ("Fuel level (OBD)", "%", 0, 100, 0, 0x2F, 1, "A*a", 100 / 255, 0),
    ("Distance since clear", "km", 0, 65535, 0, 0x31, 2, "AB*a", 1, 0),
    ("Barometric", "kPa", 70, 110, 0, 0x33, 1, "A*a", 1, 0),
    ("Catalyst temp.", "C", 0, 1000, 0, 0x3C, 2, "AB*a+b", 0.1, -40),
    ("Module voltage", "V", 10, 16, 2, 0x42, 2, "AB*a", 0.001, 0),
    ("Absolute load", "%", 0, 200, 0, 0x43, 2, "AB*a", 100 / 255, 0),
    ("Commanded lambda", "", 0.7, 1.3, 3, 0x44, 2, "AB*a", 1 / 32768, 0),
    ("Relative throttle", "%", 0, 100, 0, 0x45, 1, "A*a", 100 / 255, 0),
    ("Ambient (OBD)", "C", -30, 50, 0, 0x46, 1, "A+b", 1, -40),
    ("Pedal D", "%", 0, 100, 0, 0x49, 1, "A*a", 100 / 255, 0),
    ("Commanded throttle", "%", 0, 100, 0, 0x4C, 1, "A*a", 100 / 255, 0),
    ("Oil temp. (OBD)", "C", -40, 150, 0, 0x5C, 1, "A+b", 1, -40),
    ("Fuel rate", "l/h", 0, 40, 1, 0x5E, 2, "AB*a", 0.05, 0),
    ("Torque demand", "%", -125, 130, 0, 0x61, 1, "A+b", 1, -125),
    ("Torque actual", "%", -125, 130, 0, 0x62, 1, "A+b", 1, -125),
    ("Reference torque", "Nm", 0, 600, 0, 0x63, 2, "AB*a", 1, 0),
]

# DID (BMW, DME unless noted): did, ecu, title...; decoded in platform/dme_gauges.hpp; boost uses the ambient DID too.
DID = [
    ("Boost", "bar", 0, 2.5, 2, 0x4205),
    ("AFR", "AFR", 7, 25, 1, 0x5889),
    ("Timing (DME)", "deg", -30, 50, 1, 0x4A49),
    ("Intake air (DME)", "C", -30, 80, 0, 0x580F),
    ("Rail pressure", "bar", 0, 300, 0, 0x56D7),
    ("Oil pressure", "bar", 0, 8, 1, 0x586F),
    ("Coolant (DME)", "C", -30, 130, 0, 0x4300),
    # The DME's adapted fuel quality (MEVD17.2.x, B48: FAK_KRAFTSTOFFQUALITAET_ADAPT, 0 = RON 98 .. 1 = RON 91), shown
    # as Armenia's grades (gauge_values.hpp valueText): Regular 92, Premium 95, Super 98; the DME learns no higher than
    # 98, so racing fuel (100) reads as "Super+". The scale is marked at the three grades.
    ("Fuel quality", "RON", 92, 98, 1, 0x5776),
]

FORMULAS = {"A*a": 0, "A+b": 1, "A*a+b": 2, "AB*a": 3, "AB*a+b": 4}

# How a gauge looks on the dashboard until the owner picks another look (Gauge styles; hmiapp/gauge_layout.hpp kLook*):
# 0 card, 1 dial (half circle), 2 wide bar (two cells across), 3 wide dial, 4 tall bar (two cells high), 5 arc (the
# fuel gauge's shape), 6 wide arc.
LOOKS = ("Card", "Dial", "Wide bar", "Wide dial", "Tall bar", "Arc", "Wide arc")
DEFAULT_LOOK = {"RPM": 3, "Oil": 1, "Boost": 6, "Fuel quality": 5}
# The first dashboard (the owner's, 2026-11-27): each ticked by default, at its cell (0-3 top row, 4-7 bottom;
# a wide look covers the next cell too). Only without a gauges-place file: a saved layout is never overridden.
DEFAULT_PLACE = {
    "Boost": 0,
    "Water": 2,
    "Intake air (OBD)": 3,
    "Module voltage": 4,
    "Fuel quality": 5,
    "Fuel level": 6,
    "Oil pressure": 7,
}
# Each gauge's icon (Material Symbols names, drawn by tools/make_gauge_figures.py into hmiapp/res/gauge_icons.png), as
# BMW's own gauges carry one (the M5's coolant thermometer and turbo); a gauge with none has no icon.
ICONS = {
    "device_thermostat": ("Water", "Coolant (OBD)", "Coolant (DME)"),
    "oil_barrel": ("Oil", "Oil temp. (OBD)", "Oil pressure"),
    "av_timer": ("RPM", "RPM (OBD)"),
    "speed": ("Speed", "Speed shown", "Speed (OBD)", "Avg speed"),
    "settings": ("Torque", "Torque demand", "Torque actual", "Reference torque"),
    "mode_fan": ("Boost", "Intake pressure"),
    "battery_full": ("Battery", "Battery temp."),
    "electric_bolt": ("Module voltage",),
    "local_gas_station": ("Fuel level", "Fuel level (OBD)", "Range", "Fuel rate", "Avg consumption", "Fuel quality"),
    "thermometer": ("Outside temp.", "Ambient (OBD)", "Inside temp."),
    "ac_unit": ("A/C exchanger", "A/C evaporator", "A/C compressor"),
    "altitude": ("Altitude (GPS)",),
    "explore": ("Heading (GPS)",),
    "route": ("Odometer", "Distance since clear"),
    "tune": (
        "Accelerator",
        "Clutch",
        "Throttle",
        "Relative throttle",
        "Pedal D",
        "Commanded throttle",
        "Engine load",
        "Absolute load",
    ),
    "air": ("Intake air (OBD)", "Intake air (DME)", "Air flow (MAF)"),
    "compress": ("Rail pressure", "Rail pressure (OBD)"),
    "mist": ("AFR", "Commanded lambda", "Short fuel trim", "Long fuel trim"),
    "bolt": ("Timing advance", "Timing (DME)"),
    "timer": ("Run time",),
    "landscape": ("Barometric",),
    "heat": ("Catalyst temp.",),
}


# Scales numbered at given values instead of round steps (by unit): the fuel grades.
SCALE_MARKS = {"RON": (92, 95, 98)}


# Gauges with a red end, as BMW's: engine speed (the last 1000 rpm) and the engine's temperatures (the top 15 %).
HOT_TITLES = ("Water", "Oil", "Coolant (OBD)", "Coolant (DME)", "Oil temp. (OBD)")


def red_from(e, lo=None, hi=None):
    """Where a gauge's red end starts on the scale lo..hi (default: the catalogue's), or None."""
    lo = e["lo"] if lo is None else lo
    hi = e["hi"] if hi is None else hi
    if e["unit"] == "rpm":
        return hi - 1000
    if e["title"] in HOT_TITLES:
        return lo + 0.85 * (hi - lo)
    return None


def scale_key(e, rng=None):
    """The scale a gauge's dial or bar draws: its range (rng, or the catalogue's), where its red end starts (or None),
    and the values to number when not round steps."""
    lo, hi = rng if rng else (e["lo"], e["hi"])
    return (lo, hi, red_from(e, lo, hi), SCALE_MARKS.get(e["unit"]))


def entries():
    """Every entry as a dict, in the order the Choose gauges list shows them."""
    out = []
    for t, u, lo, hi, dec, prop, key, invalid, on in CDS:
        out.append(
            dict(source="cds", title=t, unit=u, lo=lo, hi=hi, dec=dec, prop=prop, key=key, invalid=invalid, on=on)
        )
    for t, u, lo, hi, dec, pid, n, f, a, b in OBD:
        out.append(
            dict(
                source="obd",
                title=t,
                unit=u,
                lo=lo,
                hi=hi,
                dec=dec,
                pid=pid,
                n=n,
                f=FORMULAS[f],
                a=a,
                b=b,
                on=t in DEFAULT_PLACE,
            )
        )
    for t, u, lo, hi, dec, did in DID:
        out.append(dict(source="did", title=t, unit=u, lo=lo, hi=hi, dec=dec, did=did, on=t in DEFAULT_PLACE))
    for e in out:
        e["look"] = DEFAULT_LOOK.get(e["title"], 0)
        e["place"] = DEFAULT_PLACE.get(e["title"], 255)
        e["icon"] = next((icon for icon, titles in ICONS.items() if e["title"] in titles), None)
    return out


def cpp(es):
    rows = []
    for e in es:
        src = {"cds": "kCds", "obd": "kObd", "did": "kDid"}[e["source"]]
        prop = f'"{e["prop"]}"' if e["source"] == "cds" else "nullptr"
        key = f'"{e["key"]}"' if e["source"] == "cds" else "nullptr"
        code = e.get("pid", e.get("did", 0))
        rows.append(
            f'    {{{src}, "{e["title"]}", "{e["unit"]}", {e["lo"]!r}, {e["hi"]!r}, {e["dec"]}, {prop}, {key}, '
            f'{e.get("invalid", 0)}, 0x{code:X}, {e.get("n", 0)}, {e.get("f", 0)}, {float(e.get("a", 0))!r}, '
            f'{float(e.get("b", 0))!r}, {"true" if e["on"] else "false"}, {e["look"]}, {e["place"]}}},'
        )
    return (
        "// Copyright (c) 2026 Jiraiya43\n"
        "// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0\n"
        "// GENERATED by hmiapp/gauge_catalogue.py: edit that file, then run it. Every value the Gauges app can show.\n"
        "#ifndef ENTRY_AAD_PLATFORM_GAUGE_CATALOGUE_HPP\n#define ENTRY_AAD_PLATFORM_GAUGE_CATALOGUE_HPP\n\n"
        "namespace evo {\nnamespace catalogue {\n\n"
        "enum Source { kCds, kObd, kDid };\n"
        "// OBD formulas over the answer's data bytes A (and B): 0 A*a, 1 A+b, 2 A*a+b, 3 (256A+B)*a, 4 (256A+B)*a+b.\n"
        "struct Entry {\n    Source source;\n    const char* title;\n    const char* unit;\n    double lo, hi;\n"
        "    int decimals;\n    const char* property;  // CDS\n    const char* key;       // CDS: the number's key\n"
        '    double invalid;        // CDS: >= this is "no value" (0: none)\n'
        "    unsigned code;         // OBD PID or DID\n    unsigned bytes;        // OBD: data bytes\n"
        "    unsigned formula;      // OBD\n    double a, b;           // OBD\n"
        "    bool on;               // shown by default\n"
        "    unsigned look;         // until the owner picks one: hmiapp/gauge_layout.hpp Look\n"
        "    unsigned place;        // its cell on the first dashboard, without a gauges-place file; 255: none\n"
        "};\n\n"
        "// clang-format off\n"
        "const Entry kEntries[] = {\n" + "\n".join(rows) + "\n};\n"
        "// clang-format on\n"
        "const unsigned kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);\n\n"
        "// An OBD answer's data bytes to the value; false when too short.\n"
        "inline bool obdValue(const Entry& e, const unsigned char* d, unsigned n, double* out) {\n"
        "    if (e.source != kObd || n < e.bytes) {\n        return false;\n    }\n"
        "    const double A = d[0], AB = e.bytes >= 2 ? d[0] * 256.0 + d[1] : d[0];\n"
        "    switch (e.formula) {\n"
        "        case 0:\n            *out = A * e.a;\n            return true;\n"
        "        case 1:\n            *out = A + e.b;\n            return true;\n"
        "        case 2:\n            *out = A * e.a + e.b;\n            return true;\n"
        "        case 3:\n            *out = AB * e.a;\n            return true;\n"
        "        case 4:\n            *out = AB * e.a + e.b;\n            return true;\n"
        "        default:\n            return false;\n    }\n}\n\n"
        "}  // namespace catalogue\n}  // namespace evo\n\n#endif  // ENTRY_AAD_PLATFORM_GAUGE_CATALOGUE_HPP\n"
    )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: gauge_catalogue.py <platform/gauge_catalogue.hpp>")
    Path(sys.argv[1]).write_bytes(cpp(entries()).encode("utf-8"))
    es = entries()
    print(f"{len(es)} gauges: {len(CDS)} CDS, {len(OBD)} OBD, {len(DID)} DID; {sum(1 for e in es if e['icon'])} icons")


if __name__ == "__main__":
    main()
