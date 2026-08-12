/*
 * The callbacks the kernel requires from the application, gathered in one file.
 *
 * All four are failure or allocation paths. The static-allocation pair exists
 * because configSUPPORT_STATIC_ALLOCATION is on and the kernel then refuses to
 * place its own idle and timer tasks; the other two turn conditions that would
 * otherwise corrupt memory quietly into a named fatal.
 */
#include <stdint.h>

#include <FreeRTOS.h>
#include <task.h>

#include <ultrawidelock_freertos_platform.h>

#define KERNEL_TAG "kernel"

/*
 * configASSERT. A failed kernel assertion means an invariant the scheduler
 * relies on is already broken, so continuing is not an option; the file and
 * line are the only useful thing left and they are logged before the reset.
 */
void ultrawidelock_freertos_config_assert(const char *file, unsigned line)
{
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, KERNEL_TAG, "assert at %s:%u",
			 file != NULL ? file : "?", line);
	ultrawidelock_freertos_fatal("FreeRTOS assertion failed");
}

/*
 * A stack overflow has already happened by the time this runs: the neighbouring
 * memory is overwritten. Naming the task is worth more than any attempt to
 * recover, because the fix is always to resize that one stack.
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
	(void)task;
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, KERNEL_TAG,
				   "stack overflow in task %s", name != NULL ? name : "?");
	ultrawidelock_freertos_fatal("FreeRTOS stack overflow");
}

/*
 * Every heap user in this port allocates once at startup and never frees, so a
 * failure here is a heap sized too small rather than a leak, and it is fatal
 * for the same reason: a lock that came up without its BLE host is not a lock.
 */
void vApplicationMallocFailedHook(void)
{
	ultrawidelock_freertos_fatal("FreeRTOS heap exhausted");
}

static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
	*tcb = &s_idle_tcb;
	*stack = s_idle_stack;
	*depth = configMINIMAL_STACK_SIZE;
}

static StaticTask_t s_timer_tcb;
static StackType_t s_timer_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *depth)
{
	*tcb = &s_timer_tcb;
	*stack = s_timer_stack;
	*depth = configTIMER_TASK_STACK_DEPTH;
}
