# Make target implementations

The top-level `Makefile` is the public command entry point. It dispatches to the
fragments in `mk/`, which group recipes by board or workflow:

| File | Responsibility |
|---|---|
| `cdk.mk` | DWM3001CDK build, flash, monitor, DFU, and size targets |
| `nrf5340dk.mk` | nRF5340 DK lock and initiator targets |
| `esp32.mk` | ESP32 lock, reader, and initiator targets |
| `anchor.mk` | Two-anchor bench targets |
| `host.mk` | Host tests, coverage, CBMC, and architecture gates |
| `setup.mk` | Tool inspection, workspace bootstrap, and signing-key setup |
| `extras.mk` | Cleanup and remaining cross-cutting utilities |

Invoke targets from the repository root, for example `make build` or
`make check`. The fragments are implementation details and are not intended to
be invoked as standalone Makefiles.
