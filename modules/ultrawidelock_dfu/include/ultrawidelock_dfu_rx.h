/* SPDX-License-Identifier: ISC */

/**
 * @file
 * @brief Receives a delta patch into the staging partition, application side.
 *
 * Transport-independent on purpose. The DWM3001CDK feeds this from a second
 * L2CAP CoC beside the credential one, but nothing here knows that -- it takes
 * frames and returns replies, so the host tests can drive it without a radio.
 *
 * The bootloader half is @ref ultrawidelock_dfu.h. This side never applies anything: it
 * writes bytes, checks a signature, and reboots.
 */

#ifndef ULTRAWIDELOCK_DFU_RX_H_
#define ULTRAWIDELOCK_DFU_RX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Request opcodes, first byte of every frame from the host. */
enum ultrawidelock_dfu_op {
	ULTRAWIDELOCK_DFU_OP_BEGIN = 0x01,  /**< u32 total wire length follows */
	ULTRAWIDELOCK_DFU_OP_DATA = 0x02,   /**< payload bytes follow */
	ULTRAWIDELOCK_DFU_OP_COMMIT = 0x03, /**< no body; reboots on success */
	ULTRAWIDELOCK_DFU_OP_ABORT = 0x04,  /**< no body; erases what was staged */
};

/** Reply opcodes, first byte of every frame back to the host. */
enum ultrawidelock_dfu_rsp {
	ULTRAWIDELOCK_DFU_RSP_OK = 0x81,  /**< u32 bytes received so far follows */
	ULTRAWIDELOCK_DFU_RSP_ERR = 0x82, /**< one @ref ultrawidelock_dfu_err byte follows */
};

/**
 * Why a frame was refused.
 *
 * Deliberately coarse. A peer that has not been let in learns only that it was
 * refused, not how close it got.
 */
enum ultrawidelock_dfu_err {
	ULTRAWIDELOCK_DFU_ERR_CLOSED = 1,    /**< no update window is open */
	ULTRAWIDELOCK_DFU_ERR_SEQUENCE = 2,  /**< opcode does not fit the current state */
	ULTRAWIDELOCK_DFU_ERR_SIZE = 3,      /**< will not fit patch_staging */
	ULTRAWIDELOCK_DFU_ERR_AUTH = 4,      /**< header signature did not verify */
	ULTRAWIDELOCK_DFU_ERR_INTEGRITY = 5, /**< length or CRC disagreed at commit */
	ULTRAWIDELOCK_DFU_ERR_FLASH = 6,     /**< a write or erase failed */
	ULTRAWIDELOCK_DFU_ERR_MALFORMED = 7, /**< frame too short for its opcode */
};

/** Largest reply this ever produces. */
#define ULTRAWIDELOCK_DFU_RSP_MAX 5u

/**
 * Open the update window for @p duration_ms.
 *
 * Until this is called nothing is accepted, and that IS the authorization
 * model. The patch is signed and MCUboot re-verifies the result, so no peer can
 * install code regardless; what the window prevents is an unauthenticated
 * peer in radio range burning flash cycles and forcing reboots. A door lock
 * that anyone nearby can reset in a loop is a real availability attack, and a
 * window the owner has to open is what stops it.
 *
 * Calling it again while open restarts the clock.
 */
void ultrawidelock_dfu_window_open(uint32_t duration_ms);

/** Close the window immediately and discard any transfer in progress. */
void ultrawidelock_dfu_window_close(void);

/** True while the window is open. Transports gate their accept() on this. */
bool ultrawidelock_dfu_window_is_open(void);

/**
 * Called whenever the window opens or closes.
 *
 * Registered rather than a weak symbol so that it survives LTO without
 * argument, and so a port with no indicator pays nothing.
 */
typedef void (*ultrawidelock_dfu_window_cb)(bool open);

/**
 * Watch the window, so the board can SHOW that it is open.
 *
 * There are three ways in -- SW2, Apple Home's pairing mode, and the bench
 * SWD write -- and none of them is visible from outside the board. An owner who
 * pressed the button has no way to tell whether the press registered, and the
 * five-minute window can expire while they are still looking for the phone.
 * One callback covers every path because they all end at
 * ultrawidelock_dfu_window_open().
 */
void ultrawidelock_dfu_set_window_cb(ultrawidelock_dfu_window_cb cb);

/**
 * Handle one frame.
 *
 * @param frame     request bytes, opcode first
 * @param len       length of @p frame
 * @param rsp       at least @ref ULTRAWIDELOCK_DFU_RSP_MAX bytes
 * @param rsp_len   set to the number of reply bytes produced
 *
 * @retval 0 always; failures are reported to the peer through @p rsp, because
 *           a transport has nothing useful to do with an error code.
 */
int ultrawidelock_dfu_rx_frame(const uint8_t *frame, size_t len, uint8_t *rsp, size_t *rsp_len);

/** Drop any transfer in progress. Transports call this on disconnect. */
void ultrawidelock_dfu_rx_reset(void);

#ifdef CONFIG_ULTRAWIDELOCK_DFU_SMP_IMG
/**
 * Take one SMP image-upload chunk.
 *
 * The same bytes and the same checks as @ref ultrawidelock_dfu_rx_frame, reached from
 * CBOR instead of opcodes, so that a stock mcumgr client (nRF Device Manager,
 * `mcumgr image upload`) can push an update. See src/dfu_smp_img.c.
 *
 * @param off    offset the host believes this chunk starts at
 * @param total  whole wire length; only read when @p off is 0
 * @param data   chunk bytes
 * @param len    length of @p data
 * @param[out] next  offset to send next. On a mismatched @p off this comes back
 *                   as the device's real position and the chunk is discarded --
 *                   a resync, which the protocol treats as success.
 *
 * @retval 0        chunk accepted, or a resync was requested
 * @retval -EACCES  no update window is open
 * @retval -EINVAL  refused; the transfer is discarded and must restart at 0
 */
int ultrawidelock_dfu_rx_upload(uint32_t off, uint32_t total, const uint8_t *data, size_t len,
		      uint32_t *next);

/** True when a complete, verified update is staged and waiting for a reboot. */
bool ultrawidelock_dfu_rx_staged(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ULTRAWIDELOCK_DFU_RX_H_ */
