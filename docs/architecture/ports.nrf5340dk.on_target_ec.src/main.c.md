<!-- generated documentation — edit the source, not this file -->
# `ports/nrf5340dk/on_target_ec/src/main.c`

nRF5340DK on-target self-test for the Aliro device (initiator) EC path: a
minimal Zephyr application that brings up the real PSA backend (nrf_security on
CryptoCell), runs the same credential-auth crypto suite the host tests run, and
prints PASS or FAIL to the DK console. It exists because the host suite proves
the maths against a software curve only; this proves the same vectors on the
silicon that will ship, and it caught a PSA import failure that no host run
could see. Crypto only: no BLE, no UWB, no iPhone.

<details><summary>Undocumented (1)</summary>

- `main`

</details>
