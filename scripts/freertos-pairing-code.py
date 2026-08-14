#!/usr/bin/env python3
"""Print this FreeRTOS image's Matter pairing code, after proving it is the one.

WHY THIS EXISTS. The Zephyr build runs the same check from its .config at the
end of every `make build`, and refuses to print a code whose verifier does not
re-derive. The FreeRTOS build had no equivalent, because it has no .config --
its commissioning constants are #defines in a header. The gap is not academic:
the header carried a comment naming CHIP's test passcode 20202021 long after the
project had replaced CHIP's values with its own, and nothing anywhere could
notice. It cost a bench session. The board reached Pake2, the commissioner
checked cB against a verifier for a different passcode, and hung up -- which
looks like a firmware fault and is not one.

The device CANNOT do this check itself, and that is by design rather than an
oversight. The augmented SPAKE2+ form exists so a device stores only the
verifier; the passcode is deliberately absent from the image. So the only place
the two can be compared is here, off the device, against the passcode the
oracle's Kconfig records.

Sources, and there is exactly one of each on purpose:

    verifier, salt, iterations, discriminator
        ports/freertos-nrf52833/matter/matter_compat/ultrawidelock_freertos_matter_config.h
    passcode
        apps/dwm3001cdk-lock/Kconfig, ULTRAWIDELOCK_MATTER_SETUP_PASSCODE

The passcode lives in the other product's Kconfig because that is where it has
always lived and duplicating it here would create the second copy this check
exists to catch.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import spake2p_verifier as sp  # noqa: E402

HEADER = os.path.join(
    ROOT, "ports", "freertos-nrf52833", "matter", "matter_compat",
    "ultrawidelock_freertos_matter_config.h")
KCONFIG = os.path.join(ROOT, "apps", "dwm3001cdk-lock", "Kconfig")


def header_defines(path):
    """Pull the four constants out of the C header.

    The verifier is written as adjacent string literals across several lines, so
    the value is whatever sits inside quotes between the macro name and the next
    #define -- concatenated, exactly as the compiler would.
    """
    text = open(path).read()
    out = {}

    m = re.search(r"#define\s+CONFIG_ULTRAWIDELOCK_MATTER_DISCRIMINATOR\s+(0[xX][0-9a-fA-F]+|\d+)", text)
    if m:
        out["discriminator"] = int(m.group(1), 0)

    m = re.search(r"#define\s+CONFIG_ULTRAWIDELOCK_MATTER_SPAKE2P_ITERATIONS\s+(\d+)", text)
    if m:
        out["iterations"] = int(m.group(1))

    for name, key in (("SPAKE2P_VERIFIER", "verifier"), ("SPAKE2P_SALT", "salt")):
        m = re.search(r"#define\s+CONFIG_ULTRAWIDELOCK_MATTER_" + name + r"\b(.*?)(?=#define|\Z)",
                      text, re.S)
        if m:
            out[key] = "".join(re.findall(r'"([^"]*)"', m.group(1)))
    return out


def kconfig_passcode(path):
    text = open(path).read()
    m = re.search(r"config\s+ULTRAWIDELOCK_MATTER_SETUP_PASSCODE\b(.*?)(?=\nconfig\s|\Z)", text, re.S)
    if not m:
        return None
    d = re.search(r"^\s*default\s+(\d+)", m.group(1), re.M)
    return int(d.group(1)) if d else None


def main():
    cfg = header_defines(HEADER)
    passcode = kconfig_passcode(KCONFIG)

    missing = [k for k in ("discriminator", "iterations", "verifier", "salt") if k not in cfg]
    if missing or passcode is None:
        print("  could not read %s from the sources above" %
              ", ".join(missing + ([] if passcode else ["passcode"])), file=sys.stderr)
        return 2

    salt = bytes.fromhex(cfg["salt"])
    derived = sp.derive(passcode, salt, cfg["iterations"]).hex()
    if derived != cfg["verifier"].lower():
        print("  SPAKE2+ VERIFIER DOES NOT MATCH THE PASSCODE.\n"
              "    passcode  %d  (apps/dwm3001cdk-lock/Kconfig)\n"
              "    expected  %s\n"
              "    in header %s\n"
              "  Commissioning will reach Pake2 and the phone will hang up.\n"
              "  Regenerate both together: scripts/spake2p_verifier.py --passcode <p>"
              % (passcode, derived, cfg["verifier"]), file=sys.stderr)
        return 1

    code = sp.manual_code(cfg["discriminator"], passcode)
    print("  matter: pairing code %s  (discriminator 0x%04X, verifier checks out)"
          % (code, cfg["discriminator"]))
    return 0


sys.exit(main())
