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

The NimBLE transport is executable, but the board layer still has to adapt the
pinned `hci_internal.c` opcode dispatcher away from Zephyr types, initialize
MPSL/SDC, and connect its low-priority signal to
`woz_freertos_nimble_sdc_wake_from_isr()`. The transport intentionally rejects
ISO packets because the product requires BLE GATT and L2CAP CoC, not LE Audio.

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
