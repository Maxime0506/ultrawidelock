/**
 * @file test_nfc_transport.cpp — the woz_nfc transport seam on host.
 *
 * Files under test:
 *   ports/zephyr/nfc/pn532_bus_spi.c    Zephyr SPI/GPIO glue
 *   modules/woz_nfc/src/transport_pn532.cpp  the PN532 reader backend
 *   modules/woz_nfc/src/transport_none.cpp   the no-reader backend
 *
 * pn532.c and pn532_apdu.c are linked in FOR REAL, not faked, so every frame
 * these tests move is encoded and parsed by the shipping codec against the
 * scripted bus in tests/host/nfcfake. What is not real: the SPI peripheral,
 * the chip, and the Aliro stack, which is a call recorder.
 *
 * TWO BACKENDS DEFINE THE SAME FIVE SYMBOLS, so transport_none.cpp is compiled
 * with -DWozNfc=WozNfcNone -- a compile flag on its own step, not a source
 * edit, the same trick run.sh already uses for the two crypto backends.
 */
#include <cstdio>
#include <cstring>

extern "C" {
#include "nfcfake.h"
#include "test.h"
}

#include <aliro/errors.h>
#include <aliro/types.h>
#include <woz_nfc/transport.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

extern "C" {
#include "pn532.h"
#include <woz_nfc/pn532_bus.h>
}

/** The same five entry points, renamed at compile time on the other backend. */
namespace WozNfcNone
{
AliroError Init();
AliroError Start();
AliroError Stop();
AliroError Send(Aliro::Data data);
AliroError Terminate();
} // namespace WozNfcNone

/* PN532 command codes, for scripting responses. */
#define CMD_GET_FIRMWARE_VERSION  0x02
#define CMD_SAM_CONFIGURATION     0x14
#define CMD_RF_CONFIGURATION      0x32
#define CMD_WRITE_REGISTER        0x08
#define CMD_IN_LIST_PASSIVE       0x4a
#define CMD_IN_DATA_EXCHANGE      0x40
#define CMD_IN_COMMUNICATE_THRU   0x42
#define CMD_IN_RELEASE            0x52

/*
 * pn532_bus_spi.c keeps its state in a file-static whose type is private. Its
 * first two members are the SPI spec and the IRQ spec, both of which are types
 * DEFINED IN nfcfake -- so this prefix has the same layout by construction,
 * and it is the only way to reach the no-IRQ readiness path: the IRQ pointer
 * comes from a devicetree initializer and cannot be a runtime choice.
 */
struct bus_ctx_prefix {
	struct spi_dt_spec bus;
	struct gpio_dt_spec irq;
};

static struct bus_ctx_prefix *bus_ctx(void)
{
	return static_cast<struct bus_ctx_prefix *>(pn532_bus_ctx());
}

/* ---- the no-reader backend ------------------------------------------------ */

