/*
 * Reset entry and the vector table.
 *
 * Written here rather than taken from a vendor startup file, for one reason
 * that matters: peripherals.yml freezes which peripheral belongs to which
 * owner, and the vector table is where that ownership becomes real. Keeping it
 * in one readable C file means the frozen map can be checked against the
 * hardware by reading a single page, instead of by cross-referencing weak
 * symbol overrides scattered across a link.
 *
 * The pinned Qorvo tree ships a startup file whose radio vectors are weak stubs
 * meant to be overridden at link time. That works, but it makes the routing
 * invisible: nothing tells a reader that RADIO belongs to MPSL except the
 * absence of a symbol. Here the handoff is the code.
 *
 * The two device headers this leans on -- nrf.h and system_nrf52833.h -- come
 * from the same pinned hal_nordic MDK the radio stack is built against, so
 * there is exactly one register map in the image.
 */
#include <stdint.h>

#include <nrf.h>
#include <system_nrf52833.h>

#include <woz_freertos_radio.h>

extern uint32_t __etext;
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __StackTop;

extern int main(void);

void Reset_Handler(void);

/*
 * Anything not claimed below reaching the CPU means a peripheral was enabled
 * without an owner. That is a configuration mistake rather than a runtime
 * condition, so it stops here instead of returning quietly and leaving the
 * interrupt latched forever.
 */
static void default_handler(void)
{
	for (;;) {
		__NOP();
	}
}

#define WEAK_ALIAS(name)                                                                           \
	void name(void) __attribute__((weak, alias("default_handler")))

/* Core exceptions. The kernel supplies SVC_Handler and PendSV_Handler; the
 * rest are overridable by the application's fault handling. */
