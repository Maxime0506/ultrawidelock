# nRF52833 FreeRTOS Port

This directory owns the standalone FreeRTOS backends for the DWM3001CDK.
Shared protocol code selects them with `WOZ_PORT_FREERTOS`; it must not include
Nordic, Qorvo, or FreeRTOS headers directly.

The implemented foundation now includes:

- `include/woz_freertos_platform.h` defines the BSP-owned clock, cycle-counter,
  busy-wait, and logging hooks.
- `osal/osal_freertos.c` implements deferred work, delayable work, semaphores,
  init hooks, and static-stack threads using only FreeRTOS APIs.
- `thread/openthread_freertos.c` owns one upstream OpenThread instance on a
  static task, serializes public API access with a recursive mutex, and provides
  task and ISR wake paths for tasklets, alarms, and radio work.
- `ble/nimble_sdc_transport.c` implements the static FreeRTOS receive task,
  controller serialization, NimBLE buffer ownership, ACL flattening, and MPSL
  task/ISR wake contract for the SoftDevice Controller HCI boundary.
- `radio/mpsl_freertos.c` owns the highest-priority static MPSL worker and the
  recursive lock shared by BLE, 802.15.4, and MPSL low-priority processing.
- `radio/radio_start_freertos.c` sequences the shared radio: it claims the
  frozen interrupt priorities, initializes MPSL from the board crystal, starts
  the worker on `mpsl_low_priority_process`, links only the peripheral
  GATT/CoC controller features, configures one peripheral link with 251-byte
  Link Layer packets, registers the hardware entropy source, enables the
  controller from a static eight-byte-aligned pool, and publishes the HCI
  transport contract. Every stage failure returns its own negative
  `woz_freertos_radio_stage` instead of continuing.
- `ble/hci_compat/` lets the pinned `hci_internal.c` opcode dispatcher compile
  byte for byte out of the vendor tree. It supplies the Bluetooth Core packet
  layouts, status codes, and OpCode Group Field split the dispatcher expects
  under Zephyr names, plus `woz_freertos_hci_config.h`, whose Kconfig symbols
  must mirror the controller features the sequencer links. Nothing Zephyr is
  compiled or linked, and the vendor file is never patched, so re-pinning it is
  a plain re-fetch. `ble/hci_dispatcher_freertos.c` adapts it to the port's
  radio contract.
- `radio/nrf_802154_irq_freertos.c` maps the driver's IRQ abstraction to CMSIS
  NVIC operations; `nrf_802154_misc_freertos.c` supplies entropy-seeded random
  and die-temperature callouts.
- `peripherals.yml` freezes RTC, TIMER, EGU, vector, and clock ownership before
  the first target link.
- `platform.lock.yml` pins the Qorvo base and the exact OpenThread, nrfxlib,
  SDC opcode-dispatcher, Nordic HAL, and NimBLE revisions.
- `make freertos-port-test` compiles and runs this production backend against a
  recording FreeRTOS test double.

All kernel objects are statically allocated. The dispatch task defaults to a
4096-byte stack and priority `tskIDLE_PRIORITY + 2`; the eventual board build
may override `WOZ_FREERTOS_OSAL_STACK_BYTES`,
`WOZ_FREERTOS_OSAL_QUEUE_DEPTH`, and `WOZ_FREERTOS_OSAL_TASK_PRIORITY`.

## Platform architecture

The 2026-08-10 scope decision accepts a maintained custom radio port. Qorvo
DW3/QM33 SDK v1.1.1 is the pinned DWM3001CDK board, DW3000, FreeRTOS, and nrfx
source base. This port supplies the OpenThread, 802.15.4 coexistence, and
512-byte L2CAP CoC integration. The shipping radio architecture uses linkable
MPSL/SoftDevice Controller, the nRF 802.15.4 service layer, and Apache NimBLE
1.10.0, which keeps standalone MCUboot at address zero. Bundled S113 is only an
interim bring-up option because its fixed application origin breaks that map.
The complete evidence and remaining gates are in
[`internal/dwm3001cdk-freertos-platform-qualification.md`](../../internal/dwm3001cdk-freertos-platform-qualification.md).

Reproduce the Qorvo artifact audit with:

```sh
make freertos-platform-check \
  QORVO_SDK_ARCHIVE=<path-to-DW3_QM33_SDK_1.1.1.zip>
```

Verify an existing NCS workspace against the radio source pins with:

```sh
make freertos-radio-source-check NCS_WORKSPACE=<path-to-ncs-workspace>
```

Verify an Apache NimBLE checkout against the BLE host pin with:

```sh
make freertos-ble-source-check NIMBLE_SOURCE=<path-to-mynewt-nimble>
```

The check verifies that the exact archive contains the board/UWB/FreeRTOS base,
S113 L2CAP CoC API, and radio Timeslot API. It explicitly reports OpenThread,
802.15.4 coexistence, and CoC target integration as port-owned work. Release
still requires all of the following on the DWM3001CDK:

1. BLE peripheral GATT plus a 512-byte L2CAP CoC.
2. OpenThread MTD attachment while BLE remains connected.
3. Safe flash operations and USB CDC under concurrent radio activity.
4. A high-resolution monotonic timer and cycle counter suitable for measuring
   the DW3110 response-arm path.
5. Reproducible licensing, source acquisition, and an ARM GCC build.

MPSL and the SoftDevice Controller are started by `woz_freertos_radio_start()`,
which takes the opcode dispatcher pair. Pass
`woz_freertos_radio_sdc_dispatcher()` for the pinned Nordic dispatcher. The
board layer's remaining radio duty is routing RADIO, RTC0, TIMER0, POWER_CLOCK,
and SWI5_EGU5 to the `woz_freertos_radio_*_isr()` entry points. The transport
intentionally rejects ISO packets because the product requires BLE GATT and
L2CAP CoC, not LE Audio.

Reception must go through the dispatcher's `msg_get` and never through
`sdc_hci_get` directly. The controller's command API is opcode-specific, so the
dispatcher answers most commands itself and stages the resulting Command
Complete or Command Status in its own buffer; a read path that skipped it would
lose every command response.

Verify and exercise the dispatcher against an NCS workspace with:

```sh
make freertos-hci-dispatcher-check NCS_WORKSPACE=<path-to-ncs-workspace>
```

The controller pool is 4096 bytes. The pinned `SDC_MEM_*` macros put the
shipping configuration at 3078 bytes, and `woz_freertos_radio_start()` fails
with `WOZ_FREERTOS_RADIO_STAGE_SDC_MEMORY` rather than overrunning the pool if
a controller update ever needs more. Override `WOZ_FREERTOS_SDC_MEM_BYTES` to
resize it.

The shipping resource map keeps Qorvo's RTC1 FreeRTOS tick, assigns RTC0 and
TIMER0 to MPSL, and assigns RTC2, TIMER1, and EGU0 to nRF 802.15.4. Qorvo's
`app_timer` must be omitted because it currently occupies RTC2; delayable
application work already uses the port OSAL. MPSL uses SWI5/EGU5 for its
low-priority signal, and that IRQ must be configured at a FreeRTOS-callable
priority before it invokes `woz_freertos_mpsl_wake_from_isr()`.

The deprecated Nordic nRF5 Thread SDK is reference material only; its bundled
OpenThread build is not a shipping dependency. The pinned current OpenThread,
MPSL, SoftDevice Controller, and nRF 802.15.4 components must be integrated
through their RTOS-independent APIs without importing Zephyr into the release.
