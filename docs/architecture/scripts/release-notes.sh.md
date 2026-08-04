<!-- generated documentation — edit the source, not this file -->
# `scripts/release-notes.sh`

release-notes.sh — render the GitHub release body from release/NOTES.md.in.
scripts/release-notes.sh v0.5.0                     # preview it
scripts/release-notes.sh v0.5.0 out/SHA256SUMS.txt  # what CI publishes
Placeholders: @TAG@ @REPO@ @PAGES@ @CHANGELOG@ @SUMS@
Env: REPO=owner/name (default openaliro/openaliro)
These notes are also the release email: GitHub renders them into the
notification it sends watchers, so the checksums stay inside a <details> and
nothing load-bearing sits below the fold.
