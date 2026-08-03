<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/src/dfu_applier.c`

@file
@brief Applies a staged delta patch onto the primary slot, from inside
MCUboot.
Runs as a SYS_INIT at APPLICATION level. That level is chosen, not
convenient: it is after the flash driver has initialised (POST_KERNEL) and
before MCUboot's own main(), which is the only window in which the primary
slot can be rewritten. It also means NOT ONE LINE of fetched upstream MCUboot
is edited -- the bootloader loads this the same way it loads every other
Zephyr module.
Why this cannot live in the application: the application executes from the
primary slot. Rewriting it would be rewriting the code doing the rewriting.
On a normal boot this costs one word read: the header magic does not match
and the function returns immediately.
SAFETY. Three things stand between a bad patch and a dead lock, and only the
third is load-bearing:
1. the header carries a CRC of itself, written last, so a torn write fails
2. the patch and the from-image are CRC-checked before a byte is erased
3. MCUboot re-verifies the P-256 signature of the RESULT before booting it
(CONFIG_BOOT_VALIDATE_SLOT0=y), and drops to serial recovery if it
fails (CONFIG_BOOT_SERIAL_NO_APPLICATION=y)
So the worst a corrupt or forged patch achieves is destroying the installed
image, which is recoverable, rather than installing code, which is not.
POWER CUTS ARE EXPECTED, not exceptional: this rewrites most of 442 KB and
takes seconds. detools' step counter is what makes that survivable -- see
step_set()/step_get() below.

**depends on** [`modules/woz_dfu/include/woz_dfu.h`](../modules.woz_dfu.include/woz_dfu.h.md)

## API

### `static int wbuf_flush_words(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:122`

Push out every complete word we are holding.

**called by** `mem_write`, `wbuf_flush_all`

### `static int wbuf_flush_all(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:143`

Push out everything, padding a partial tail to a word with erased bits.

**called by** `mem_erase`, `mem_read`, `mem_write`, `patch_stream`  ·  **calls** `wbuf_flush_words`

### `static int area_crc32(const struct flash_area *fa, off_t off, size_t len, uint32_t *out)`
`modules/woz_dfu/src/dfu_applier.c:331`

CRC-32 over a run of a flash area.
crc32_ieee_update() is seeded with 0 and chains across calls exactly as
Python's zlib.crc32(data, previous) does, which is what lets the host compute
the same value without either side reimplementing the other's polynomial.

**called by** `woz_dfu_apply`

### `static void staging_consume(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:351`

Erase the whole staging partition, consuming the update.

**called by** `woz_dfu_apply`

<details><summary>Undocumented (8)</summary>

- `apply_ctx`
- `mem_write`
- `mem_read`
- `mem_erase`
- `step_set`
- `step_get`
- `patch_stream`
- `woz_dfu_apply`

</details>
