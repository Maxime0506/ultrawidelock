/* dfufake — RAM flash partitions, real CRC-32, a reboot recorder and the
 * scripted detools double. See dfufake.h for what is real here and what is not.
 */

#include "dfufake.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <detools.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>

struct dfufake_state dfufake;

/* Backing store. Static rather than malloc'd so a suite that forgets to reset
 * still reads deterministic bytes instead of whatever the allocator left. */
static uint8_t staging_bytes[DFUFAKE_STAGING_SIZE];
static uint8_t primary_bytes[DFUFAKE_PRIMARY_SIZE];

/* dfu_smp_img.c reads the running image through PM_MCUBOOT_PAD_ADDRESS. One
 * page is more than enough for a header plus a TLV block. */
uint8_t dfufake_running_image[DFUFAKE_PAGE_SIZE];

struct dfufake_area dfufake_staging = {
	.id = DFUFAKE_STAGING_ID,
	.buf = staging_bytes,
	.size = DFUFAKE_STAGING_SIZE,
	.registered = true,
};

struct dfufake_area dfufake_primary = {
	.id = DFUFAKE_PRIMARY_ID,
	.buf = primary_bytes,
	.size = DFUFAKE_PRIMARY_SIZE,
	.registered = true,
};

/* The descriptors handed back by flash_area_open(). Kept beside the areas
 * rather than inside them so the pointer the code under test holds stays a
 * plain `const struct flash_area *` exactly as Zephyr's does. */
static struct flash_area staging_fa;
static struct flash_area primary_fa;

static struct dfufake_area *area_of(const struct flash_area *fa)
{
	if (fa == &staging_fa) {
		return &dfufake_staging;
	}
	if (fa == &primary_fa) {
		return &dfufake_primary;
	}
	return NULL;
}

/* Count a knob down. -1 never fires; 0 fires now and stays fired. */
static bool knob_fires(int *counter)
{
	if (*counter < 0) {
		return false;
	}
	if (*counter == 0) {
		return true;
	}
	--*counter;
	return false;
}

void dfufake_blank(struct dfufake_area *area)
{
	memset(area->buf, 0xff, area->size);
}

uint8_t dfufake_peek(const struct dfufake_area *area, size_t off)
{
	return off < area->size ? area->buf[off] : 0;
}

static void area_reset(struct dfufake_area *area)
{
	area->fail_open = false;
	area->write_fail_in = -1;
	area->erase_fail_in = -1;
	area->read_fail_in = -1;
	area->open_calls = 0;
	area->close_calls = 0;
	area->write_calls = 0;
	area->erase_calls = 0;
	area->read_calls = 0;
	area->last_write_off = 0;
	area->last_write_len = 0;
	area->last_erase_off = 0;
	area->last_erase_len = 0;
	dfufake_blank(area);
}

void dfufake_reset(void)
{
	memset(&dfufake, 0, sizeof(dfufake));
	memset(&workfake, 0, sizeof(workfake));
	dfufake.detools_init_ret = 0;
	dfufake.detools_process_ret = 0;
	dfufake.detools_finalize_ret = 0;
	area_reset(&dfufake_staging);
	area_reset(&dfufake_primary);
	memset(dfufake_running_image, 0, sizeof(dfufake_running_image));
	dfufake_script(NULL, 0);
}

/* ---- flash map ------------------------------------------------------------ */

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
	struct dfufake_area *area;
	struct flash_area *descriptor;

	if (id == DFUFAKE_STAGING_ID) {
		area = &dfufake_staging;
		descriptor = &staging_fa;
	} else if (id == DFUFAKE_PRIMARY_ID) {
		area = &dfufake_primary;
		descriptor = &primary_fa;
	} else {
		return -ENOENT;
	}

	area->open_calls++;
	if (area->fail_open) {
		return -ENODEV;
	}

	descriptor->fa_id = (uint8_t)area->id;
	descriptor->fa_off = 0;
	descriptor->fa_size = area->size;
	*fa = descriptor;
	return 0;
}

