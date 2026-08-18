# Tests

`tests/` verifies the portable implementation, platform boundaries, and target
integration surfaces.

| Directory | Scope |
|---|---|
| `host/` | Native unit tests, fakes, sanitizers, coverage, and CBMC harnesses |
| `shared/` | Portable tests compiled by more than one environment |
| `ports/` | Framework-port host verification |
| `tooling/` | Purity, drift, source-role, patch, seam, and static-analysis gates |
| `sdk/` | Installed CMake package and external C consumer |
| `on_target/` | Hardware-backed Zephyr and ESP32 tests |

Run the complete host-side gate with:

```sh
make check
```

Use `make test`, `make sdk-check`, `make test-san`, `make coverage`, `make cbmc`,
`make drift`, `make seam`, `make purity`, or `make lint` for a narrower surface.
Hardware tests are kept separate because they require attached boards and, for
end-to-end flows, a commissioned phone.

## Static analysis

Four passes read the portable tree, each catching what the others cannot:

| Command | Tool | Reads | Cost |
|---|---|---|---|
| `make lint` | cppcheck | patterns, every path whether a test reaches it or not | seconds |
| `make test-san` | ASan + UBSan | memory behaviour, but only on the paths a suite exercises | ~1 min |
| `make cbmc` | CBMC | the wire parsers exhaustively, within bounds | minutes |
| `make sca` | Clang Static Analyzer | values followed across functions and branches | seconds |

`make lint` runs inside `make check` and CI. `make sca` does not: CodeChecker is
a Python package rather than a one-line install, so requiring it would fail a
clean checkout for a reason unrelated to the change under test. Run it before a
release, or when touching parsing and session code:

```sh
python3 -m venv .venv-sca
.venv-sca/bin/pip install codechecker
CODECHECKER=.venv-sca/bin/CodeChecker make sca
```

Both gates cover `modules/` and `include/` only. `ports/` and `apps/` cannot be
parsed without Zephyr and ESP-IDF expanding their macros, so pointing either
tool at them reports missing SDK headers rather than defects. Each gate takes a
`--self-test` flag that plants a bug of the class it exists to catch.
