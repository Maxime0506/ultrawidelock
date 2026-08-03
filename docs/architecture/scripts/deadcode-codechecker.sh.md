<!-- generated documentation — edit the source, not this file -->
# `scripts/deadcode-codechecker.sh`

deadcode-codechecker.sh — CodeChecker over the real firmware build.
Same target as deadcode-tidy.sh and the same database, but it runs the Clang
Static Analyzer as well as clang-tidy, keeps results in a store so two runs can
be diffed, and writes an HTML report. Use deadcode-tidy.sh for the quick pass;
use this when you want the cross-translation-unit analyser or a report to read.
It reuses the FILTERED database that deadcode-tidy.sh writes, because the raw
Zephyr one is GCC-flavoured and clang rejects several of its flags outright.
Running the tidy script first is therefore not optional, and this checks.
