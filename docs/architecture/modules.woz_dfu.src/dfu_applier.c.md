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

### `struct apply_ctx`
`modules/woz_dfu/src/dfu_applier.c:82`

Context passed to detools patch applier callbacks holding the primary and staging flash areas.

### `static int wbuf_flush_words(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:125`

Push out every complete word we are holding.

**called by** `mem_write`, `wbuf_flush_all`

### `static int wbuf_flush_all(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:146`

Push out everything, padding a partial tail to a word with erased bits.

**called by** `mem_erase`, `mem_read`, `mem_write`, `patch_stream`  ·  **calls** `wbuf_flush_words`

### `static int mem_write(void *arg_p, uintptr_t dst, void *src_p, size_t size)`
`modules/woz_dfu/src/dfu_applier.c:172`

Write bytes to the primary flash area during patch application by buffering them and flushing
when the buffer reaches word size or on a discontiguous write; returns -1 on failure.

**calls** `wbuf_flush_all`, `wbuf_flush_words`

### `static int mem_read(void *arg_p, void *dst_p, uintptr_t src, size_t size)`
`modules/woz_dfu/src/dfu_applier.c:206`

Read bytes from the primary flash area for a patch segment by flushing any buffered writes first
and reading from the flash area; returns -1 on failure.

**calls** `wbuf_flush_all`

### `static int mem_erase(void *arg_p, uintptr_t addr, size_t size)`
`modules/woz_dfu/src/dfu_applier.c:225`

Erase flash pages for a patch segment by rounding the size up to page boundaries, validating
alignment and bounds, and flushing any buffered writes first; returns -1 on failure.

**calls** `wbuf_flush_all`

### `static int step_set(void *arg_p, int step)`
`modules/woz_dfu/src/dfu_applier.c:282`

Record or clear the patch application step counter in the staging area: step 0 erases the page,
positive steps append a word, and out-of-range or negative steps fail; returns -1 on error.

### `static int step_get(void *arg_p, int *step_p)`
`modules/woz_dfu/src/dfu_applier.c:324`

Retrieve the current step counter from the staging area by reading append-only words until an
erased value is found; returns 0 on success.

**called by** `woz_dfu_apply`

### `static int area_crc32(const struct flash_area *fa, off_t off, size_t len, uint32_t *out)`
`modules/woz_dfu/src/dfu_applier.c:354`

CRC-32 over a run of a flash area.
crc32_ieee_update() is seeded with 0 and chains across calls exactly as
Python's zlib.crc32(data, previous) does, which is what lets the host compute
the same value without either side reimplementing the other's polynomial.

**called by** `woz_dfu_apply`

### `static void staging_consume(struct apply_ctx *c)`
`modules/woz_dfu/src/dfu_applier.c:374`

Erase the whole staging partition, consuming the update.

**called by** `woz_dfu_apply`

### `static int patch_stream(struct apply_ctx *c, const struct woz_dfu_hdr *hdr)`
`modules/woz_dfu/src/dfu_applier.c:385`

Stream a patch from the staging area into detools in chunks, applying it in place to the primary
flash area, and flushing all buffered writes before finalization; returns detools result code.

**called by** `woz_dfu_apply`  ·  **calls** `wbuf_flush_all`

### `static int woz_dfu_apply(void)`
`modules/woz_dfu/src/dfu_applier.c:431`

Apply a staged firmware delta to the primary flash partition during boot. Opens staging and
primary areas, validates header and patch CRC, applies patch in resumable steps, and erases
staging on completion or failure. Returns 0 always; actual success determined by primary image
validity on next boot.

**calls** `area_crc32`, `patch_stream`, `staging_consume`, `step_get`
