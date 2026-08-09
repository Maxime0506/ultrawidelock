# DWM3001CDK Lock, FreeRTOS

This sibling application is the Zephyr-free port of
`apps/dwm3001cdk-lock`. The existing Zephyr application remains the behavioral
and hardware-in-loop oracle throughout the migration.

Implementation has started with the production FreeRTOS OSAL, platform-hook
contract, and serialized upstream OpenThread task runtime in
`ports/freertos-nrf52833`. Run its host gate with:

```sh
make freertos-port-test
```

There is intentionally no target firmware recipe yet. `make freertos-build`
reports that the custom target graph is incomplete rather than producing a
misleading image. The selected board source base is Qorvo DW3/QM33 SDK v1.1.1;
this port owns the pinned OpenThread MTD, nRF52833 802.15.4
integration, and Aliro/Matter L2CAP CoC backend layered onto it. The radio
runtime is paired with the pinned MPSL/SoftDevice Controller, nRF 802.15.4,
and Apache NimBLE host source set.

The intended parity scope remains the shipping Matter-over-Thread reader and
the UWB self-test. Existing Zephyr targets, settings, and images are unchanged.
