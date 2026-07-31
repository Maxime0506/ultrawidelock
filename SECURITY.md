# Security policy

This project is access-control firmware, so security reports get priority over every
other kind of issue. It is also a personal research project with an explicit caveat: it
is provided as is and should not be relied on to secure anything of value.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub:
**Security tab → Report a vulnerability** on this repository. Please do not open a
public issue or PR for a security problem before it is fixed.

A useful report includes the affected file or subsystem, the firmware commit, and a
reproduction (a frame capture, a failing input, or a test case is ideal).

Do not attach raw flight-recorder output to a public report. Serial logs containing
`[FREC]` records and binary `.frc` files include the session's ephemeral URSK. Share them
only through the private reporting channel and delete unneeded copies. A fuzz corpus
exported by `tools/flight_recorder.py` contains received frame bytes only and excludes the
URSK, but review every artifact before publishing it.

Expectations, honestly stated for a single-maintainer project: acknowledgment within
7 days, best-effort fixes with no guaranteed timeline, and credit in the release notes
if you want it. There is no bug bounty.

## Scope

In scope:

- Parsing and session code that consumes attacker-controlled radio input (BLE, UWB, NFC
  paths in `modules/` and `ports/`).
- The credential-to-ranging binding: anything that lets ranging be spoofed, replayed, or
  unbound from the authenticated key (STS derivation, key ladder, M1-M4 handling).
- The ESP32-S3 reader's credential-auth and secure-channel code
  (`ports/esp32/components/aliro_crypto` and `.../aliro_reader`): key schedule, GCM
  channel handling, signature verification, and the credential trust gate.
- Key material handling in this repository's code.

Out of scope:

- Vulnerabilities in upstream components (nRF Connect SDK, the Nordic add-on, Zephyr,
  ESP-IDF, esp-matter, the DW3000 driver): report those upstream, though a heads-up here
  is welcome if this project is affected.
- Physical and hardware-level attacks on the DIY reader assembly.
- Findings that amount to the documented threat model limits (for example, the add-on
  handing the engine a plaintext ranging key is a known design seam, noted in the
  README).
- The ESP32-S3 reader's fallback **dev identity** and its dev-open trust policy. These
  are a documented bench seam, not a security control: with no provisioned trust anchors
  the reader accepts the presented credential and logs a warning. Provision a real
  identity over Matter before treating a build as anything but a bench setup.

## Automated scanning

Every pull request must pass four blocking gates before it can merge: secret scanning
(gitleaks), a structural review of the diff (binaries, mode changes, symlinks, gitlinks,
capture files), SAST (semgrep), and a dependency check for known-vulnerable and
known-malicious packages (osv-scanner, pip-audit). They run in about forty seconds.

Slower analyses — CodeQL, a full-history secret scan, and OpenSSF Scorecard — run weekly and
report to the repository's code-scanning alerts rather than blocking anything.

All of it runs locally too, and is the same code CI runs:

```sh
make security     # the four blocking gates
make verify       # those, plus every other host-runnable CI gate
```

`security/README.md` documents what each gate catches, why ClamAV, DAST and SBOM tooling are
deliberately not used here, and the four known blind spots — including that semgrep cannot
parse 17 macro-heavy files, which are covered by clang-tidy, CodeQL, CBMC and fuzzing instead.

## Supported versions

The latest tagged release and `main` are supported. Older tags are not patched;
fixes land on `main` and ship in the next release.