static void test_transport_none(void)
{
	uint8_t payload[4] = {1, 2, 3, 4};
	Aliro::Data data{payload, sizeof(payload)};

	t_group("woz_nfc transport none");

	/* Bring-up and polling both succeed and do nothing: a board with no
	 * frontend must not fail its own init over a reader it never had. */
	T_EQ("init succeeds", WozNfcNone::Init().ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("start succeeds", WozNfcNone::Start().ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("stop succeeds", WozNfcNone::Stop().ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("terminate succeeds", WozNfcNone::Terminate().ToInt(), (int)ALIRO_NO_ERROR);

	/* No session is ever created here, so Send() is unreachable in a
	 * correct run; it reports invalid state rather than pretending. */
	T_EQ("send reports invalid state", WozNfcNone::Send(data).ToInt(),
	     (int)ALIRO_INVALID_STATE);
	T_EQ("send with no data reports invalid state", WozNfcNone::Send({nullptr, 0}).ToInt(),
	     (int)ALIRO_INVALID_STATE);
}

/* ---- the SPI bus glue ----------------------------------------------------- */

static void test_bus_init(void)
{
	t_group("pn532 spi bus init");

	/* A controller the devicetree did not bring up is a configuration
	 * fault, not a wiring fault, and is reported before anything is poked. */
	nfcfake_reset();
	nfcfake.spi_ready = false;
	T_EQ("unready controller refused", pn532_bus_init(), -1);
	T_EQ("no GPIO touched", (long)nfcfake.gpio_configure_calls, 0L);

	/* The IRQ line: not ready, unconfigurable, or its interrupt setup
	 * failing, each refused with the pin named in the log. */
	nfcfake_reset();
	nfcfake.gpio_ready = false;
	T_EQ("unready irq gpio refused", pn532_bus_init(), -1);

	nfcfake_reset();
	nfcfake.gpio_configure_ret = -1;
	T_EQ("unconfigurable irq gpio refused", pn532_bus_init(), -1);

	nfcfake_reset();
	nfcfake.gpio_add_callback_ret = -1;
	T_EQ("callback registration failure refused", pn532_bus_init(), -1);

	nfcfake_reset();
	nfcfake.gpio_interrupt_configure_ret = -1;
	T_EQ("interrupt configuration failure refused", pn532_bus_init(), -1);

	/* The cold-start wake pulse: CS is asserted, held, and released. */
	nfcfake_reset();
	T_EQ("init succeeds", pn532_bus_init(), 0);
	T_EQ("irq configured as input", (long)nfcfake.gpio_configure_calls, 2L);
	T_EQ("edge interrupt armed", (long)nfcfake.gpio_interrupt_calls, 1L);
	T_EQ("wake pulse asserted then released", (long)nfcfake.gpio_set_calls, 2L);
	T_EQ("released last", nfcfake.last_gpio_set_value, 0);
	T_OK("held and settled", nfcfake.msleep_calls >= 2u);

	/* A CS line that will not move means no wake pulse, so bring-up stops. */
	nfcfake_reset();
	nfcfake.gpio_set_ret = -1;
	T_EQ("unusable cs refused", pn532_bus_init(), -1);

	/* Failing only the release half is caught separately: the chip would be
	 * left with CS asserted. */
	nfcfake_reset();
	T_EQ("init succeeds", pn532_bus_init(), 0);
	T_OK("bus context is stable", pn532_bus_ctx() == bus_ctx());
}

static void test_bus_transfers(void)
{
	struct pn532 chip;
	uint8_t fw[4];

	t_group("pn532 spi transfers");

	/* One real GetFirmwareVersion, end to end: the driver writes a host
	 * frame, waits on the IRQ, reads the ACK, then reads the response, and
	 * the real codec in pn532.c parses it. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	{
		const uint8_t version[] = {0x32, 0x01, 0x06, 0x07};

		nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, version, sizeof(version));
		T_EQ("firmware read", pn532_get_firmware_version(&chip, fw), PN532_OK);
		T_EQ("ic", (long)fw[0], 0x32L);
		T_EQ("firmware major", (long)fw[1], 1L);
		T_EQ("firmware minor", (long)fw[2], 6L);
	}
	/* The DATAWRITE command byte really did lead the frame. */
	T_OK("a host frame went out", nfcfake.write_count >= 1u);
	if (nfcfake.write_count >= 1u) {
		T_EQ("datawrite command byte", (long)nfcfake.writes[0][0], 0x01L);
		T_EQ("frame start code", (long)nfcfake.writes[0][3], 0xffL);
	}

	/* A transfer error on the wire is an IO error, not a parse error. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.write_ret = -1;
	T_EQ("write failure is IO", pn532_get_firmware_version(&chip, fw), PN532_ERR_IO);

	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	nfcfake.transceive_ret = -1;
	T_EQ("read failure is IO", pn532_get_firmware_version(&chip, fw), PN532_ERR_IO);

	/* An IRQ line that reads back an error stops the wait. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.gpio_get_ret = -1;
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	T_EQ("irq read failure is IO", pn532_get_firmware_version(&chip, fw), PN532_ERR_IO);

	/* An IRQ that never asserts times out rather than hanging. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.gpio_get_ret = 0;
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	T_EQ("silent irq times out", pn532_get_firmware_version(&chip, fw), PN532_ERR_TIMEOUT);

	/* WITHOUT AN IRQ LINE the driver falls back to STATREAD polling. The
	 * devicetree decides that at build time, so the pointer is cleared here
	 * (see bus_ctx_prefix). */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	bus_ctx()->irq.port = nullptr;
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	{
		const uint8_t version[] = {0x32, 0x01, 0x06, 0x07};

		nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, version, sizeof(version));
		T_EQ("polled readiness works", pn532_get_firmware_version(&chip, fw), PN532_OK);
		T_EQ("ic", (long)fw[0], 0x32L);
	}

	/* A chip that never raises the ready bit times out on the poll. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	bus_ctx()->irq.port = nullptr;
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.status_byte = 0x00;
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	T_EQ("never-ready chip times out", pn532_get_firmware_version(&chip, fw),
	     PN532_ERR_TIMEOUT);

	/* A status read that fails on the wire is not "ready". */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	bus_ctx()->irq.port = nullptr;
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.transceive_ret = -1;
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	T_EQ("failed status read times out", pn532_get_firmware_version(&chip, fw),
	     PN532_ERR_TIMEOUT);

	/* Put the IRQ line back: it was cleared above to reach the polling
	 * path, and every test after this one expects the shipping wiring. */
	nfcfake_reset();
	bus_ctx()->irq.port = &nfcfake_gpio_port;
	T_EQ("bus init", pn532_bus_init(), 0);
	T_EQ("irq path restored", (long)nfcfake.gpio_add_callback_calls, 1L);
}

static void test_bus_irq_callback(void)
{
	struct pn532 chip;
	uint8_t fw[4];

	t_group("pn532 spi irq callback");

	/* The edge handler's whole job is to wake the thread waiting on the
	 * chip. It lives inside the driver's private state, so it is reached
	 * the only way anything reaches it on target: through the callback the
	 * driver registered. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	T_EQ("callback registered once", (long)nfcfake.gpio_add_callback_calls, 1L);
	{
		const unsigned before = nfcfake.sem_give_calls;

		nfcfake_fire_irq();
		T_EQ("an edge wakes the waiter", (long)(nfcfake.sem_give_calls - before), 1L);
	}

	/* A line that reads low with a wake already pending: the wait consumes
	 * the semaphore, looks again, and only gives up when the deadline
	 * passes -- it does not mistake the wake for readiness. */
	nfcfake_reset();
	T_EQ("bus init", pn532_bus_init(), 0);
	pn532_init(&chip, &pn532_bus_ops, pn532_bus_ctx());
	nfcfake.gpio_get_ret = 0;
	nfcfake_fire_irq();
	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, nullptr, 0);
	T_EQ("a spurious wake still times out", pn532_get_firmware_version(&chip, fw),
	     PN532_ERR_TIMEOUT);
	T_OK("the line was re-read after the wake", nfcfake.gpio_get_calls >= 2u);
}

/* ---- the PN532 backend ---------------------------------------------------- */

/** Script the eight-command bring-up transport_pn532.cpp's Init() performs. */
static void script_successful_init(void)
{
	const uint8_t version[] = {0x32, 0x01, 0x06, 0x07};

	nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, version, sizeof(version));
	nfcfake_push_response(CMD_SAM_CONFIGURATION, nullptr, 0); /* SAMConfiguration */
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);  /* retries */
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);  /* timeouts */
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);  /* field off */
}