void flash_area_close(const struct flash_area *fa)
{
	struct dfufake_area *area = area_of(fa);

	if (area != NULL) {
		area->close_calls++;
	}
}

int flash_area_read(const struct flash_area *fa, off_t off, void *dst, size_t len)
{
	struct dfufake_area *area = area_of(fa);

	if (area == NULL) {
		return -EINVAL;
	}
	area->read_calls++;
	if (knob_fires(&area->read_fail_in)) {
		return -EIO;
	}
	if (off < 0 || (size_t)off + len > area->size) {
		return -EINVAL;
	}
	memcpy(dst, area->buf + off, len);
	return 0;
}

int flash_area_write(const struct flash_area *fa, off_t off, const void *src, size_t len)
{
	struct dfufake_area *area = area_of(fa);

	if (area == NULL) {
		return -EINVAL;
	}
	area->write_calls++;
	area->last_write_off = off;
	area->last_write_len = len;
	if (knob_fires(&area->write_fail_in)) {
		return -EIO;
	}
	if (off < 0 || (size_t)off + len > area->size) {
		return -EINVAL;
	}
	/* The nRF driver's word rule, enforced. This is what makes the applier's
	 * write combiner testable rather than decorative. */
	if (((size_t)off % DFUFAKE_WRITE_BLOCK) != 0U || (len % DFUFAKE_WRITE_BLOCK) != 0U) {
		return -EINVAL;
	}
	memcpy(area->buf + off, src, len);
	return 0;
}

int flash_area_erase(const struct flash_area *fa, off_t off, size_t len)
{
	struct dfufake_area *area = area_of(fa);

	if (area == NULL) {
		return -EINVAL;
	}
	area->erase_calls++;
	area->last_erase_off = off;
	area->last_erase_len = len;
	if (knob_fires(&area->erase_fail_in)) {
		return -EIO;
	}
	if (off < 0 || (size_t)off + len > area->size) {
		return -EINVAL;
	}
	/* Pages, not bytes — the reason mem_erase() rounds up at all. */
	if (((size_t)off % DFUFAKE_PAGE_SIZE) != 0U || (len % DFUFAKE_PAGE_SIZE) != 0U) {
		return -EINVAL;
	}
	memset(area->buf + off, 0xff, len);
	return 0;
}

/* ---- CRC-32 (real) -------------------------------------------------------- */

uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len)
{
	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(crc & 1u)));
		}
	}
	return ~crc;
}

uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	return crc32_ieee_update(0u, data, len);
}

/* ---- reboot --------------------------------------------------------------- */

void sys_reboot(int type)
{
	dfufake.reboot_calls++;
	dfufake.last_reboot_type = type;
}

/* ---- deferred work --------------------------------------------------------
 *
 * Same recorder shape as tests/host/shim/drvfake.c, duplicated rather than
 * shared because that file is the UWB radio double and linking it here would
 * drag a fake DW3000 into a flash test. The two are never in one binary.
 *
 * Nothing runs on its own: a suite fires a work item by reaching through
 * workfake.last, which is how the receiver's window expiry and its deferred
 * reboot are driven without a scheduler.
 */
struct workfake_state workfake;

int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	workfake.reschedule_calls++;
	workfake.last = dwork;
	workfake.last_delay = delay;
	return 0;
}

int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay)
{
	workfake.schedule_calls++;
	workfake.last = dwork;
	workfake.last_delay = delay;
	return 0;
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	workfake.cancel_calls++;
	workfake.last = dwork;
	return 0;
}

void k_work_init_delayable(struct k_work_delayable *dwork, k_work_handler_t handler)
{
	dwork->work.handler = handler;
}

int k_work_submit(struct k_work *work)
{
	workfake.submit_calls++;
	workfake.last_submit = work;
	return 0;
}

/* ---- scripted detools ----------------------------------------------------- */

