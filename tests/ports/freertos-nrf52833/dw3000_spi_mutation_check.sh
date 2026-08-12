#!/usr/bin/env bash
#
# Prove the DW3110 SPI backend's tests bite.
#
# A test that passes tells you nothing on its own: it passes against correct
# code and it would pass against a backend with the bug removed from the test's
# reach. So each mutation below is a real defect -- one this port could
# plausibly have been written with -- applied to a copy of the production
# source. The test must FAIL for every one of them. A mutation the suite
# survives is a check that is decorative, and it is reported as a failure here.
#
# Every mutation names the rule it breaks, because that is the part worth
# reading: the list doubles as the inventory of what the SPI backend is
# actually being held to.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${ALIRO_BUILD_ROOT:-$ROOT/build}/freertos-nrf52833-host/spi-mutants"
SRC="$ROOT/ports/freertos-nrf52833/uwb/dw3000_spi_freertos.c"

mkdir -p "$OUT"

build() { # <source> <binary>
	"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -DWOZ_PORT_FREERTOS \
		-I"$HERE/fake" \
		-I"$ROOT/ports/freertos-nrf52833/include" \
		-I"$ROOT/ports/freertos-nrf52833/uwb" \
		-I"$ROOT/modules/ultrawidelock_dw3000/include" \
		"$HERE/test_dw3000_spi.c" \
		"$HERE/fake/fake_freertos.c" \
		"$HERE/fake/fake_gpio.c" \
		"$HERE/fake/fake_spim.c" \
		"$1" \
		-o "$2"
}

# Each entry is: description ::: text to find ::: text to put in its place.
# The find text must appear exactly once, so a mutation cannot silently become
# a no-op when the source is edited.
MUTATIONS=(
	"a stale END event is not cleared before starting ::: 	nrf_spim_event_clear(NRF_SPIM3, NRF_SPIM_EVENT_END);

	cs_assert(); ::: 	cs_assert();"
	"EasyDMA is pointed at the caller's flash-resident header ::: nrf_spim_tx_buffer_set(NRF_SPIM3, s_tx, (size_t)total); ::: nrf_spim_tx_buffer_set(NRF_SPIM3, hdr, (size_t)total);"
	"send and receive share one buffer ::: nrf_spim_rx_buffer_set(NRF_SPIM3, s_rx, ::: nrf_spim_rx_buffer_set(NRF_SPIM3, s_tx,"
	"the clock pin's input buffer is left disconnected ::: NRF_GPIO_PIN_INPUT_CONNECT, ::: NRF_GPIO_PIN_INPUT_DISCONNECT,"
	"chip select is never asserted ::: cs_assert();
	nrf_spim_task_trigger ::: nrf_spim_task_trigger"
	"chip select is left low after the command ::: 		ret = -1;
	}
	cs_release(); ::: 		ret = -1;
	}"
	"the response is taken from the front of the buffer ::: memcpy(rx_body, s_rx + hlen, blen); ::: memcpy(rx_body, s_rx, blen);"
	"a stalled peripheral is abandoned rather than stopped ::: 		xfer_abort();
		ret = -1; ::: 		ret = -1;"
	"the bus lock is not released when a transfer fails ::: 	(void)xSemaphoreGive(s_lock);

	if (ret != 0) { ::: 	if (ret == 0) {
		(void)xSemaphoreGive(s_lock);
	}

	if (ret != 0) {"
	"the transfer bound is off by one, so an over-long command is truncated ::: total > DW_XFER_MAX ::: total > DW_XFER_MAX + 1u"
	"a zero-length header is accepted ::: hlen == 0u || ::: "
	"the wrong SPI mode is selected ::: NRF_SPIM_MODE_0 ::: NRF_SPIM_MODE_1"
	"bits go out least significant first ::: NRF_SPIM_BIT_ORDER_MSB_FIRST ::: NRF_SPIM_BIT_ORDER_LSB_FIRST"
	"MOSI is left high while the chip answers a read ::: nrf_spim_orc_set(NRF_SPIM3, 0); ::: nrf_spim_orc_set(NRF_SPIM3, 0xff);"
	"MISO is left floating instead of pulled down ::: NRF_GPIO_PIN_PULLDOWN ::: NRF_GPIO_PIN_NOPULL"
	"chip select is made an output before its idle level is written ::: 	nrf_gpio_pin_set(ULTRAWIDELOCK_DW3000_PIN_CS);
	nrf_gpio_cfg_output(ULTRAWIDELOCK_DW3000_PIN_CS); ::: 	nrf_gpio_cfg_output(ULTRAWIDELOCK_DW3000_PIN_CS);
	nrf_gpio_pin_set(ULTRAWIDELOCK_DW3000_PIN_CS);"
	"the bus starts at the fast clock, before the chip's PLL has locked ::: BIT_ORDER_MSB_FIRST);
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_SLOW_HZ); ::: BIT_ORDER_MSB_FIRST);
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_FAST_HZ);"
	"the fast rate is set past what the board is qualified for ::: void dw3000_spi_speed_fast(void)
{
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_FAST_HZ); ::: void dw3000_spi_speed_fast(void)
{
	s_freq = freq_of(32000000u);"
	"the wake pulse is too short for the chip to notice ::: woz_freertos_busy_wait_us(500); ::: woz_freertos_busy_wait_us(50);"
	"the CRC byte is dropped from the command ::: 	if (crc != NULL) {
		s_tx[hlen + blen] = *crc;
	} ::: 	if (crc != NULL) {
		(void)crc;
	}"
	"bringing the bus up twice reconfigures it under a live chip ::: 	if (s_ready) {
		return 0;
	} ::: "
	"shutting the bus down leaves the peripheral enabled ::: nrf_spim_disable(NRF_SPIM3); ::: (void)0;"
	"a transfer after shutdown is attempted anyway ::: 	if (!s_ready) {
		return -1;
	} ::: "
	"the body is sent before the header ::: 	memcpy(s_tx, hdr, hlen);
	if (body != NULL && blen != 0u) {
		memcpy(s_tx + hlen, body, blen); ::: 	memcpy(s_tx + blen, hdr, hlen);
	if (body != NULL && blen != 0u) {
		memcpy(s_tx, body, blen);"
)

pass=0
fail=0

# The unmutated source must pass, or every result below is meaningless.
if build "$SRC" "$OUT/baseline" >"$OUT/baseline.log" 2>&1 && "$OUT/baseline" >"$OUT/baseline.out" 2>&1; then
	printf '  ok   the unmutated backend passes\n'
	pass=$((pass + 1))
else
	printf '  FAIL the unmutated backend does not pass; nothing below means anything\n'
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
		# A mutation the compiler rejects is still caught, just earlier.
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

printf 'dw3000-spi-mutation: %s (%d mutations)\n' \
	"$([ "$fail" -eq 0 ] && echo PASS || echo FAIL)" "$i"
[ "$fail" -eq 0 ]
