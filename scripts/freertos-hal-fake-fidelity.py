#!/usr/bin/env python3
"""Hold the host suite's HAL doubles to the pinned vendor headers.

A fake that defines a symbol the real header does not is the most expensive
kind of green test: the host suite passes, the port compiles against the fake
for weeks, and the defect surfaces at the first target link as an undefined
reference. This port has already been bitten once, by an RTC span macro that
existed only in the fake.

So every vendor-shaped name a fake HAL header declares -- anything spelled
nrf_* or NRF_* -- has to appear in the vendor header it stands in for. Names
the fake owns are spelled fake_* or FAKE_* and are ignored, which is the whole
convention: if it is not vendor-shaped, port code has no business reaching it.

Usage: freertos-hal-fake-fidelity.py <DW3_QM33_SDK_1.1.1.zip>
"""
import re
import sys
import zipfile

# fake header -> the pinned vendor header it must not exceed.
SDK = "SDK_BSP/Nordic/SDK_17_1_0/modules/nrfx/"
PAIRS = {
    "tests/ports/freertos-nrf52833/fake/hal/nrf_spim.h": SDK + "hal/nrf_spim.h",
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpio.h": SDK + "hal/nrf_gpio.h",
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpiote.h": SDK + "hal/nrf_gpiote.h",
}

# Peripheral base pointers and vector numbers -- NRF_SPIM3, GPIOTE_IRQn and
# their kind -- come from the MDK device header rather than from any HAL
# header, and a fake HAL header is where the port reaches them. So every pair
# above is checked against its own header plus this one.
DEVICE = SDK + "mdk/nrf52833.h"

# Vendor-shaped names the fake may define even though the vendor header does
# not, each with the reason it is not a hole. Keep this list short and argued.
ALLOWED = {
    # The pinned nrf_gpio.h leaves the pin count to the MDK device header,
    # which the host suite does not include. A fake pin array needs a bound.
    "tests/ports/freertos-nrf52833/fake/hal/nrf_gpio.h": {"NRF_GPIO_PIN_COUNT"},
}

NAME = re.compile(r"\b(?:nrf|NRF)_[A-Za-z0-9_]+")


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write("usage: %s <DW3_QM33_SDK_1.1.1.zip>\n" % sys.argv[0])
        return 2

    failures = 0
    checked = 0
    with zipfile.ZipFile(sys.argv[1]) as archive:
        try:
            device = archive.read(DEVICE).decode("utf-8", "replace")
        except KeyError:
            print("  FAIL pinned device header is missing: %s" % DEVICE)
            return 1

        for fake_path, real_path in sorted(PAIRS.items()):
            try:
                fake = open(fake_path).read()
            except OSError as exc:
                print("  FAIL cannot read the fake HAL header: %s" % exc)
                failures += 1
                continue
            try:
                real = archive.read(real_path).decode("utf-8", "replace")
            except KeyError:
                print("  FAIL pinned vendor header is missing: %s" % real_path)
                failures += 1
                continue

            allowed = ALLOWED.get(fake_path, set())
            missing = sorted(
                {
                    n
                    for n in NAME.findall(fake)
                    if n not in allowed and n not in real and n not in device
                }
            )
            checked += 1
            if missing:
                failures += 1
                print(
                    "  FAIL %s declares names the vendor header does not: %s"
                    % (fake_path, ", ".join(missing))
                )
            else:
                print(
                    "  ok   %s uses only names the pinned %s defines"
                    % (fake_path, real_path.rsplit("/", 1)[1])
                )

    # An entry that stops being checked is a gate that quietly stopped working.
    if checked != len(PAIRS):
        print("  FAIL only %d of %d fake HAL headers were checked" % (checked, len(PAIRS)))
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