WEAK_ALIAS(NMI_Handler);
WEAK_ALIAS(HardFault_Handler);
WEAK_ALIAS(MemoryManagement_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(SVC_Handler);
WEAK_ALIAS(DebugMon_Handler);
WEAK_ALIAS(PendSV_Handler);
WEAK_ALIAS(SysTick_Handler);

/*
 * Peripheral vectors with no owner in peripherals.yml. They are weak so a
 * driver added later -- USB, the DW3000's SPI, GPIOTE -- can claim one by
 * defining the standard name, without this file having to be edited.
 */
WEAK_ALIAS(UARTE0_UART0_IRQHandler);
WEAK_ALIAS(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler);
WEAK_ALIAS(SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler);
WEAK_ALIAS(NFCT_IRQHandler);
WEAK_ALIAS(SAADC_IRQHandler);
WEAK_ALIAS(TIMER2_IRQHandler);
WEAK_ALIAS(TEMP_IRQHandler);
WEAK_ALIAS(ECB_IRQHandler);
WEAK_ALIAS(CCM_AAR_IRQHandler);
WEAK_ALIAS(WDT_IRQHandler);
WEAK_ALIAS(QDEC_IRQHandler);
WEAK_ALIAS(COMP_LPCOMP_IRQHandler);
WEAK_ALIAS(SWI1_EGU1_IRQHandler);
WEAK_ALIAS(SWI2_EGU2_IRQHandler);
WEAK_ALIAS(SWI3_EGU3_IRQHandler);
WEAK_ALIAS(SWI4_EGU4_IRQHandler);
WEAK_ALIAS(TIMER3_IRQHandler);
WEAK_ALIAS(TIMER4_IRQHandler);
WEAK_ALIAS(PWM0_IRQHandler);
WEAK_ALIAS(PDM_IRQHandler);
WEAK_ALIAS(MWU_IRQHandler);
WEAK_ALIAS(PWM1_IRQHandler);
WEAK_ALIAS(PWM2_IRQHandler);
WEAK_ALIAS(SPIM2_SPIS2_SPI2_IRQHandler);
WEAK_ALIAS(I2S_IRQHandler);
WEAK_ALIAS(FPU_IRQHandler);
WEAK_ALIAS(USBD_IRQHandler);
WEAK_ALIAS(UARTE1_IRQHandler);
WEAK_ALIAS(PWM3_IRQHandler);
WEAK_ALIAS(SPIM3_IRQHandler);

/*
 * Owned vectors. Each of these is a handoff named in peripherals.yml, and each
 * forwards to the port entry point that peripherals.yml says owns it. The
 * priorities are not set here -- the owning driver sets its own, because the
 * priority and the handler have to move together.
 */
void POWER_CLOCK_IRQHandler(void)
{
	woz_freertos_radio_power_clock_isr();
}

void RADIO_IRQHandler(void)
{
	woz_freertos_radio_radio_isr();
}

void TIMER0_IRQHandler(void)
{
	woz_freertos_radio_timer0_isr();
}

void RTC0_IRQHandler(void)
{
	woz_freertos_radio_rtc0_isr();
}

void SWI5_EGU5_IRQHandler(void)
{
	woz_freertos_radio_low_priority_isr();
}

/*
 * TIMER1, SWI0_EGU0, RTC2, RTC1, RNG, and GPIOTE are defined by their owners:
 * TIMER1 by the pinned Nordic high-precision timer, SWI0_EGU0 by the nRF
 * 802.15.4 driver, RTC2 by radio/nrf_802154_lptimer_freertos.c, RTC1 by
 * board/tick_freertos.c, RNG by board/entropy_freertos.c, and GPIOTE by
 * board/gpiote_freertos.c, which fans it out because the DW3110 line and the
 * update button each take a channel. They are declared weak here only so an
 * image that leaves one of those out still links.
 *
 * A weak definition here does not shadow the real one: this object is pulled
 * for the vector table, but each owner's object is pulled by an ordinary call
 * into it -- woz_freertos_gpiote_add_handler() in GPIOTE's case -- and a strong
 * definition in an object the linker has already taken wins over a weak one.
 * An owner reachable ONLY through its vector would not be pulled at all, which
 * is why none of them is written that way.
 */
WEAK_ALIAS(GPIOTE_IRQHandler);
WEAK_ALIAS(TIMER1_IRQHandler);
WEAK_ALIAS(SWI0_EGU0_IRQHandler);
WEAK_ALIAS(RTC1_IRQHandler);
WEAK_ALIAS(RTC2_IRQHandler);
WEAK_ALIAS(RNG_IRQHandler);

typedef void (*vector_t)(void);

/*
 * SPIM3 is vector 47 and the highest the part defines, so the table is 48
 * entries plus the 16 core ones.
 */
const vector_t __isr_vector[] __attribute__((used, section(".isr_vector"))) = {
	(vector_t)(&__StackTop),
	Reset_Handler,
	NMI_Handler,
	HardFault_Handler,
	MemoryManagement_Handler,
	BusFault_Handler,
	UsageFault_Handler,
	0,
	0,
	0,
	0,
	SVC_Handler,
	DebugMon_Handler,
	0,
	PendSV_Handler,
	SysTick_Handler,

	POWER_CLOCK_IRQHandler,                        /* 0 */
	RADIO_IRQHandler,                              /* 1 */
	UARTE0_UART0_IRQHandler,                       /* 2 */
	SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler,  /* 3 */
	SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler,  /* 4 */
	NFCT_IRQHandler,                               /* 5 */
	GPIOTE_IRQHandler,                             /* 6 */
	SAADC_IRQHandler,                              /* 7 */
	TIMER0_IRQHandler,                             /* 8 */
	TIMER1_IRQHandler,                             /* 9 */
	TIMER2_IRQHandler,                             /* 10 */
	RTC0_IRQHandler,                               /* 11 */
	TEMP_IRQHandler,                               /* 12 */
	RNG_IRQHandler,                                /* 13 */
	ECB_IRQHandler,                                /* 14 */
	CCM_AAR_IRQHandler,                            /* 15 */
	WDT_IRQHandler,                                /* 16 */
	RTC1_IRQHandler,                               /* 17 */
	QDEC_IRQHandler,                               /* 18 */
	COMP_LPCOMP_IRQHandler,                        /* 19 */
	SWI0_EGU0_IRQHandler,                          /* 20 */
	SWI1_EGU1_IRQHandler,                          /* 21 */
	SWI2_EGU2_IRQHandler,                          /* 22 */
	SWI3_EGU3_IRQHandler,                          /* 23 */
	SWI4_EGU4_IRQHandler,                          /* 24 */
	SWI5_EGU5_IRQHandler,                          /* 25 */
	TIMER3_IRQHandler,                             /* 26 */
	TIMER4_IRQHandler,                             /* 27 */
	PWM0_IRQHandler,                               /* 28 */
	PDM_IRQHandler,                                /* 29 */
	0,                                             /* 30 */
	0,                                             /* 31 */
	MWU_IRQHandler,                                /* 32 */
	PWM1_IRQHandler,                               /* 33 */
	PWM2_IRQHandler,                               /* 34 */
	SPIM2_SPIS2_SPI2_IRQHandler,                   /* 35 */
	RTC2_IRQHandler,                               /* 36 */
	I2S_IRQHandler,                                /* 37 */
	FPU_IRQHandler,                                /* 38 */
	USBD_IRQHandler,                               /* 39 */
	UARTE1_IRQHandler,                             /* 40 */
	0,                                             /* 41 */
	0,                                             /* 42 */
	0,                                             /* 43 */
	0,                                             /* 44 */
	PWM3_IRQHandler,                               /* 45 */
	0,                                             /* 46 */
	SPIM3_IRQHandler,                              /* 47 */
};

void Reset_Handler(void)
{
	uint32_t *src;
	uint32_t *dst;

	/*
	 * SystemInit comes from the pinned MDK and is the part's own errata
	 * work. On this device it also enables the instruction cache and sets
	 * the trace pin state, neither of which is safe to reimplement by hand.
	 */
	SystemInit();

	/*
	 * Point the core at our vector table before anything can interrupt.
	 *
	 * Required, not defensive. This image is linked at 0xa200 and starts with
	 * MCUboot's table still selected in VTOR -- MCUboot jumps to our reset
	 * vector without changing it, because an image that relocates itself is
	 * the only one that knows where it went. Every interrupt taken before
	 * this line would dispatch through the bootloader's table, which by then
	 * describes handlers that are no longer running.
	 *
	 * SystemInit() first: it is the part's errata work and touches no
	 * interrupt this could race.
	 */
	SCB->VTOR = (uint32_t)&__isr_vector[0];
	__DSB();
	__ISB();

	for (src = &__etext, dst = &__data_start__; dst < &__data_end__;) {
		*dst++ = *src++;
	}
	for (dst = &__bss_start__; dst < &__bss_end__;) {
		*dst++ = 0u;
	}

	/*
	 * The FPU is enabled before main because the kernel's Cortex-M4F port
	 * refuses to build without hardware floating point, and its context
	 * switch assumes the unit is reachable.
	 */
	SCB->CPACR |= (3uL << 20) | (3uL << 22);
	__DSB();
	__ISB();

	(void)main();

	for (;;) {
		/* main() is not expected to return. */
	}
}
