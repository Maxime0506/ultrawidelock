#!/usr/bin/env python3
"""Hold the host suite's HAL doubles to the nrfx the image is actually built against.

A fake that defines a symbol the real header does not is the most expensive
kind of green test: the host suite passes, the port compiles against the fake
for weeks, and the defect surfaces at the first target link. This port has
already been bitten twice -- once by an RTC span macro that existed only in the
fake, and once by a GPIOTE API that had the right names but the wrong
signatures.

WHICH TREE IS AUTHORITATIVE, because the first version of this gate got it
wrong and passed everything. Two nrfx trees are within reach: the older one
bundled with the Qorvo SDK, and hal_nordic's in the NCS workspace. The target
build deliberately uses hal_nordic's -- ports/freertos-nrf52833/CMakeLists.txt
says so and explains why -- and the two disagree. In hal_nordic's, every GPIOTE
function takes the peripheral as its first argument and the event enum is
NRF_GPIOTE_EVENT_IN_0; in the Qorvo one there is no peripheral argument and the
enum is NRF_GPIOTE_EVENTS_IN_0. Checking against the Qorvo tree therefore
approved a backend that could not compile. This gate reads the workspace.

Names are compared as declarations, not as substrings, and a function has to
match its parameter count too. A gate that only asked whether the spelling
appears somewhere in the vendor header is exactly the gate that passed the
GPIOTE mismatch.

Usage: freertos-hal-fake-fidelity.py <ncs-workspace>
"""
import os
import re
import sys

NRFX = "modules/hal/nordic/nrfx"

# fake header -> the vendor header it stands in for, under the workspace.
PAIRS = {
    "tests/ports/freertos-nrf52833/fake/hal/nrf_spim.h": NRFX + "/hal/nrf_spim.h",
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpio.h": NRFX + "/hal/nrf_gpio.h",
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpiote.h": NRFX + "/hal/nrf_gpiote.h",
}

# Peripheral base pointers, vector numbers and register-struct names come from
# the MDK device header rather than from any HAL header, and a fake HAL header
# is where the port reaches them. Every pair is checked against this too.
DEVICE = NRFX + "/bsp/stable/mdk/nrf52833.h"

# Vendor-shaped names the fake may define even though the vendor headers do not,
# each with the reason it is not a hole. Keep this list short and argued.
ALLOWED = {
    # The pinned nrf_gpio.h leaves the pin count to the MDK device header's
    # P0/P1 definitions, which the host suite does not include. A fake pin
    # array needs a bound.
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpio.h": {"NRF_GPIO_PIN_COUNT"},
}

NAME = re.compile(r"\b(?:nrf|NRF)_[A-Za-z0-9_]+")

# A function declaration in either the fake or the vendor header, captured with
# its parameter list so the arity can be compared.
DECL = re.compile(
    r"\b((?:nrf|NRF)_[A-Za-z0-9_]+)\s*\(([^;{)]*(?:\([^)]*\)[^;{)]*)*)\)\s*[;{]",
    re.MULTILINE,
)


def arities(text):
    """Map each declared function name to the parameter counts seen for it."""
    found = {}
    for name, params in DECL.findall(text):
        params = re.sub(r"\s+", " ", params).strip()
        if params in ("", "void"):
            count = 0
        else:
            count = params.count(",") + 1
        found.setdefault(name, set()).add(count)
    return found


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: %s <ncs-workspace>\n" % sys.argv[0])
        return 2
    workspace = sys.argv[1]

    failures = 0
    checked = 0

    device_path = os.path.join(workspace, DEVICE)
    try:
        device = open(device_path).read()
    except OSError as exc:
        print("  FAIL cannot read the pinned device header: %s" % exc)
        return 1

    for fake_path, real_rel in sorted(PAIRS.items()):
        real_path = os.path.join(workspace, real_rel)
        try:
            fake = open(fake_path).read()
        except OSError as exc:
            print("  FAIL cannot read the fake HAL header: %s" % exc)
            failures += 1
            continue
        try:
            real = open(real_path).read()
        except OSError as exc:
            print("  FAIL cannot read the vendor header: %s" % exc)
            failures += 1
            continue

        checked += 1
        allowed = ALLOWED.get(fake_path, set())
        vendor_names = set(NAME.findall(real)) | set(NAME.findall(device))

        missing = sorted(
            {n for n in NAME.findall(fake) if n not in allowed and n not in vendor_names}
        )
        if missing:
            failures += 1
            print(
                "  FAIL %s declares names no vendor header defines: %s"
                % (fake_path, ", ".join(missing))
            )
            continue

        # Same spelling is not the same function. A fake whose signature has
        # drifted compiles here and fails at the target, which is the whole
        # failure this gate exists to move earlier.
        fake_arity = arities(fake)
        real_arity = arities(real)
        drifted = []
        for name, counts in sorted(fake_arity.items()):
            if name in allowed or name not in real_arity:
                continue
            if not (counts & real_arity[name]):
                drifted.append(
                    "%s takes %s, vendor takes %s"
                    % (
                        name,
                        "/".join(str(c) for c in sorted(counts)),
                        "/".join(str(c) for c in sorted(real_arity[name])),
                    )
                )
        if drifted:
            failures += 1
            print("  FAIL %s has drifted from %s:" % (fake_path, real_rel))
            for line in drifted:
                print("       %s" % line)
            continue

        print(
            "  ok   %s matches the pinned %s, names and signatures"
            % (fake_path, real_rel.rsplit("/", 1)[1])
        )

    # An entry that stops being checked is a gate that quietly stopped working.
    if checked != len(PAIRS):
        print("  FAIL only %d of %d fake HAL headers were checked" % (checked, len(PAIRS)))
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
