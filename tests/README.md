# Tests

`tests/` verifies the portable implementation, platform boundaries, and target
integration surfaces.

| Directory | Scope |
|---|---|
| `host/` | Native unit tests, fakes, sanitizers, coverage, and CBMC harnesses |
| `shared/` | Portable tests compiled by more than one environment |
| `ports/` | Framework-port host verification |
| `tooling/` | Purity, drift, source-role, patch, and seam gates |
| `on_target/` | Hardware-backed Zephyr and ESP32 tests |

Run the complete host-side gate with:

```sh
make check
```

Use `make test`, `make test-san`, `make coverage`, `make cbmc`, `make drift`,
`make seam`, or `make purity` for a narrower surface. Hardware tests are kept
separate because they require attached boards and, for end-to-end flows, a
commissioned phone.
