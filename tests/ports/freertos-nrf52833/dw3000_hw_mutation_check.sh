#!/usr/bin/env bash
#
# Prove the DW3110 hardware layer's tests bite.
#
# Same contract as its SPI sibling: each mutation is a defect this backend could
# plausibly have shipped with, applied to a copy of the production source, and
# the suite must fail for every one. A mutation that survives is a decorative
# check, reported here as a failure.
#
# The interrupt-path mutations are the ones worth reading. Every one of them
# produces a board that comes up, logs nothing, and never ranges.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${ALIRO_BUILD_ROOT:-$ROOT/build}/freertos-nrf52833-host/hw-mutants"
SRC="$ROOT/ports/freertos-nrf52833/uwb/dw3000_hw_freertos.c"

mkdir -p "$OUT"

build() { # <source> <binary>
	"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -DWOZ_PORT_FREERTOS \
		-I"$HERE/fake" \
		-I"$ROOT/ports/freertos-nrf52833/include" \
		-I"$ROOT/ports/freertos-nrf52833/uwb" \
		-I"$ROOT/modules/woz_dw3000/include" \
		-I"$ROOT/modules/woz_dw3000/dwt_uwb_driver" \
		"$HERE/test_dw3000_hw.c" \
		"$HERE/fake/fake_freertos.c" \
		"$HERE/fake/fake_nrf.c" \
		"$HERE/fake/fake_gpio.c" \
		"$HERE/fake/fake_gpiote.c" \
		"$HERE/fake/fake_spim.c" \
		"$ROOT/ports/freertos-nrf52833/uwb/dw3000_spi_freertos.c" \
		"$ROOT/ports/freertos-nrf52833/board/gpiote_freertos.c" \
		"$1" \
		-o "$2"
}

MUTATIONS=(
	"the GPIOTE channel is enabled but never bound to the pin ::: 	nrf_gpiote_event_configure(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL, WOZ_DW3000_PIN_IRQ,
				   NRF_GPIOTE_POLARITY_LOTOHI);
 ::: "
	"the line is never registered on the shared GPIOTE vector ::: 	if (woz_freertos_gpiote_add_handler(woz_freertos_dw3000_irq_handler) != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, \"no GPIOTE handler slot\");
		return -1;
	}
 ::: "
	"the channel watches the wrong edge ::: NRF_GPIOTE_POLARITY_LOTOHI ::: NRF_GPIOTE_POLARITY_HITOLO"
	"the channel is configured but left disabled ::: nrf_gpiote_event_enable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL); ::: (void)0;"
	"the GPIOTE interrupt is never unmasked ::: 	nrf_gpiote_int_enable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
	s_irq_enabled = true; ::: 	s_irq_enabled = true;"
	"the vector is never enabled in the NVIC ::: NVIC_EnableIRQ(GPIOTE_IRQn); ::: (void)0;"
	"the vector runs one level below the frozen priority ::: #define WOZ_DW3000_IRQ_PRIORITY 4u ::: #define WOZ_DW3000_IRQ_PRIORITY 5u"
	"the vector leaves the latched event set ::: nrf_gpiote_event_clear(NRF_GPIOTE, NRF_GPIOTE_EVENT_IN_0);

	g_dw_cyc_gpio ::: g_dw_cyc_gpio"
	"the vector notifies without checking there was an event ::: 	if (!nrf_gpiote_event_check(NRF_GPIOTE, NRF_GPIOTE_EVENT_IN_0)) {
		return;
	}
 ::: "
	"the vector does the driver's work itself instead of notifying ::: 	if (s_task != NULL) {
		vTaskNotifyGiveFromISR(s_task, &wake);
		portYIELD_FROM_ISR(wake);
	} ::: 	dwt_isr();
	(void)wake;"
	"the vector notifies but never asks for a reschedule ::: 		portYIELD_FROM_ISR(wake); ::: 		(void)wake;"
	"the worker services one event instead of draining the line ::: 		while (nrf_gpio_pin_read(WOZ_DW3000_PIN_IRQ)) {
			dwt_isr();
		} ::: 		dwt_isr();"
	"the worker runs at an ordinary priority ::: #define WOZ_DW3000_ISR_TASK_PRIORITY (configMAX_PRIORITIES - 1) ::: #define WOZ_DW3000_ISR_TASK_PRIORITY (configMAX_PRIORITIES - 4)"
	"a second bring-up creates a second worker ::: 	if (s_task == NULL) {
		s_task = xTaskCreateStatic ::: 	if (s_task != NULL || s_task == NULL) {
		s_task = xTaskCreateStatic"
	"the reset line is driven high instead of released ::: 	nrf_gpio_cfg_input(WOZ_DW3000_PIN_RST, NRF_GPIO_PIN_NOPULL);

	/* Long enough ::: 	nrf_gpio_pin_set(WOZ_DW3000_PIN_RST);

	/* Long enough"
	"reset comes up driven instead of released ::: 	nrf_gpio_cfg_input(WOZ_DW3000_PIN_RST, NRF_GPIO_PIN_NOPULL);

	/* Wake line ::: 	nrf_gpio_cfg_output(WOZ_DW3000_PIN_RST);

	/* Wake line"
	"waking does not wait for the chip to reach IDLE_RC ::: 	woz_freertos_busy_wait_us(2000); /* INIT_RC to IDLE_RC. */
	s_asleep = false; ::: 	s_asleep = false;"
	"reset does not let the chip climb to IDLE_RC before returning ::: 	/* Long enough for the chip to climb from INIT_RC to IDLE_RC. */
	woz_freertos_busy_wait_us(2000); ::: "
	"the wake line comes up deasserted ::: nrf_gpio_pin_set(WOZ_DW3000_PIN_WAKEUP); ::: nrf_gpio_pin_clear(WOZ_DW3000_PIN_WAKEUP);"
	"waking an already awake chip strobes it anyway ::: 	if (!s_asleep) {
		return;
	}
	dw3000_spi_wakeup(); ::: 	dw3000_spi_wakeup();"
	"a chip that never reaches IDLE_RC is assumed to have woken ::: 	while (!dwt_checkidlerc() && spins < 500u) {
		woz_freertos_busy_wait_us(10);
		spins++;
	} ::: 	(void)dwt_checkidlerc();"
	"masking the line reports itself enabled anyway ::: 		nrf_gpiote_int_disable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
		s_irq_enabled = false; ::: 		nrf_gpiote_int_disable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);"
	"unmasking never re-enables the interrupt ::: 		nrf_gpiote_int_enable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
		s_irq_enabled = true;
	}
}

