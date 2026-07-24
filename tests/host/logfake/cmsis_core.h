/* logfake: minimal <cmsis_core.h> — the DWT cycle counter + CoreDebug enable
 * bit as plain host structs the suite can poke. */
#ifndef LOGFAKE_CMSIS_CORE_H
#define LOGFAKE_CMSIS_CORE_H

#include <stdint.h>

typedef struct {
	volatile uint32_t CTRL;
	volatile uint32_t CYCCNT;
} logfake_dwt_t;

typedef struct {
	volatile uint32_t DEMCR;
} logfake_coredebug_t;

extern logfake_dwt_t *DWT;
extern logfake_coredebug_t *CoreDebug;

#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24)
#define DWT_CTRL_CYCCNTENA_Msk     (1u << 0)

#endif /* LOGFAKE_CMSIS_CORE_H */
