<!-- generated documentation — edit the source, not this file -->
# `scripts/cdk-size.py`

cdk-size.py — what the DWM3001CDK image costs, as a machine-readable record.

The CDK image is the constrained one: reader, DW3110 ranging, a hand-written
Matter node and an OpenThread MTD on a 128 KB-RAM nRF52833. RAM is the scarcest
resource in the project and the one most easily spent by accident, so this reads
an already-built tree and says how much of each region is left.

    scripts/cdk-size.py --build build/cdk-matter
    scripts/cdk-size.py --build build/cdk-matter --json out.json
    make cdk-size

FREE BYTES, NOT PERCENTAGES. At 97% of 128 KB a 644 B regression moves the
percentage by half a point and reads as noise; "3,891 B free" does not.

NO TOOLCHAIN REQUIRED for the headline numbers. The ELF is parsed here rather
than shelled out to arm-none-eabi-size/nm, for the reason scripts/security-fw.sh
gives about objcopy: those binaries live inside the NCS toolchain, so requiring
them turns "the gate ran" into "the gate ran if you had bootstrapped". Zephyr's
own ram_report/rom_report DO need the toolchain and are folded in when they are
available (--reports), as a cross-check rather than as the only source.

REGION SIZES COME FROM THE BUILD. Origin and length are read out of the linker's
own Memory Configuration block in zephyr.map, and the partition layout out of
partitions.yml, so a pm_static change moves these numbers instead of silently
invalidating a datasheet constant hardcoded here.

Exit 0 on a report, 2 when there is no build tree to measure. Measuring is all
this does: scripts/cdk-size-compare.py is what fails a build.

## API

### `class Elf`
`scripts/cdk-size.py:55`

Just enough ELF32 to account for what the linker placed where.

**called by** `build_report`

#### `Elf.alloc_sections(self)`
`scripts/cdk-size.py:99`

Every section the loader gives address space to, with its size.

**called by** `account_sections`

#### `Elf.symbols(self)`
`scripts/cdk-size.py:103`

(name, addr, size) for sized code and data symbols.

**called by** `build_report`  ·  **calls** `Elf._cstr`

### `load_address(elf, vaddr)`
`scripts/cdk-size.py:256`

Where a run-time address is stored, following the segment that maps it.

**called by** `account_sections`

### `account_sections(elf, regions)`
`scripts/cdk-size.py:266`

The `arm-none-eabi-size -A` view: sum of allocated sections, by address.

Returned alongside the load-image total, so the gap against the linker's own
figure is ACCOUNTED FOR rather than merely reported. A section placed
`> RAM AT> FLASH` shows up in RAM here and its initialiser image is charged
to FLASH; whatever is left over after both is alignment padding.

**called by** `build_report`  ·  **calls** `Elf.alloc_sections`, `in_region`, `load_address`

### `relative_to_root(path, root)`
`scripts/cdk-size.py:430`

A build path that can be committed: never above the repo, never absolute.

**called by** `build_report`

### `config_key(config)`
`scripts/cdk-size.py:644`

A readable name for an overlay set: the thing that makes builds differ.

Defined here, next to the measurement, because it names a property OF a
report; the comparator and the baseline writer both import it from here so
a build cannot be recorded under one name and looked up under another.

**called by** `markdown`

### `markdown(report)`
`scripts/cdk-size.py:666`

A standalone table, for a build that measures but does not compare.

**called by** `main`  ·  **calls** `config_key`, `fmt`

<details><summary>Undocumented (18)</summary>

- `Elf.__init__`
- `Elf._cstr`
- `normalise_symbol`
- `read_memory_config`
- `in_region`
- `account_segments`
- `claim`
- `read_partitions`
- `read_cmake_cache`
- `read_kconfig`
- `read_version_header`
- `collect_config`
- `git_commit`
- `run_reports`
- `build_report`
- `fmt`
- `print_table`
- `main`

</details>