void dw3000_hw_interrupt_disable ::: 		s_irq_enabled = true;
	}
}

void dw3000_hw_interrupt_disable"
	"shutting down leaves the GPIOTE channel live ::: nrf_gpiote_event_disable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL); ::: (void)0;"
	"shutting down leaves the SPI bus up ::: 	nrf_gpiote_event_disable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL);
	dw3000_spi_fini(); ::: 	nrf_gpiote_event_disable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL);"
)

pass=0
fail=0

if build "$SRC" "$OUT/baseline" >"$OUT/baseline.log" 2>&1 && "$OUT/baseline" >"$OUT/baseline.out" 2>&1; then
	printf '  ok   the unmutated hardware layer passes\n'
	pass=$((pass + 1))
else
	printf '  FAIL the unmutated hardware layer does not pass; nothing below means anything\n'
	cat "$OUT/baseline.out" 2>/dev/null || true
	exit 1
fi

i=0
for entry in "${MUTATIONS[@]}"; do
	i=$((i + 1))
	desc=${entry%%' ::: '*}
	rest=${entry#*' ::: '}
	find=${rest%%' ::: '*}
	replace=${rest#*' ::: '}
	mutant="$OUT/mutant_$i.c"

	if ! FIND="$find" REPLACE="$replace" python3 -c '
import os, sys
src = open(sys.argv[1]).read()
find, replace = os.environ["FIND"], os.environ["REPLACE"]
if src.count(find) != 1:
    sys.stderr.write("pattern appears %d times, expected 1\n" % src.count(find))
    sys.exit(1)
open(sys.argv[2], "w").write(src.replace(find, replace))
' "$SRC" "$mutant" 2>"$OUT/mutant_$i.patchlog"; then
		printf '  FAIL mutation %d could not be applied: %s\n' "$i" "$desc"
		cat "$OUT/mutant_$i.patchlog"
		fail=$((fail + 1))
		continue
	fi

	if ! build "$mutant" "$OUT/mutant_$i" >"$OUT/mutant_$i.log" 2>&1; then
		printf '  ok   rejected at compile time: %s\n' "$desc"
		pass=$((pass + 1))
		continue
	fi

	if "$OUT/mutant_$i" >"$OUT/mutant_$i.out" 2>&1; then
		printf '  FAIL survives the suite: %s\n' "$desc"
		fail=$((fail + 1))
	else
		printf '  ok   caught: %s\n' "$desc"
		pass=$((pass + 1))
	fi
done

printf 'dw3000-hw-mutation: %s (%d mutations)\n' \
	"$([ "$fail" -eq 0 ] && echo PASS || echo FAIL)" "$i"
[ "$fail" -eq 0 ]
