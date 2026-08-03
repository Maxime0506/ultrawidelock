<!-- generated documentation — edit the source, not this file -->
# `scripts/deadcode-tidy.sh`

deadcode-tidy.sh — run clang-tidy against the REAL firmware build.
scripts/verify.sh already has a clang-tidy gate, but it compiles UNIT_SRCS out
of tests/host/sources.sh with host flags: -std=c11, a macOS sysroot, and the
host fakes. That covers six modules and nothing else. firmware/src and
modules/woz_dfu are in none of it, which security/semgrep-parse-baseline.txt
already records as a gap -- and modules/woz_dfu parses signed update payloads
arriving over Bluetooth.
This runs the same tool against build/<img>/compile_commands.json instead, so
the analysis sees the actual Cortex-M4 target, the real include paths and the
generated autoconf.h, rather than a host approximation of them.
Two things have to be fixed before clang can read a GCC database:
1. GCC-only flags are hard errors to clang ("unknown argument"), not
warnings, so one of them kills the whole file. They are stripped below.
The list is deliberately explicit: a silent catch-all would also swallow
a flag that changes semantics.
2. Zephyr's generated autoconf.h defines negative Kconfig values bare
(#define CONFIG_SYSTEM_WORKQUEUE_PRIORITY -1), which trips
bugprone-macro-parentheses ~1000 times per file. The header filter keeps
findings to this repo's own sources.
