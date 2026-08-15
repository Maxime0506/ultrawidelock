<!--
For anything with a security consequence, do not open a pull request.
Use the private reporting path in SECURITY.md instead.
-->

## What this changes

<!-- One or two sentences. What behavior is different after this merges? -->

## Why

<!-- The problem this solves. Link an issue if there is one. -->

## Verification

Which of these did you actually run? Paste the outcome, not just a checkmark.

- [ ] `make sdk-check`
- [ ] `bash tests/tooling/port_purity_check.sh --self-test`
- [ ] `make check`
- [ ] A target build (say which: `make build`, `make nrf-build`, `make esp-build APP=... TARGET=...`)
- [ ] Hardware (say which board, and what you observed)

<!-- Paste the relevant output here. -->

## Checklist

- [ ] The diff is limited to what the change needs; no drive-by reformatting.
- [ ] No test, purity rule, or ratchet allowlist was weakened to obtain a pass.
- [ ] No private information, credentials, machine-local paths, or personal
      identity in files, output, or commit messages.
- [ ] Vendored trees (`modules/ultrawidelock_dw3000/dwt_uwb_driver/`,
      `modules/ultrawidelock_dfu/src/detools/`) are untouched.
- [ ] If `VERSION` changed, `make sdk-check` proved both source-tree and
      installed consumption.
