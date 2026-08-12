/*
 * The kernel configuration for the target build.
 *
 * The kernel itself is FreeRTOS V10.0.0 as shipped in the pinned Qorvo SDK's
 * nRF5 SDK 17.1.0 tree, and the Cortex-M4F port under portable/CMSIS/nrf52 and
 * portable/GCC/nrf52 is compiled unmodified. What this port does not use is
 * that tree's tick file, port_cmsis_systick.c: it drives RTC1 through
 * nrf_drv_clock, and MPSL owns CLOCK on this build, so two owners would fight
 * over the low-frequency clock. board/tick_freertos.c replaces it.
 *
 * Everything below that is not a plain FreeRTOS default is justified where it
 * is defined, because several of these values are load-bearing against the
 * frozen interrupt map in peripherals.yml rather than a matter of taste.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if !(defined(__ASSEMBLY__) || defined(__ASSEMBLER__))
#include <nrf.h>
#endif

#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TIME_SLICING 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_16_BIT_TICKS 0
#define configMAX_TASK_NAME_LEN 12
#define configMAX_PRIORITIES 7
#define configENABLE_BACKWARD_COMPATIBILITY 0

/*
 * 64 MHz is the nRF52833 core clock once HFCLK runs from the crystal, which
 * MPSL starts. Nothing in this configuration derives a tick from it -- the tick
 * is RTC1 -- so this value only feeds run-time statistics and assertions.
 */
#define configCPU_CLOCK_HZ 64000000UL

/*
 * 1024 Hz, not the more usual 1000 Hz, because the tick is RTC1 running from
 * the 32768 Hz crystal with no prescaler: 32768/1024 is exactly 32 counts per
 * tick, so the tick never accumulates rounding error against the radio's
 * timebase. At 1000 Hz the same counter gives 32.768 counts per tick and the
 * two clocks drift apart by 2.4 percent.
 *
 * The prescaler has to stay at zero for a second reason: board/time_freertos.c
 * reads the same counter for woz_freertos_cycle_get_32 and
 * woz_freertos_busy_wait_us, and states 32768 Hz for it. Prescaling to get a
 * 1000 Hz tick would silently make every busy wait 33 times too long and coarsen
 * the DW3110 response-arm probe from 30.5 us to 1 ms.
 */
#define configTICK_RATE_HZ ((TickType_t)1024)

/*
 * Tickless idle is off for now. The tick is already compare-based, so the
 * mechanism is available, but suppressing ticks has to be reconciled with the
 * MPSL timeslot scheduler before it can be trusted, and this build has not
 * measured current draw yet.
 */
#define configUSE_TICKLESS_IDLE 0

#define configSUPPORT_STATIC_ALLOCATION 1

/*
 * NimBLE's FreeRTOS porting layer allocates its event queue, mutexes,
 * semaphore, and callout timers from the heap inside nimble_port_init(), and
 * Mbed TLS allocates bignums per operation. Everything else in this port is
 * static.
 *
 * The size is a first-link measurement, not a derivation, and it is expected to
 * move once the image links and the heap high-water mark can be read.
 */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE ((size_t)12 * 1024)
#endif

/* NimBLE callouts are FreeRTOS software timers; ble/nimble_host_freertos.c
 * refuses to build without them. */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH 256

#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_QUEUE_SETS 0
#define configUSE_CO_ROUTINES 0
#define configUSE_NEWLIB_REENTRANT 0
#define configMINIMAL_STACK_SIZE ((uint16_t)192)

/*
 * Overflow checking is on with the expensive method: this port has tasks whose
 * stacks were sized by estimate rather than measurement, and a silently
 * corrupted neighbour is far harder to diagnose on a lock than a named fatal.
 * The hook is board/fault_freertos.c.
 */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configRECORD_STACK_HIGH_ADDRESS 1

#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define INCLUDE_xTimerGetTimerDaemonTaskHandle 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xEventGroupSetBitFromISR 1
#define INCLUDE_xResumeFromISR 1
#define INCLUDE_pcTaskGetTaskName 1

/*
 * Interrupt priorities. The nRF52833 NVIC has three priority bits, so the only
 * legal values are 0 through 7; peripherals.yml is written in those terms.
 *
 * Nordic's port shifts both of the values below by (8 - configPRIO_BITS)
 * itself, so they are plain priority numbers here and not pre-shifted BASEPRI
 * values.
 */
#define configPRIO_BITS __NVIC_PRIO_BITS

/* The tick and PendSV run at the lowest priority the part has, so a context
 * switch can never delay a radio event. */
#define configKERNEL_INTERRUPT_PRIORITY 7

/*
 * The FromISR API ceiling, and the one value in this file that is pinned from
 * both sides.
 *
 * It must be at most 4. radio/radio_start_freertos.c installs
 * woz_freertos_radio_low_priority_isr on SWI5_EGU5 at priority 4, and that
 * handler calls woz_freertos_mpsl_wake_from_isr, which is
 * vTaskNotifyGiveFromISR followed by portYIELD_FROM_ISR. FreeRTOS permits a
 * FromISR call only from a handler at or numerically below this ceiling, so a
 * ceiling of 5 -- which is what the Qorvo baseline ships -- would make that
 * handler illegal: a genuine race against the scheduler's ready lists.
 *
 * Nothing would catch that at run time, which is worth knowing before relying
 * on the kernel to police it. The pinned port is internally inconsistent about
 * how this value is expressed: port.c shifts it by (8 - configPRIO_BITS)
 * before loading BASEPRI, so the masking is correct for a plain priority
 * number, but port_cmsis.c builds its validation threshold as
 * (configMAX_SYSCALL_INTERRUPT_PRIORITY & implemented_priority_bits). On this
 * part the implemented mask is 0xe0, so 4 & 0xe0 is 0 and
 * vPortValidateInterruptPriority can never fail. That is a vendor defect --
 * it fires the same way with Nordic's own _PRIO_APP_* values -- and the file
 * is compiled unmodified here. The static assertions in this file and in
 * radio/radio_start_freertos.c are therefore the whole of the protection,
 * not a convenience layered on a working runtime check.
 *
 * It must be greater than 1. MPSL's handlers run at 0 and the nRF 802.15.4
 * driver's at 1, and a critical section masks everything at or above the
 * ceiling. Setting it to 1 or 0 would let an ordinary taskENTER_CRITICAL delay
 * a radio interrupt, which is the one thing this whole port is arranged to
 * prevent.
 *
 * That leaves 2, 3, or 4, and 4 is chosen so the largest possible range of
 * interrupt priorities keeps access to the FromISR API. Priorities 0 through 3
 * are therefore radio-only by construction.
 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 4

#if configMAX_SYSCALL_INTERRUPT_PRIORITY > 4
#error "SWI5_EGU5 runs the MPSL low-priority handler at 4 and calls FromISR APIs"
#endif
#if configMAX_SYSCALL_INTERRUPT_PRIORITY < 2
#error "A critical section at this ceiling would mask MPSL or nRF 802.15.4"
#endif

/* The vector table in board/startup_freertos.c uses the CMSIS names. */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#define configASSERT_DEFINED 1
#if !(defined(__ASSEMBLY__) || defined(__ASSEMBLER__))
void woz_freertos_config_assert(const char *file, unsigned line);
#define configASSERT(x)                                                                            \
	do {                                                                                       \
		if (!(x)) {                                                                        \
			woz_freertos_config_assert(__FILE__, (unsigned)__LINE__);                   \
		}                                                                                  \
	} while (0)
#endif

#endif /* FREERTOS_CONFIG_H */
