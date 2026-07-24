# On-target Aliro device EC self-test

Runs the initiator (device) self-test on an ESP32-S3 against the **real** PSA
P-256 backend (`aliro_prim_psa.c`), not the host fake curve. It proves the
credential-auth crypto path works on silicon: ECDH, both ECDSA transcripts, the
key schedule, the AES-GCM channels, the fast-phase cryptogram, and the full
reader<->device URSK loopback. No BLE, UWB, or iPhone needed; any ESP32-S3 board.

This closes the one gap the host suite cannot: on the machine the loopback runs
against a self-consistent fake curve, so a PASS proves wiring, not real-crypto
behaviour. Here it runs against the same PSA/mbedTLS curve the reader firmware
ships with.

## Build + flash

    . ~/esp/esp-idf/export.sh
    cd ports/esp32/test/on_target_ec
    idf.py set-target esp32s3
    idf.py -p <PORT> flash monitor        # e.g. -p /dev/tty.usbmodem2101

`idf.py build` alone (no board) already proves it compiles + links against real
PSA EC. Ctrl-] exits the monitor.

## Expected serial output

    === on-target Aliro device EC self-test (real PSA P-256) ===
    == aliro_device: initiator-side codec + crypto ==
    ...
      ok   loopback: reader URSK == device URSK

    RESULT: PASS
    ON-TARGET RESULT: PASS

A `FAIL` line names the failing check (e.g. `FAIL loopback: reader URSK == device
URSK`). Any `ON-TARGET RESULT: FAIL` means real-crypto behaviour diverged from
the host fake curve — worth investigating before trusting the EC path on hardware.
