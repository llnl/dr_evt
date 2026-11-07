#!/usr/bin/env python3
"""Read the reservation barrier setting from a simulator's generated header."""

import argparse
from pathlib import Path
import re


def reservation_barrier(simulator, config_header=None):
    """Return (enabled, header); reject missing or older, unrecorded settings.

    Discover build-tree and installed headers next to the resolved binary.
    A caller can supply a header for a nonstandard installation layout.
    """
    simulator = Path(simulator).resolve()
    candidates = ([Path(config_header).resolve()] if config_header else
                  [simulator.parent / "dr_evt_config.hpp",
                   simulator.parent.parent / "include" / "dr_evt_config.hpp"])
    header = next((path for path in candidates if path.is_file()), None)
    if header is None:
        raise ValueError("Cannot find dr_evt_config.hpp for {}; supply "
                         "--config-header PATH".format(simulator))
    content = header.read_text()
    macro = "DR_EVT_LEGACY_RESERVATION_BARRIER"
    if re.search(r"^\s*#\s*define\s+" + macro + r"\s+1\s*$",
                 content, re.MULTILINE):
        return True, header
    if re.search(r"^\s*(?:/\*\s*)?#\s*undef\s+" + macro +
                 r"\s*(?:\*/)?\s*$", content, re.MULTILINE):
        return False, header
    raise ValueError("{} does not record {}; rebuild with the updated "
                     "config header".format(header, macro))


def require_reservation_barrier(simulator, enabled, config_header=None):
    """Check a test's required mode before running any simulator cases."""
    observed, header = reservation_barrier(simulator, config_header)
    if observed != enabled:
        raise ValueError("{} records DR_EVT_LEGACY_RESERVATION_BARRIER={}; "
                         "expected {}".format(header, "ON" if observed else "OFF",
                                              "ON" if enabled else "OFF"))
    return header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("simulator", type=Path)
    parser.add_argument("--config-header", type=Path)
    parser.add_argument("--expect", choices=("on", "off"))
    parser.add_argument("--value", action="store_true", help="Print only on or off")
    args = parser.parse_args()
    try:
        enabled, header = reservation_barrier(args.simulator, args.config_header)
        if args.expect:
            require_reservation_barrier(args.simulator, args.expect == "on",
                                        args.config_header)
    except ValueError as error:
        parser.error(str(error))
    if args.value:
        print("on" if enabled else "off")
    else:
        print("DR_EVT_LEGACY_RESERVATION_BARRIER={} ({})".format(
            "ON" if enabled else "OFF", header))


if __name__ == "__main__":
    main()
