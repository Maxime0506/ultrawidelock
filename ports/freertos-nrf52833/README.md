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
- `ble/nimble_host_freertos.c` starts the whole BLE stack:
  `woz_freertos_nimble_host_start()` brings up the radio, runs
  `nimble_port_init()`, registers the host's sync and reset callbacks, creates
  the host task on a static stack, and schedules host/controller
  synchronization. It returns before the link is usable; wait for
  `woz_freertos_nimble_host_synced()` before advertising.
- `ble/nimble_syscfg/syscfg/syscfg.h` is this product's NimBLE configuration.
  It states only the settings that must differ from upstream and reaches the
  rest with `#include_next`, so the vendor tree is neither copied nor patched;
  put this directory ahead of upstream's on the include path. Every role it
  sets has to agree with the controller features `radio_start_freertos.c`
  links, and `make freertos-ble-source-check` fails if an upstream default ever
  stops contradicting an override.
- `ble/hci_compat/` lets the pinned `hci_internal.c` opcode dispatcher compile
  byte for byte out of the vendor tree. It supplies the Bluetooth Core packet
  layouts, status codes, and OpCode Group Field split the dispatcher expects
  under Zephyr names, plus `woz_freertos_hci_config.h`, whose Kconfig symbols
  must mirror the controller features the sequencer links. Nothing Zephyr is
  compiled or linked, and the vendor file is never patched, so re-pinning it is
  a plain re-fetch. `ble/hci_dispatcher_freertos.c` adapts it to the port's
  radio contract.
- `radio/nrf_802154_clock_freertos.c` implements the 802.15.4 clock platform on
  top of MPSL, which owns the CLOCK peripheral. It uses the source-selecting
  MPSL API because the pinned MPSL deprecates the older one. The low-frequency
  clock is already running by the time anything calls it, since `mpsl_init()`
  waits for the crystal, and stopping it is refused because it carries the
  FreeRTOS tick. With the MPSL-arbitrated service-layer binary only `init` and
  `deinit` are actually reached; the rest is the header's contract and is what
  the open-source scheduler variant would call.
- `radio/nrf_802154_lptimer_freertos.c` implements the service layer's
  low-power timer on RTC2. The service layer works in 64-bit ticks while RTC2
  counts 24 bits, so the counter is extended with an overflow count; reads mask
  the timer interrupt briefly, because the counter and the count are written by
  two contexts and no ordering of those writes is safe to read lock-free.
  Compare channel 0 carries the scheduled timer, channel 1 the timestamper
  synchronization event, and channel 2 the hardware task. A deadline already
  past, or too near for the compare write to be seen, pends the interrupt
  instead of waiting out a 512 second wrap. The hardware task is armed with its
  event enabled and its interrupt off, because PPI carries it to another
  peripheral with no CPU involvement; the channel it publishes to is allocated
  by the 802.15.4 driver core and passed in, so this port never picks one and
  cannot collide with MPSL. Unlike most nRF peripherals the RTC raises no event
  at all until EVTEN says so, so every channel is armed with both bits.
- The high-precision timer platform on TIMER1 is Nordic's own
  `nrf_802154_hp_timer.c`, pinned in `platform.lock.yml` and compiled from the
  vendor tree unmodified. Unlike the low-power timer it depends on nothing but
  nrfx and the Nordic HAL, so it needs no compatibility layer and is used rather
  than reimplemented.
- `thread/ot_compat/` and `thread/ot_kernel_freertos.c` carry Nordic's own
  OpenThread radio platform, `radio_nrf5.c`, which is the whole otPlatRadio
  implementation. It is pinned and compiled unmodified rather than rewritten:
  its Zephyr surface is one semaphore, one intrusive queue, an atomic bit
  array, four byte-order helpers, the logging macros and the assertions, and
  every one of those is implemented here over FreeRTOS. The semaphore picks its
  FreeRTOS entry point by reading IPSR, because the 802.15.4 driver callouts
  signal it from an interrupt; the queue and the atomics mask interrupts for
  the same reason. `woz_freertos_ot_config.h` states the Kconfig selection this
  product resolves to, and `make freertos-ncs-source-check` fails if the
  vendor file ever reaches past what the shim covers.