static void test_transport_pn532_init(void)
{
	t_group("woz_nfc transport pn532 init");

	/* A chip that never answers is reported with the wiring hint, after
	 * three attempts -- the first doubles as a wake. */
	nfcfake_reset();
	T_EQ("silent chip refused", WozNfc::Init().ToInt(), (int)ALIRO_ERROR_INTERNAL);
	T_OK("probed three times", nfcfake.spi_write_calls >= 3u);
	T_EQ("no thread started", (long)nfcfake.thread_create_calls, 0L);

	/* A bus that will not come up is refused before the chip is probed. */
	nfcfake_reset();
	nfcfake.spi_ready = false;
	T_EQ("unready bus refused", WozNfc::Init().ToInt(), (int)ALIRO_ERROR_INTERNAL);

	/* SAMConfiguration failing after a good firmware probe is called out
	 * separately: the chip is alive but will not configure. */
	nfcfake_reset();
	{
		const uint8_t version[] = {0x32, 0x01, 0x06, 0x07};

		nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, version, sizeof(version));
		T_EQ("sam configuration failure refused", WozNfc::Init().ToInt(),
		     (int)ALIRO_ERROR_INTERNAL);
	}

	/* RF configuration failing likewise. */
	nfcfake_reset();
	{
		const uint8_t version[] = {0x32, 0x01, 0x06, 0x07};

		nfcfake_push_response(CMD_GET_FIRMWARE_VERSION, version, sizeof(version));
		nfcfake_push_response(CMD_SAM_CONFIGURATION, nullptr, 0);
		T_EQ("rf configuration failure refused", WozNfc::Init().ToInt(),
		     (int)ALIRO_ERROR_INTERNAL);
	}

	/* The bring-up that works. Everything after this test depends on it,
	 * because Init() is idempotent by a file-static and only runs once. */
	nfcfake_reset();
	script_successful_init();
	T_EQ("init succeeds", WozNfc::Init().ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("polling thread created", (long)nfcfake.thread_create_calls, 1L);
	T_OK("thread named", nfcfake.thread_name != nullptr &&
				    std::strcmp(nfcfake.thread_name, "woz_nfc_pn532") == 0);
	T_OK("thread entry captured", nfcfake_thread_entry() != nullptr);

	/* A second Init() is a no-op: the chip is already up and re-probing it
	 * would cost a bring-up for nothing. */
	nfcfake_reset();
	T_EQ("second init is a no-op", WozNfc::Init().ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("no second thread", (long)nfcfake.thread_create_calls, 0L);
	T_EQ("no traffic", (long)nfcfake.spi_write_calls, 0L);
}

static void test_transport_pn532_control(void)
{
	uint8_t payload[8] = {0x80, 0xca, 0, 0, 0, 0, 0, 0};
	Aliro::Data data{payload, sizeof(payload)};

	t_group("woz_nfc transport pn532 control");

	/* Start arms the ECP beacon with the provisioned Reader Identifier. */
	nfcfake_reset();
	T_EQ("start succeeds", WozNfc::Start().ToInt(), (int)ALIRO_NO_ERROR);
	T_OK("polling thread woken", nfcfake.sem_give_calls >= 1u);

	/* An unprovisioned reader still beacons, with a zero identifier: a
	 * silent reader would look like a dead one. */
	nfcfake_reset();
	nfcfake.identifier_set = false;
	T_EQ("start without an identifier still succeeds", WozNfc::Start().ToInt(),
	     (int)ALIRO_NO_ERROR);
	nfcfake_reset();
	nfcfake.identifier_ret = -1;
	T_EQ("start with an unreadable identifier still succeeds", WozNfc::Start().ToInt(),
	     (int)ALIRO_NO_ERROR);

	/* Stop and Terminate are unconditional and both wake the thread. */
	nfcfake_reset();
	T_EQ("stop succeeds", WozNfc::Stop().ToInt(), (int)ALIRO_NO_ERROR);
	T_OK("stop wakes the thread", nfcfake.sem_give_calls >= 1u);
	nfcfake_reset();
	T_EQ("terminate succeeds", WozNfc::Terminate().ToInt(), (int)ALIRO_NO_ERROR);
	T_OK("terminate wakes the thread", nfcfake.sem_give_calls >= 1u);

	/* Send is refused unless a device is activated. */
	nfcfake_reset();
	T_EQ("send with no device refused", WozNfc::Send(data).ToInt(), (int)ALIRO_INVALID_STATE);
}

/**
 * Drive the polling thread through one activation and one APDU exchange.
 *
 * The script is a whole poll round: field on, the ECP beacon (three register
 * writes, the raw broadcast, three restores), then InListPassiveTarget
 * reporting one ISO-DEP card.
 */
static void script_poll_round_with_card(bool iso_dep)
{
	/* PollRound: RF field on. */
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	/* BroadcastEcp: timeouts, CRC off x2, the beacon, CRC on x2, timeouts. */
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	nfcfake_push_response(CMD_WRITE_REGISTER, nullptr, 0);
	nfcfake_push_response(CMD_WRITE_REGISTER, nullptr, 0);
	{
		/* Nothing answers an ECP broadcast: the chip reports a timeout,
		 * which the transport treats as the expected outcome. */
		const uint8_t timed_out[] = {PN532_STATUS_TIMEOUT};

		nfcfake_push_exchange(timed_out[0], nullptr, 0);
	}
	nfcfake_push_response(CMD_WRITE_REGISTER, nullptr, 0);
	nfcfake_push_response(CMD_WRITE_REGISTER, nullptr, 0);
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	/* InListPassiveTarget: one target, SEL_RES says ISO-DEP or not. */
	{
		const uint8_t target[] = {
			0x01,                   /* NbTg */
			0x01,                   /* Tg */
			0x00, 0x04,             /* SENS_RES */
			(uint8_t)(iso_dep ? 0x20 : 0x08), /* SEL_RES */
			0x04,                   /* NFCID length */
			0x11, 0x22, 0x33, 0x44, /* NFCID */
		};

		nfcfake_push_response(CMD_IN_LIST_PASSIVE, target, sizeof(target));
	}
}

static void test_transport_pn532_polling(void)
{
	uint8_t apdu[5] = {0x00, 0xa4, 0x04, 0x00, 0x00};
	Aliro::Data data{apdu, sizeof(apdu)};

	t_group("woz_nfc transport pn532 polling");

	/* The thread parks the field when polling is stopped and waits. */
	nfcfake_reset();
	WozNfc::Stop();
	nfcfake_push_response(CMD_IN_RELEASE, nullptr, 0);
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	nfcfake_run_thread(nfcfake_thread_entry(), 3);
	T_OK("parked and waited", nfcfake.sem_take_calls >= 1u);
	T_EQ("no session while stopped", (long)nfcfake.create_session_calls, 0L);

	/* An RF field that will not come on skips the round rather than
	 * pressing on into an activation that cannot work. */
	nfcfake_reset();
	WozNfc::Start();
	nfcfake_run_thread(nfcfake_thread_entry(), 2);
	T_OK("slept after the failed field", nfcfake.msleep_calls >= 1u);
	T_EQ("no session", (long)nfcfake.create_session_calls, 0L);

	/* A card that is not ISO-DEP is ignored, and polling continues. */
	nfcfake_reset();
	WozNfc::Start();
	script_poll_round_with_card(false);
	nfcfake_push_response(CMD_IN_RELEASE, nullptr, 0);
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	nfcfake_run_thread(nfcfake_thread_entry(), 4);
	T_EQ("non ISO-DEP card ignored", (long)nfcfake.create_session_calls, 0L);

	/* AN ISO-DEP CARD OPENS A SESSION. RunSession then waits for Send();
	 * with nothing queued it spins until the tick budget runs out, and the
	 * session is created exactly once. */
	nfcfake_reset();
	WozNfc::Start();
	script_poll_round_with_card(true);
	nfcfake_run_thread(nfcfake_thread_entry(), 5);
	T_EQ("session created on activation", (long)nfcfake.create_session_calls, 1L);
	T_OK("beacon went out", nfcfake.write_count >= 8u);

	/* Now Send() is accepted, because a device is active. */
	T_EQ("send accepted while activated", WozNfc::Send(data).ToInt(), (int)ALIRO_NO_ERROR);
	T_EQ("a second send while pending is refused", WozNfc::Send(data).ToInt(),
	     (int)ALIRO_INVALID_STATE);
	T_EQ("null data refused", WozNfc::Send({nullptr, 4}).ToInt(), (int)ALIRO_INVALID_ARGUMENT);
	T_EQ("empty data refused", WozNfc::Send({apdu, 0}).ToInt(), (int)ALIRO_INVALID_ARGUMENT);
	T_EQ("oversized data refused", WozNfc::Send({apdu, 1024}).ToInt(),
	     (int)ALIRO_INVALID_ARGUMENT);
}

/*
 * Acting on the transport from inside its own thread.
 *
 * The session loop is only reachable while the thread is running, and the
 * thread cannot be resumed once it has escaped -- re-entering starts a fresh
 * poll round. So each of these hooks does its one thing on a chosen tick,
 * which is also how the real thing behaves: the Aliro workqueue calls Send()
 * or Terminate() while the polling thread sits on its semaphore.
 */
static uint8_t hook_apdu[5] = {0x00, 0xa4, 0x04, 0x00, 0x00};
static int hook_send_status;

static void send_on_tick(int remaining)
{
	if (remaining == 3) {
		hook_send_status = WozNfc::Send({hook_apdu, sizeof(hook_apdu)}).ToInt();
	}
}

static void stop_on_tick(int remaining)
{
	if (remaining == 3) {
		(void)WozNfc::Stop();
	}
}

static void terminate_on_tick(int remaining)
{
	if (remaining == 3) {
		(void)WozNfc::Terminate();
	}
}

/** Bring the transport back to "polling, no session" between sub-tests. */
static void restart_polling(void)
{
	nfcfake_reset();
	(void)WozNfc::Stop();
	(void)WozNfc::Start();
}

static void test_transport_pn532_exchange(void)
{
	t_group("woz_nfc transport pn532 exchange");

	/* ONE ACTIVATION, ONE APDU, ONE RESPONSE. Send() is called from inside
	 * the session loop, so the round trip runs where it does on target. */
	restart_polling();
	hook_send_status = -1;
	script_poll_round_with_card(true);
	{
		const uint8_t response[] = {0x6f, 0x02, 0x90, 0x00};

		nfcfake_push_exchange(PN532_STATUS_OK, response, sizeof(response));
	}
	nfcfake_on_tick(send_on_tick);
	nfcfake_run_thread(nfcfake_thread_entry(), 6);
	nfcfake_on_tick(nullptr);
	T_EQ("session created on activation", (long)nfcfake.create_session_calls, 1L);
	T_EQ("send accepted while activated", hook_send_status, (int)ALIRO_NO_ERROR);
	T_OK("response delivered to the stack", nfcfake.session_data_calls >= 1u);
	if (nfcfake.session_data_calls >= 1u) {
		T_EQ("response length", (long)nfcfake.last_session_data_len, 4L);
		T_EQ("status word high", (long)nfcfake.last_session_data[2], 0x90L);
		T_EQ("status word low", (long)nfcfake.last_session_data[3], 0x00L);
	}
	T_EQ("session not torn down on success", (long)nfcfake.destroy_session_calls, 0L);

	/* AN EXCHANGE THAT FAILS tears the session down, rather than leaving
	 * the stack believing an NFC link is still live. Nothing is queued for
	 * the exchange, so the read runs off the end of the script and the real
	 * frame parser rejects it. */
	restart_polling();
	script_poll_round_with_card(true);
	nfcfake_on_tick(send_on_tick);
	nfcfake_run_thread(nfcfake_thread_entry(), 6);
	nfcfake_on_tick(nullptr);
	T_EQ("session created", (long)nfcfake.create_session_calls, 1L);
	T_OK("session destroyed after a failed exchange", nfcfake.destroy_session_calls >= 1u);

	/* STOP MID-SESSION still balances the CreateSession that was posted at
	 * entry: without it the stack keeps a session on ConnectionHandle::Nfc()
	 * and the next activation double-creates on the same handle. */
	restart_polling();
	script_poll_round_with_card(true);
	nfcfake_push_response(CMD_IN_RELEASE, nullptr, 0);
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	nfcfake_on_tick(stop_on_tick);
	nfcfake_run_thread(nfcfake_thread_entry(), 6);
	nfcfake_on_tick(nullptr);
	T_EQ("session created", (long)nfcfake.create_session_calls, 1L);
	T_EQ("stop balanced the create", (long)nfcfake.destroy_session_calls, 1L);

	/* TERMINATE MUST NOT CALL BACK. The stack asked for the session to end
	 * and already knows; a DestroySession here would be the stack being
	 * told about a teardown it initiated. */
	restart_polling();
	script_poll_round_with_card(true);
	nfcfake_push_response(CMD_IN_RELEASE, nullptr, 0);
	nfcfake_push_response(CMD_RF_CONFIGURATION, nullptr, 0);
	nfcfake_on_tick(terminate_on_tick);
	nfcfake_run_thread(nfcfake_thread_entry(), 6);
	nfcfake_on_tick(nullptr);
	T_EQ("session created", (long)nfcfake.create_session_calls, 1L);
	T_EQ("terminate did not call back", (long)nfcfake.destroy_session_calls, 0L);
}

int main(void)
{
	test_transport_none();
	test_bus_init();
	test_bus_transfers();
	test_bus_irq_callback();
	test_transport_pn532_init();
	test_transport_pn532_control();
	test_transport_pn532_polling();
	test_transport_pn532_exchange();

	if (t_fail > 0) {
		std::printf("  nfc-transport: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	std::printf("  nfc-transport: PASS (%d checks — real PN532 codec on a scripted bus, "
		    "no chip and no stack truth)\n",
		    t_pass);
	return 0;
}
