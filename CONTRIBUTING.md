# Contributing

Issues and PRs are welcome. This is a single-maintainer project, so response times are
best-effort; small, focused changes land fastest. Taking part means following the
[code of conduct](CODE_OF_CONDUCT.md).

## Building and testing

Prerequisites and build steps are in the [README](README.md#build-it). The short
version: `make dfu-key` and `make bootstrap` once, then `make build` for the DWM3001CDK
(bare targets mean that board) or `make nrf-build` for the nRF5340 DK. Bootstrap installs
the host tools, then the NCS toolchain, then fetches the SDK, skipping whatever is already
there; `dfu-key` writes this checkout's image-signing key, which every DWM3001CDK build
needs. Host-side tests need no toolchain or hardware at all: `make test` runs the KAT
suite, `make coverage` the coverage report.

For the ESP32-S3 ports, build from the app directory (`ports/esp32/apps/reader` or
`ports/esp32/apps/matter-lock`) with ESP-IDF on your `PATH`; see [`ports/README.md`](ports/README.md).
Their host tests run without ESP-IDF: `make test-port`.

Every PR must pass the CI gates that run automatically: host tests + coverage floor,
sanitizers, fuzz, CBMC, the ESP32 port suite, clang-format and clang-tidy over
`modules/`, shellcheck, patch drift, workflow lint, and compile checks of all three
firmware targets (DWM3001CDK, nRF5340 DK, ESP32). `make verify` runs the host-side
gates locally in one go, the ESP32 port suite included. CBMC is the one exception: it
is half the sweep's runtime on its own, so it is opt-in as `WITH_CBMC=1 make verify`,
and the summary says on every run that it did not run. A gate whose tool is not installed
fails the sweep rather than passing quietly, because CI runs it either way: `make tools`
lists what each gate needs and what this machine is missing, and `make tools-install`
installs it after showing you the commands ([`docs/set-up.md`](docs/set-up.md)).

`make verify` holds one row per CI job, and `make test-verify` is what keeps that true.
If your change adds a job to `.github/workflows/`, those tests fail until the job is
accounted for: either name the local gate that reproduces it, or write down why it
cannot have one (the firmware builds cannot: they need the full toolchains). That is
the whole point, so please do not work around it. The same tests fail if a gate is
deleted while a job still expects it, or if a gate needs a tool `make tools` cannot
install.

## Ground rules

- **Never edit fetched upstream.** `workspace/` is fetched pristine and is layered onto,
  not modified: diffs go in `ports/nrf5340dk/patches/`, configuration in
  `ports/nrf5340dk/overlays/`, new code in `modules/` or `ports/`. A PR that edits fetched
  trees will be asked to rework.
- **Keep changes surgical.** Match the surrounding style; do not reformat or restructure
  code unrelated to your change.
- **Tests come with the change.** New parsing, session, or crypto code in
  `modules/woz_uwb/` needs host KAT coverage in `tests/host/` (CI enforces a coverage
  floor); the same rule applies to `ports/esp32/components/` and
  `ports/esp32/test/`.
- **Keep the shared engine target-neutral.** `modules/woz_uwb/` is compiled by both the
  nRF5340 and the ESP32-S3 builds. A tweak that only suits one target belongs behind
  `#if defined(ESP_PLATFORM)`, and `make test` must still pass (the host shim compiles
  the file without it).
- **Hardware claims need hardware.** If a change affects on-air behavior, note in the PR
  which board and phone it was validated on and what was not; see
  [`docs/hardware-validation.md`](docs/hardware-validation.md).

## Documentation

`docs/` is committed, so reading and editing it needs nothing installed. Two rules:

- **Pages stamped `<!-- generated documentation -->` are written from the source.** Edit
  the doc comment on the declaration, not the page. That covers `docs/README.md`,
  `docs/ARCHITECTURE.md` and everything under `docs/architecture/`. The remaining pages
  are hand-written and yours to edit directly.
- **`make docs` renders the site into `site/`** and needs `doxygen` and `graphviz`. It
  reports that the page generator is not configured and builds the API reference over the
  committed tree; that is the expected result and not a failure. Regenerating the
  stamped pages is a maintainer step, described in [`docs/RELEASING.md`](docs/RELEASING.md).

## Licensing

Project-original code is ISC (see [LICENSE](LICENSE)). The tree is mixed-license, and
because of the Qorvo driver's terms the repository as a whole is source-available rather
than open source in the OSI sense; [LICENSE](LICENSE) maps each vendored component to its
license and says where the text lives.

Where a file carries an `SPDX-License-Identifier` header, that header is authoritative.
Most of the tree predates the convention and has none, so absence of a header means the
component mapping in [LICENSE](LICENSE) applies, not that the file is unlicensed. CI gates
the license store rather than full REUSE compliance: every identifier a file claims must
have its text in [`LICENSES/`](LICENSES), and every text there must be claimed by
something. Per-file header coverage is reported, not enforced.

Contributions to project-original files are accepted under ISC; new project-original files
should carry `SPDX-License-Identifier: ISC`. Do not copy code in from external projects
unless its license is compatible, its header says where it came from, and its text is
added to [`LICENSES/`](LICENSES).

## Security issues

Do not open public issues for vulnerabilities; see [SECURITY.md](SECURITY.md).