- `thread/ot_radio_freertos.c` makes the three joins Zephyr would otherwise
  make: `woz_freertos_openthread_radio_start()` brings the platform up, which
  must follow `woz_freertos_radio_start()` because the 802.15.4 driver
  arbitrates the radio through MPSL and must precede
  `woz_freertos_openthread_start()` because the task drains the platform on
  every pass; `woz_freertos_openthread_process_drivers()` is that drain, and
  refuses to reach the platform before it is up; and `otSysEventSignalPending()`
  becomes a FreeRTOS notification, through the interrupt path when IPSR says a
  driver callout is signalling.
- `thread/ot_alarm_freertos.c` is OpenThread's millisecond alarm, carried by the
  port's delayable work rather than a second timer service: the callback only
  records the expiry and wakes the OpenThread task, and the stack's own
  callback runs from that task under the API lock. A deadline already past is
  recorded rather than scheduled, and the flag is cleared before the callback so
  a re-arm from inside it is not swallowed. The microsecond alarm is absent on
  purpose: it exists only under
  `OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE`, which this product leaves at
  upstream's default of zero, and `make freertos-radio-source-check` fails if
  that default ever changes.
- `radio/nrf_802154_irq_freertos.c` maps the driver's IRQ abstraction to CMSIS
  NVIC operations; `nrf_802154_misc_freertos.c` supplies entropy-seeded random
  and die-temperature callouts.
- `peripherals.yml` freezes RTC, TIMER, EGU, vector, and clock ownership before
  the first target link.
- `platform.lock.yml` pins the Qorvo base and the exact OpenThread, nrfxlib,
  SDC opcode-dispatcher, Nordic HAL, and NimBLE revisions.
- `make freertos-port-test` compiles and runs this production backend against a
  recording FreeRTOS test double.

Every kernel object this port owns is statically allocated, but the build is
not heap-free. Apache NimBLE's FreeRTOS porting layer creates its own objects
with the dynamic APIs: one 32-entry event queue, the host, HCI, and GAP
preemption mutexes, the HCI acknowledgement semaphore, and one software timer
per callout. Patching the vendor tree is not an option, so the board build must
set `configSUPPORT_DYNAMIC_ALLOCATION=1` and `configUSE_TIMERS=1` and provide a
heap. Those allocations all happen once inside `nimble_port_init()` and are
never freed, so the heap is a fixed startup cost, not a source of runtime
fragmentation. `make freertos-ble-source-check` asserts the allocation sites so
a NimBLE bump that adds more has to be re-costed. The exact heap size is a
first-link measurement and is not yet established.

The dispatch task defaults to a
4096-byte stack and priority `tskIDLE_PRIORITY + 2`; the eventual board build
may override `WOZ_FREERTOS_OSAL_STACK_BYTES`,
`WOZ_FREERTOS_OSAL_QUEUE_DEPTH`, and `WOZ_FREERTOS_OSAL_TASK_PRIORITY`. The
NimBLE host task defaults to a 4096-byte stack at `tskIDLE_PRIORITY + 1`,
below the HCI receive pump, and is overridden with
`WOZ_FREERTOS_NIMBLE_HOST_STACK_BYTES` and
`WOZ_FREERTOS_NIMBLE_HOST_TASK_PRIORITY`.

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

The BLE configuration is peripheral-only on purpose: no central, no observer,
no extended advertising, one connection, one credit-based L2CAP channel, and
LE Secure Connections with the legacy pairing fallback compiled out. Bonding
stays off until the port has a settings backend to persist keys into. Privacy
has no build-time switch, so product code must pass only
`BLE_OWN_ADDR_PUBLIC` or `BLE_OWN_ADDR_RANDOM`; the controller does not link
`sdc_support_le_privacy` and has no resolving list, so requesting an
`BLE_OWN_ADDR_RPA_*` type fails at runtime rather than at build time.

The transport copies each HCI event into one fixed `BLE_TRANSPORT_EVT_SIZE`
pool block. Upstream's 70-byte default is exactly the Command Complete for Read
Local Supported Commands, the largest event a legacy-only peripheral receives,
so it is correct here but has no margin. The transport refuses a longer event
instead of copying it, because that bound is a property of the linked feature
set rather than of the copy.

Reception must go through the dispatcher's `msg_get` and never through
`sdc_hci_get` directly. The controller's command API is opcode-specific, so the
dispatcher answers most commands itself and stages the resulting Command
Complete or Command Status in its own buffer; a read path that skipped it would
lose every command response.

Build and run the pinned NCS sources against an NCS workspace with:

```sh
make freertos-ncs-source-check NCS_WORKSPACE=<path-to-ncs-workspace>
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
