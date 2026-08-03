<!-- generated documentation — edit the source, not this file -->
# `activity/scripts/boot-probe.py`

Phase 1 check: the boot shim must never cost the twin its self-test.

Loads activity/dist/index.html three ways and reports both the page's own
#selftest verdict and the data-in-discord attribute the shim stamps on <html>.

Usage: boot-probe.py [dist-dir] [case]   (default dist: ../dist)

<details><summary>Undocumented (5)</summary>

- `find_firefox`
- `Handler`
- `Handler.log_message`
- `Handler.do_POST`
- `Handler.do_GET`

</details>
