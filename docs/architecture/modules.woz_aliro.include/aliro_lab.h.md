<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_lab.h`

Aliro Lab trace: structured "[ALAB]" lines at transaction phase boundaries,
parsed by tools/aliro_lab.py into a scored walk-up report. Ships in every Aliro
build (CONFIG_WOZ_ALIRO_LAB defaults y, like the sibling uwbdiag trace) but is
OFF at boot and toggled at runtime by the `lab on`/`lab off` console command, so
any firmware profiles on demand with no reflash. Set CONFIG_WOZ_ALIRO_LAB=n to
strip it from a hardened production image.

**used by** [`modules/woz_aliro/src/aliro_lat.c`](../modules.woz_aliro.src/aliro_lat.c.md), [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_aliro/src/aliro_reader.c`](../modules.woz_aliro.src/aliro_reader.c.md)

<details><summary>Undocumented (6)</summary>

- `aliro_lab_set_enabled`
- `aliro_lab_enabled`
- `aliro_lab_ev`
- `aliro_lab_evi`
- `aliro_lab_evi2`
- `aliro_lab_dump`

</details>
