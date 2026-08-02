# Installing

The DWM3001CDK is the primary target; the nRF5340 DK and the ESP32-S3, ESP32-C5
and ESP32-C6 apps port the same engine.
No hardware needed until you flash.

## Get the code

```bash
git clone https://github.com/openaliro/openaliro.git
cd openaliro
```

Every command below runs from this directory.

## Fastest install: browser flash for ESP32-S3/C5

Release images can be installed from the
[browser flasher](https://openaliro.github.io/openaliro/flash/) in Chrome or Edge:
connect the board, select S3 or C5, and choose **Install**. This path needs no
ESP-IDF or local flashing tools. Its implementation and local test procedure are
documented in [`web-flasher/README.md`](../web-flasher/README.md).

The page and dual-chip manifest are shipped and dry-checked, but the repository
does not record a successful real WebSerial flash yet. Treat browser flashing as
experimental until that bench check exists. ESP32-S3 is hardware-validated
through the normal flash path; ESP32-C5 has build and release support only.

## The Zephyr boards: DWM3001CDK and nRF5340 DK

Both build out of one fetched workspace, so the setup below is shared.

One command:

```bash
make bootstrap
```

It runs in three phases, so a clone reaches a build without a manual step in
the middle:

1. `make tools-install`, covered [below](#development-tools-macos--linux): the
   host tools every CI gate needs, plus `nrfutil` itself.
2. The NCS v3.3.0 toolchain, via `nrfutil sdk-manager toolchain install`. An
   already-installed toolchain costs a query rather than a re-download, so this
   is safe on every run. It asks nrfutil rather than checking a path, so a
   toolchain in a non-default location is found as long as nrfutil knows about
   it (`nrfutil sdk-manager config show` names the directory it looks in). That
   is the same route the builds use to reach the compiler, so the two can
   never disagree: a toolchain nrfutil cannot see is one the build could not
   have used. If yours is managed some other way, `ALIRO_TOOLCHAIN=env` uses
   whatever is already on `PATH` and skips this phase.
3. NCS v3.3.0 + the Nordic door-lock add-on (~6.5 GB) into `./workspace`, with
   this repo's patches applied on top.

Anything it cannot install stops the run before the 6.5 GB download rather than
after it, which is where that failure used to surface. Knobs:

| Knob | Effect |
|---|---|
| `NO_TOOLS=1` | skip phase 1, the host tools |
| `NO_TOOLCHAIN=1` | skip phase 2, for a host that manages the toolchain itself |
| `ALIRO_WS=/big/disk/ws` | put the workspace somewhere else |
| `ALIRO_SHALLOW=1` | shallow fetch, saves several GB (what CI uses) |
| `ALIRO_TOOLCHAIN=env` | use the toolchain already on `PATH` |
| `HA=1` | Home Assistant patches (pair with `make nrf-build HA=1`) |

CI never runs `make bootstrap`; it calls `scripts/bootstrap.sh` directly, so no
runner has its packages touched, and it builds in a container with the
toolchain already on `PATH` (`ALIRO_TOOLCHAIN=env`), which skips phase 2 too.

Then, for the **DWM3001CDK** — one nRF52833 carrying the reader, the Matter
node and a Thread MTD:

```bash
make build          # -> build/cdk-matter/merged.hex
make flash          # over the on-board J-Link OB
make monitor        # RTT; this board has no UART console
```

`make reader` builds the same source without Matter or Thread, which needs no
commissioner. Details: [`firmware/README.md`](../firmware/README.md).

Or for the **nRF5340 DK**, the only target with NFC:

```bash
make nrf-build
make nrf-flash-erase
make nrf-term
```

That image lands in `./build/nrf5340dk/merged.hex`; the first flash needs the
erase, plain `make nrf-flash` after.

Build options: [configuring.md](configuring.md). Board setup:
[nrf5340-bringup.md](nrf5340-bringup.md).

## Host tests (no toolchain)

Needs only a plain C compiler; runs in a second.

```bash
make test
make coverage
```

## Development tools (macOS / Linux)

Everything above needs a compiler and nothing else. The CI gates need more, and
`make verify` fails on a gate whose tool is absent rather than passing quietly,
because CI runs that gate whatever your machine has. Two commands answer what is
missing and fix it:

```bash
make tools           # what each gate needs, what this machine has, what's missing
make tools-install   # install the missing ones; prints the commands, asks first
```

`make tools` installs nothing and exits nonzero while anything is missing, so
`make tools && make verify` is a safe sequence. `make tools-install` uses
whichever of `brew`, `apt-get`, `dnf`, `pacman` or `zypper` is present, plus
`pipx` for the four tools CI pins to a version; `-y` skips the prompt.

What the gates use, and why:

| Tool | Gate |
|---|---|
| C compiler | `test`, `test-san`, `fuzz` |
| `python3` | `test-web`, `coverage`, `licenses` |
| `shellcheck` | `shellcheck` |
| `actionlint`, `zizmor` | workflow lint + security audit |
| `clang-format`, `clang-tidy` | `format`, `clang-tidy` (CI pins both; a different version disagrees with CI) |
| `node`, emsdk | the WASM twin |
| `doxygen`, `graphviz` | `docs` |
| `llvm-cov` | `coverage` (macOS reaches it through `xcrun`, so Xcode CLT is enough) |
| `reuse` | licence store |
| `cbmc` | the parser memory-safety proof |
| `nrfutil` | no gate — `make bootstrap`/`build`/`flash` only, so its absence is reported and never fails `make tools` |
| python `markdown`, `coverage` | not checked by any gate, but CI installs both: without them the flash-HTML drift check and the python coverage rows silently skip |

Version pins are read from `.github/workflows/` at run time, so bumping a pin in
CI is enough, and nothing here restates it.

Most of these are installed system-wide by the package manager. The four tools
CI pins to a version go through `pipx`, which gives each its own virtualenv and
puts the binary on `PATH`.

The two python packages are the exception: the suites `import` them, so they
have to be visible to the interpreter that runs the gates, and a `pipx`
virtualenv would not be. They go into a repo-local `.venv/` instead (about
16 MB, gitignored). Nothing needs activating and `PATH` is untouched: the test
runners resolve `.venv/bin/python3` themselves when it exists and fall back to
the system `python3` when it does not. That also sidesteps PEP 668, which
otherwise refuses the install outright on Homebrew and most current distros.

A gate whose python package is missing fails the sweep exactly as a missing
binary does. Without `markdown`, `make test` runs 11 fewer checks than CI does,
including the one that catches a stale committed `FLASH.html`.

The sandboxed `git pr` candidate has a narrower, explicit contract because it
has no network, real home directory, user-local tools, or gitignored `.venv`.
Configure its tracked verifier once:

```bash
git config git-pr.verify scripts/verify-isolated.sh
```

That wrapper runs the committed twin self-test and every hermetic gate available
inside the candidate. It names the seven skipped gates in the verdict; CI still
runs them. Run `make verify` directly for the full developer sweep.

## ESP32-S3, ESP32-C5, and ESP32-C6 ports

Both apps expect ESP-IDF at `~/esp/esp-idf` (override: `IDF_EXPORT=`); CI
pins ESP-IDF v5.5.4 and esp-matter in
[`firmware-builds.yml`](../.github/workflows/firmware-builds.yml).

Use `esp32s3` for the hardware-validated target, `esp32c5` for the
build/release-supported target, or `esp32c6` for the direct-SPI BU04 bring-up
target with `ST_NRST` held low. S3 and C6 are hardware-validated; no C5 bench
validation is recorded.

**Reader** (`../ports/esp32/apps/reader`): plain ESP-IDF, no esp-matter.

```bash
make esp-set-target APP=reader TARGET=esp32c6   # or: esp32s3 / esp32c5
make esp-build      APP=reader
make esp-flash      APP=reader
```

**Matter door lock** (`../ports/esp32/apps/matter-lock`): also needs
esp-matter at `~/esp/esp-matter` (override: `ESP_MATTER_PATH=`).

```bash
make esp-set-target APP=matter-lock TARGET=esp32c6   # or: esp32s3 / esp32c5
make esp-go         APP=matter-lock
```

`esp-go` = build + flash + monitor; every flash and monitor target refuses
SEGGER/J-Link ports, so they can never touch an nRF board on the bench.

Running these from the app directory still works — `cd ports/esp32/apps/reader
&& make build` forwards to the same recipes.

Wiring for both chips: [esp32-bringup.md](esp32-bringup.md). Traps:
[esp32-gotchas.md](esp32-gotchas.md).

## Documentation site

```bash
brew install doxygen graphviz   # or: make tools-install
make docs
```

The site lands in `./site`.

## If something fails

See [troubleshooting.md](troubleshooting.md).
