<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-rtt-elf-check.sh`

Refuse to attach RTT with an ELF the board is not running.
probe-rs reads the _SEGGER_RTT control-block address out of the ELF you hand
it. Hand it one you built but did not flash and it reads an address the board
never populated, then prints nothing -- which looks exactly like a dead board.
That failure has cost real bench time, so `make monitor` checks first.
The predicate is the _SEGGER_RTT address, not the file bytes. Two ELFs that
place the control block identically stream fine no matter how else they
differ, and a byte compare would refuse those too -- false refusals are how a
guard gets routed around.
Exit 1 ONLY on a positive mismatch: two addresses that were both read and
disagree. Anything that leaves the question open (no record of a flash, no
toolchain nm, no symbol) warns and exits 0, because blocking a console on an
indeterminate check is worse than the bug.
Usage: cdk-rtt-elf-check.sh <candidate-elf> <deployed-elf>

## API

### `addr_of()`
`scripts/cdk-rtt-elf-check.sh:44`

nm prints "ADDRESS TYPE NAME"; undefined symbols have no address and so never
reach field 3, which is why an exact $3 match is enough.

<details><summary>Undocumented (1)</summary>

- `warn`

</details>
