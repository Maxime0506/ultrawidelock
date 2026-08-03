<!-- generated documentation — edit the source, not this file -->
# `ports/nrf5340dk/on_target_ec/src/main.c`

nRF5340DK on-target self-test for the Aliro device (initiator) EC path: a
minimal Zephyr application that brings up the real PSA backend (nrf_security on
CryptoCell), runs the same credential-auth crypto suite the host tests run, and
prints PASS or FAIL to the DK console. It exists because the host suite proves
the maths against a software curve only; this proves the same vectors on the
silicon that will ship, and it caught a PSA import failure that no host run
could see. Crypto only: no BLE, no UWB, no iPhone.

## API

### `int main(void)`
`ports/nrf5340dk/on_target_ec/src/main.c:30`

Run the on-target Aliro device EC self-test: initialize PSA, run the device self-test, and print
PASS or FAIL with return code.
