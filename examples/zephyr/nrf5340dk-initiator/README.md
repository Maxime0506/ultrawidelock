# nRF5340 DK initiator example

This application runs the credential user-device role on an nRF5340 DK. It scans for
an credential reader, negotiates the BLE transport, and drives the initiator-side
UWB exchange. It is a bench peer, not a lock.

Prepare the NCS workspace and build:

```sh
make bootstrap
make nrf-init-build
```

Flash both application and network cores with:

```sh
make nrf-init-flash
```

The output is stored under `build/nrf5340dk-initiator/`. The target uses the
same portable UWB modules and Zephyr port as the product applications.