static struct dfufake_op script_ops[DFUFAKE_MAX_OPS];
static size_t script_count;
static size_t script_done;
static int script_last_step_get;

void dfufake_script(const struct dfufake_op *ops, size_t count)
{
	if (count > DFUFAKE_MAX_OPS) {
		count = DFUFAKE_MAX_OPS;
	}
	if (ops != NULL && count > 0U) {
		memcpy(script_ops, ops, count * sizeof(*ops));
	}
	script_count = (ops != NULL) ? count : 0U;
	script_done = 0;
	script_last_step_get = -1;
}

int dfufake_last_step_get(void)
{
	return script_last_step_get;
}

size_t dfufake_ops_done(void)
{
	return script_done;
}

int detools_apply_patch_in_place_init(struct detools_apply_patch_in_place_t *self_p,
				      detools_mem_read_t mem_read, detools_mem_write_t mem_write,
				      detools_mem_erase_t mem_erase, detools_step_set_t step_set,
				      detools_step_get_t step_get, size_t patch_size, void *arg_p)
{
	dfufake.detools_init_calls++;
	dfufake.detools_patch_size = patch_size;
	dfufake.detools_fed = 0;

	self_p->mem_read = mem_read;
	self_p->mem_write = mem_write;
	self_p->mem_erase = mem_erase;
	self_p->step_set = step_set;
	self_p->step_get = step_get;
	self_p->arg_p = arg_p;
	self_p->patch_size = patch_size;
	self_p->replayed = 0;

	return dfufake.detools_init_ret;
}

/* Scratch for DFUFAKE_OP_WRITE / DFUFAKE_OP_READ payloads. One page covers any
 * operation a suite has reason to script. */
static uint8_t op_scratch[DFUFAKE_PAGE_SIZE];

static int replay(struct detools_apply_patch_in_place_t *self_p)
{
	for (; script_done < script_count; script_done++) {
		const struct dfufake_op *op = &script_ops[script_done];
		size_t len = op->len;
		int rc;

		if (len > sizeof(op_scratch)) {
			len = sizeof(op_scratch);
		}

		switch (op->kind) {
		case DFUFAKE_OP_WRITE:
			memset(op_scratch, op->fill, len);
			rc = self_p->mem_write(self_p->arg_p, op->addr, op_scratch, len);
			break;
		case DFUFAKE_OP_READ:
			rc = self_p->mem_read(self_p->arg_p, op_scratch, op->addr, len);
			break;
		case DFUFAKE_OP_ERASE:
			rc = self_p->mem_erase(self_p->arg_p, op->addr, op->len);
			break;
		case DFUFAKE_OP_STEP_SET:
			rc = self_p->step_set(self_p->arg_p, op->step);
			break;
		case DFUFAKE_OP_STEP_GET:
			rc = self_p->step_get(self_p->arg_p, &script_last_step_get);
			break;
		default:
			rc = -1;
			break;
		}

		if (rc != 0) {
			script_done++;
			return DFUFAKE_DETOOLS_CALLBACK_FAILED;
		}
	}
	return 0;
}

int detools_apply_patch_in_place_process(struct detools_apply_patch_in_place_t *self_p,
					 const uint8_t *patch_p, size_t size)
{
	(void)patch_p;
	dfufake.detools_process_calls++;
	dfufake.detools_fed += size;

	if (dfufake.detools_process_ret != 0) {
		return dfufake.detools_process_ret;
	}
	/* The script runs once, on the first chunk: the applier streams the
	 * patch in CONFIG_WOZ_DFU_APPLIER_CHUNK pieces and replaying per chunk
	 * would multiply every scripted operation by the chunk count. */
	if (self_p->replayed) {
		return 0;
	}
	self_p->replayed = 1;
	return replay(self_p);
}

int detools_apply_patch_in_place_finalize(struct detools_apply_patch_in_place_t *self_p)
{
	(void)self_p;
	dfufake.detools_finalize_calls++;
	return dfufake.detools_finalize_ret;
}
