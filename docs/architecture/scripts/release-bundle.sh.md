<!-- generated documentation — edit the source, not this file -->
# `scripts/release-bundle.sh`

release-bundle.sh — assemble one publishable firmware bundle.
scripts/release-bundle.sh --target dwm3001cdk --out build/release/... \
--version v0.5.0 --board 'DWM3001CDK (nRF52833)' \
--setup-code 12345678 merged.hex
Options:
--target <slug>          release/<slug>/ supplies the guide and script
--out <dir>              destination, wiped and recreated
--version <text>         the tag, or `git describe` when omitted
--commit <sha>           defaults to HEAD
--board <text>           hardware line in VERSION.txt
--setup-code <code>      Matter setup code, when the build knows it
--commission-note <text> the line printed under it, or instead of it
Writes the firmware given as positional arguments, plus flash.sh, FLASH.md,
FLASH.html and README.txt from release/<slug>/, plus a generated VERSION.txt
and SHA256SUMS.txt. Every bundle gets all of them: this is the one place that
decides what a release zip contains, so the three targets cannot drift.
Exit 0 on a complete bundle, 1 on any failure. There is no partial success —
a bundle missing a file looks identical to a good one once it is a zip.

<details><summary>Undocumented (1)</summary>

- `die`

</details>
