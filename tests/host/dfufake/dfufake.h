/* dfufake — test-side control/inspection API for the fake Zephyr flash-map,
 * CRC, reboot and detools surfaces that modules/woz_dfu builds against.
 *
 * The flash is real where the code under test depends on it: RAM-backed
 * partitions, erase writes 0xff, and BOTH nRF alignment rules enforced (word
 * writes, page erases) -- the entire reason the applier's write combiner and
 * erase ROUND_UP exist, so a fake accepting anything would hide that bug.
 * CRC-32 is the real IEEE polynomial. detools is NOT real (a scripted double
 * driving the applier's five callbacks -- never the patch format), and PSA is
 * a knob (tests/host/psafake/psafake.h).
 */
#ifndef WOZ_DFUFAKE_H
#define WOZ_DFUFAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Partition geometry, matching firmware/pm_static.yml so the size limits the
 * code checks against are the ones it will meet on the board. */
#define DFUFAKE_STAGING_ID   3
#define DFUFAKE_STAGING_SIZE 0xa000u  /* 40,960 B */
#define DFUFAKE_PRIMARY_ID   1
#define DFUFAKE_PRIMARY_SIZE 0x6a000u /* 434,176 B */

#define DFUFAKE_WRITE_BLOCK 4u
#define DFUFAKE_PAGE_SIZE   4096u

/**
 * One RAM-backed flash partition plus the failure knobs a suite injects into
 * it. Every `*_fail_in` counts calls down: -1 never fails, 0 fails this call
 * and every call after it, N > 0 lets N calls through and then fails.
 */
struct dfufake_area {
	int id;
	uint8_t *buf;
	size_t size;
	bool registered;

	/* knobs */
	bool fail_open;
	int write_fail_in;
	int erase_fail_in;
	int read_fail_in;

	/* recorded */
	unsigned open_calls, close_calls, write_calls, erase_calls, read_calls;
	off_t last_write_off;
	size_t last_write_len;
	off_t last_erase_off;
	size_t last_erase_len;
};

/**
 * Everything the suites drive or inspect that is not per-area: the reboot
 * recorder and the scripted detools double.
 */
struct dfufake_state {
	/* sys_reboot() recorder — the receiver must reply before it reboots,
	 * so a suite checks the reply bytes and then that this fired. */
	unsigned reboot_calls;
	int last_reboot_type;

	/* detools double: statuses returned by each entry point. */
	int detools_init_ret;
	int detools_process_ret;
	int detools_finalize_ret;
	unsigned detools_init_calls, detools_process_calls, detools_finalize_calls;
	size_t detools_patch_size; /* patch_size argument seen at init */
	size_t detools_fed;        /* total patch bytes handed to _process */
};

extern struct dfufake_state dfufake;
extern struct dfufake_area dfufake_staging;
extern struct dfufake_area dfufake_primary;

/**
 * The bytes dfu_smp_img.c reads as the RUNNING image. On target that is the
 * primary slot mapped at PM_MCUBOOT_PAD_ADDRESS and read straight through a
 * pointer; here pm_config.h points that macro at this buffer, so a suite lays
 * out a header and a TLV block and then asks what the module reports.
 */
extern uint8_t dfufake_running_image[];

/** @brief Zero every recording, restore every knob, erase both partitions. */
void dfufake_reset(void);

/** @brief Fill @p area with 0xff without going through flash_area_erase(). */
void dfufake_blank(struct dfufake_area *area);

/** @brief Byte at @p off of @p area, for assertions about what landed. */
uint8_t dfufake_peek(const struct dfufake_area *area, size_t off);

/* ---- the scripted detools double ------------------------------------------
 *
 * A suite builds a list of these and detools_apply_patch_in_place_process()
 * replays it against the applier's callbacks, in order, on its FIRST call.
 * Anything the callbacks reject stops the replay and _process() returns
 * DFUFAKE_DETOOLS_CALLBACK_FAILED, which is how the applier's error paths are
 * reached without inventing a patch format.
 */
enum dfufake_op_kind {
	DFUFAKE_OP_WRITE,  /**< mem_write(dst, bytes, len) */
	DFUFAKE_OP_READ,   /**< mem_read(src, len) — forces a full flush first */
	DFUFAKE_OP_ERASE,  /**< mem_erase(addr, len) */
	DFUFAKE_OP_STEP_SET, /**< step_set(step) */
	DFUFAKE_OP_STEP_GET, /**< step_get() — result recorded below */
};

/** One scripted memory operation handed to the applier's callbacks. */
struct dfufake_op {
	enum dfufake_op_kind kind;
	uintptr_t addr;
	size_t len;
	int step;
	uint8_t fill; /**< byte written by DFUFAKE_OP_WRITE */
};

#define DFUFAKE_MAX_OPS 64

/** @brief Replace the script with @p count operations from @p ops. */
void dfufake_script(const struct dfufake_op *ops, size_t count);

/** @brief Result of the last DFUFAKE_OP_STEP_GET the script replayed. */
int dfufake_last_step_get(void);

/** @brief How many scripted operations the callbacks accepted before stopping. */
size_t dfufake_ops_done(void);

/** Returned by the double when an applier callback refused an operation. */
#define DFUFAKE_DETOOLS_CALLBACK_FAILED (-6001)

#endif /* WOZ_DFUFAKE_H */
