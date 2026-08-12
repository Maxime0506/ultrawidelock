# Porting UltraWideLock

This guide is the shortest path to a new board or chipset. It assumes the board
uses Zephyr or ESP-IDF. A new operating system also needs the contracts listed
under [New operating systems](#new-operating-systems).

## Choose the integration path

| Target | Start from | Keep |
|---|---|---|
| Zephyr reader | `apps/dwm3001cdk-lock/` or `apps/nrf5340dk-lock/` | Module selection and `ports/zephyr/` |
| Zephyr initiator | `examples/zephyr/nrf5340dk-initiator/` | Device role manifests and `ports/zephyr/` |
| ESP-IDF reader | `examples/esp32/reader/` | `EXTRA_COMPONENT_DIRS` and `aliro_reader` requirements |
| ESP-IDF initiator | `examples/esp32/initiator/` | `EXTRA_COMPONENT_DIRS` and `aliro_device` requirements |

Keep the new application small. Board pins, devicetree, partitions, and feature
selection belong in the application. Portable protocol decisions belong in
`modules/`.

## C include surfaces

Application code includes only the stable role API it consumes:

```c
#include <ultrawidelock/reader.h>
#include <ultrawidelock/uwb.h>
```

An initiator uses `<ultrawidelock/device.h>`. A host tool that only handles protocol
data uses `<ultrawidelock/tlv.h>`. The all-in-one `<ultrawidelock/ultrawidelock.h>` is an
installed-package convenience and should not be used to pull unused firmware
roles into a target build.

Port implementations include the complete chipset contract:

```c
#include <ultrawidelock/woz_hal.h>
```

In framework builds the role headers are available in the same `ultrawidelock/`
namespace. Their canonical declarations live under the owning module's
`include/ultrawidelock/` directory. Use that spelling in applications, modules,
ports, and tests; the removed flat role-header names are rejected by the SDK
gate.

## Five chipset seams

| Seam | Contract header | Existing backends |
|---|---|---|
| DW3000 GPIO, reset, and IRQ | `dw3000_hw.h` | `ports/zephyr/dw3000/`, `ports/esp32/components/ultrawidelock_uwb/port/` |
| DW3000 SPI | `dw3000_spi.h` | `ports/zephyr/dw3000/`, `ports/esp32/components/ultrawidelock_uwb/port/` |
| Reader BLE GATT and L2CAP | `aliro_ble.h` | `ports/zephyr/ble/`, `ports/esp32/components/aliro_ble/` |
| Initiator BLE central | `aliro_ble_central.h` | `ports/zephyr/ble/`, `ports/esp32/components/aliro_ble_central/` |
| Reader credential store | `aliro_prov.h` | `ports/zephyr/store/`, `ports/esp32/components/aliro_reader/` |

Implement every function in `dw3000_hw.h`, `dw3000_spi.h`, and `aliro_ble.h`
for a reader. Implement `aliro_ble_central_start()` and
`aliro_ble_central_send()` for an initiator. The parser and salt helpers in the
same header are portable code and must not be copied into a port. Implement only
`aliro_prov_load()`, `aliro_prov_store()`, and `aliro_prov_erase()` from
`aliro_prov.h`; serialization and trust policy remain portable.

If an existing backend already works for the chipset, do not fork it. Supply
pins and bus instances through the framework's board configuration. Add a new
backend file only when the hardware API is genuinely different.

## Build integration

Zephyr applications add the required `modules/<name>` directories and
`ports/zephyr/` to `ZEPHYR_EXTRA_MODULES`. The existing applications show the
smallest known-good module set for each role.

ESP-IDF applications add the component root once:

```cmake
set(EXTRA_COMPONENT_DIRS "${ULTRAWIDELOCK_ROOT}/ports/esp32/components")
```

Then name the role in the consuming component's `REQUIRES` or `PRIV_REQUIRES`.
Use `aliro_reader` for a reader and `aliro_device` plus `aliro_ble_central` for
an initiator. ESP-IDF discovers every component but compiles only required
components, so an unreferenced backend is not verified.

Shared source selection comes from `modules/*/roles/*.list`. Add a source to one
role manifest and consume that role through the existing CMake helper. Do not
paste the source path into a second build definition.

## New UWB chipsets

The two `dw3000_*` seams above cover a new board carrying a DW3000-family
chip. A different chipset replaces the engine, not the seams. The contract is
`<ultrawidelock/uwb.h>`: bind a URSK, start and stop an Aliro session from the
negotiated parameters, report ranges with integrity evidence. Everything above
that header is chip-agnostic and reused as is — the FiRa session state, DS-TWR
math, CCC key schedule and MAC framing, the Aliro M1-M4 adapter, and the apps.

What a new chipset supplies:

1. An implementation of every function in `<ultrawidelock/uwb.h>`, in its own
   module directory beside `modules/ultrawidelock_dw3000/`.
2. Role manifests for its source sets, replacing the DW3000-shaped roles
   (`base_driver`, `ccc_engine`, `responder_driver`, `diag_cir`,
   `flight_recorder`); the chip-agnostic roles are consumed unchanged.
3. Wiring seams in `ports/` only if the chip is a raw transceiver. A chip that
   runs its own FiRa stack and speaks UCI needs no local ranging engine at
   all: the contract implementation translates sessions to UCI commands.

`tests/tooling/uwb_engine_scope_check.sh` (`make scope`) enforces the
boundary: the Qorvo radio API is named only inside the DW3000 engine's file
set, so chip-agnostic code cannot silently couple to one vendor's silicon.
Keep a new chipset's radio API inside its own engine the same way.

## New operating systems

The five HAL seams are not the complete contract for a new operating system.
Before chipset work, implement the platform services declared by:

- `woz_osal.h`
- `woz_flash.h`
- `woz_log.h`
- `woz_port.h`

Put that backend under one new `ports/<os>/` tree. Keep conditional operating
system code out of `modules/`. Add a host compile or fake for each new contract
before relying on a hardware build.

## Verification

Run the boundary gates before a target build:

```sh
bash tests/tooling/port_purity_check.sh --self-test
make check
```

Then build the closest existing role and the new target. For ESP-IDF also run
`bash tests/ports/esp32/verify_port.sh`. A new gate rule is complete only after
a temporary violation proves it fails and restoring the valid tree proves it
passes.
