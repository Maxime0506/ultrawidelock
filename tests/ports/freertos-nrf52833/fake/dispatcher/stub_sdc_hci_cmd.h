/*
 * Recorder shared by the generated SoftDevice Controller command stubs. The
 * stub bodies themselves are generated from the real nrfxlib prototypes by
 * scripts/freertos-hci-dispatcher-check.sh, so the dispatcher links against
 * exactly the command surface this port's configuration selects.
 */
#ifndef ULTRAWIDELOCK_STUB_SDC_HCI_CMD_H
#define ULTRAWIDELOCK_STUB_SDC_HCI_CMD_H

#include <stdint.h>

/** Record one controller command call. Always returns success status 0x00. */
uint8_t ultrawidelock_stub_record(const char *name);

/** Calls recorded for one command entry point since the last reset. */
unsigned ultrawidelock_stub_calls(const char *name);

/** Calls recorded across every command entry point since the last reset. */
unsigned ultrawidelock_stub_total(void);

void ultrawidelock_stub_reset(void);

#endif /* ULTRAWIDELOCK_STUB_SDC_HCI_CMD_H */
