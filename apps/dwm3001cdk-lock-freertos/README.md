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

The target image builds:

```sh
make freertos-build \
  NCS_WORKSPACE=<path-to-ncs-workspace> \
  QORVO_SDK_DIR=<path-to-extracted-DW3_QM33_SDK_1.1.1>
```

It is not the product yet. The application is a skeleton that brings the kernel
up and proves the tick runs; what the build is for right now is the link, since
the binding constraint on this port is the 512 KB flash and 128 KB RAM budget
and the Zephyr oracle already overflows 128 KB by 1,752 bytes with the same
feature set. Layers are added one at a time and measured as they land.
`src/radio_not_yet_linked.c` exists only until the radio layer joins the graph
and is deleted with it.

The selected board source base is Qorvo DW3/QM33 SDK v1.1.1;
this port owns the pinned OpenThread MTD, nRF52833 802.15.4
integration, and credential/Matter L2CAP CoC backend layered onto it. The radio
runtime is paired with the pinned MPSL/SoftDevice Controller, nRF 802.15.4,
and Apache NimBLE host source set.

The intended parity scope remains the shipping Matter-over-Thread reader and
the UWB self-test. Existing Zephyr targets, settings, and images are unchanged.
